#!/usr/bin/env python3
"""Verify the real iOS world-loader archive and report its link dependencies.

This checks compiled objects, not a linked engine or runtime map load. Symbols
found in foundation artifacts are potential providers, not proof of linkability.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
UNITS = ('modelloader.cpp', 'gl_lightmap.cpp', 'gl_rsurf.cpp', 'cmodel_bsp.cpp')
ENTRY_POINTS = ('CMapLoadHelper::InitFromMemory(', 'Mod_LoadFaces(', 'Mod_LoadNodes(',
                'Mod_LoadLeafs(', 'R_BuildLightMap(', 'R_BuildWorldLists(',
                'R_DrawWorldLists(', 'CollisionBSPData_Load(')


def run(*args, input=None):
    return subprocess.run([str(arg) for arg in args], check=True, text=True,
                          input=input, stdout=subprocess.PIPE).stdout


def symbols(path):
    objects, current = {}, None
    for line in run('xcrun', 'nm', '-g', '-P', path).splitlines():
        if line.endswith('.o:'):
            current = line[:-1]
            objects[current] = {'defined': set(), 'undefined': set()}
            continue
        match = re.fullmatch(r'(\S+) ([A-Za-z?]) [0-9a-fA-F]+ [0-9a-fA-F]+', line)
        if not match:
            continue
        name, kind = match.groups()
        record = objects.setdefault(current or path.name, {'defined': set(), 'undefined': set()})
        record['undefined' if kind.upper() == 'U' else 'defined'].add(name)
    if not objects:
        raise ValueError('No symbols found in ' + str(path))
    return objects


def demangle(names):
    ordered = sorted(names)
    # Mach-O has one leading underscore in addition to the Itanium ABI prefix.
    output = run('xcrun', 'c++filt', '-n', input='\n'.join(name[1:] if name.startswith('_') else name
                                                        for name in ordered) + '\n').splitlines()
    if len(output) != len(ordered):
        raise ValueError('Unexpected demangler output')
    return dict(zip(ordered, output))


def check(build, target):
    path = build / 'world-loader-check.json'
    path.unlink(missing_ok=True)
    archive = build / 'togles/libEngineWorldLoader.a'
    if run('xcrun', 'lipo', '-archs', archive).strip() != 'arm64':
        raise ValueError('World loader must contain only ARM64 objects')
    table = symbols(archive)
    if set(table) != {unit + '.o' for unit in UNITS}:
        raise ValueError('Archive does not contain the four expected engine units')
    expected_platform = 'IOSSIMULATOR' if target == 'simulator' else 'IOS'
    versions = {}
    for unit in UNITS:
        matches = list((build / 'togles/CMakeFiles/EngineWorldLoader.dir').rglob(unit + '.o'))
        if len(matches) != 1:
            raise ValueError('Cannot uniquely locate object for ' + unit)
        output = run('xcrun', 'vtool', '-show-build', matches[0])
        platform = re.search(r'^\s*platform (\S+)$', output, re.M)
        if not platform or platform[1] != expected_platform:
            raise ValueError('Wrong Mach-O platform for ' + unit)
        versions[unit] = {key: re.search(r'^\s*' + key + r' (\S+)$', output, re.M)[1]
                          for key in ('platform', 'minos', 'sdk')}
    defined = set().union(*(item['defined'] for item in table.values()))
    undefined = set().union(*(item['undefined'] for item in table.values())) - defined
    readable = demangle(defined | undefined)
    entries = {}
    for prefix in ENTRY_POINTS:
        matches = [readable[name] for name in defined if readable[name].startswith(prefix)]
        if not matches:
            raise ValueError('Missing real engine entry point: ' + prefix)
        entries[prefix] = sorted(matches)
    providers = {}
    for relative in ('tier0/libtier0.dylib', 'tier1/libtier1.a', 'mathlib/libmathlib.a'):
        available = set().union(*(item['defined'] for item in symbols(build / relative).values()))
        for name in undefined & available:
            providers.setdefault(name, []).append(relative)
    dependencies = []
    for name in sorted(undefined, key=lambda name: readable[name]):
        dependencies.append({'symbol': name, 'name': readable[name],
            'referenced_by': sorted(unit for unit, item in table.items() if name in item['undefined']),
            'defined_in_foundation_artifacts': providers.get(name, [])})
    report = {
        'compile_check_passed': True, 'linked_engine': False, 'runtime_map_load_tested': False,
        'target': target, 'architecture': 'arm64', 'objects': versions,
        'archive': str(archive.relative_to(build)),
        'archive_sha256': hashlib.sha256(archive.read_bytes()).hexdigest(),
        'engine_source_sha256': {unit: hashlib.sha256((ROOT / 'engine' / unit).read_bytes()).hexdigest()
                                 for unit in UNITS},
        'entry_points': entries, 'external_symbol_count': len(undefined),
        'symbols_with_foundation_definitions': len(providers),
        'dependencies': dependencies,
        'scope': 'Object-level dependencies; includes system/C++ runtime symbols. Not a full link check.'}
    path.write_text(json.dumps(report, indent=2, sort_keys=True) + '\n')
    print(f'World loader compile check: PASS ({target}, ARM64, {len(UNITS)} engine units)')
    print(f'External symbols: {len(undefined)}; foundation candidates: {len(providers)}')
    print(f'Not linked or runtime-tested. Report: {path}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--target', choices=('simulator', 'device'), required=True)
    parser.add_argument('--build-dir', type=Path, required=True)
    args = parser.parse_args()
    check(args.build_dir.resolve(), args.target)
