#!/usr/bin/env python3
"""Compile selected SM2 probe shaders, or validate/stage an existing shader cache.

FXC runs on the build host only. On macOS, pass a Wine/CrossOver command prefix
as --runner-json; the prefix must expose the host filesystem through drive Z:.
No compiler or Wine component is bundled in the iOS application.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
JOBS = {
    'ios_probe_vs20': ('ios/shaders/probe_vs20.hlsl', 'vs_2_0', [{}]),
    'ios_probe_ps20': ('ios/shaders/probe_ps20.hlsl', 'ps_2_0', [{}]),
    'ios_probe_sample': ('ios/shaders/probe_ps20.hlsl', 'ps_2_0', [{}]),
    'screenspaceeffect_vs20': ('materialsystem/stdshaders/screenspaceeffect_vs20.fxc',
                             'vs_2_0', [{'X360APPCHOOSER': 0}, {'X360APPCHOOSER': 1}]),
}


def combo_layout(source, profile):
    layout = {'STATIC': [], 'DYNAMIC': []}
    tag = 'ps20b' if profile == 'ps_2_b' else profile.replace('_', '')
    for kind, name, low, high, suffix in re.findall(
            r'//\s*(STATIC|DYNAMIC):\s*"(\w+)"\s*"(\d+)\.\.(\d+)"([^\r\n]*)',
            (ROOT / source).read_text(encoding='latin1')):
        tags = re.findall(r'\[(vs\w+|ps\w+|XBOX)\]', suffix)
        if 'XBOX' in tags or (tags and tag not in tags):
            continue
        if any(item[0] == name for item in layout[kind]):
            raise ValueError('Duplicate combo definition: ' + name)
        layout[kind].append((name, int(low), int(high)))
    return layout


SPARSE = {}
for name, source, profile, choices in [
    ('vertexlit_and_unlit_generic_vs20', 'vertexlit_and_unlit_generic_vs20.fxc',
     'vs_2_0', [{'USE_STATIC_CONTROL_FLOW': 0}, {'USE_STATIC_CONTROL_FLOW': 1}]),
    ('vertexlit_and_unlit_generic_ps20b', 'vertexlit_and_unlit_generic_ps2x.fxc',
     'ps_2_b', [{}]),
]:
    source = 'materialsystem/stdshaders/' + source
    layout = combo_layout(source, profile)
    defaults = {key: low for entries in layout.values() for key, low, high in entries}
    JOBS[name] = (source, profile, [dict(defaults, **choice) for choice in choices])
    SPARSE[name] = layout


def combo_index(entries, defines):
    index, count = 0, 1
    for name, low, high in entries:
        value = defines[name]
        if not low <= value <= high:
            raise ValueError('Combo out of range: ' + name)
        index += (value - low) * count
        count *= high - low + 1
    return index, count



def combo_header(name):
    directory = ROOT / 'materialsystem/stdshaders/fxctmp9'
    return next(path for path in directory.iterdir() if path.name.lower() == name + '.inc')


def validate_combo_indices():
    for name, layout in SPARSE.items():
        defaults = JOBS[name][2][0]
        _, dynamic_count = combo_index(layout['DYNAMIC'], defaults)
        coefficients = {}
        for kind, stride in [('DYNAMIC', 1), ('STATIC', dynamic_count)]:
            for key, low, high in layout[kind]:
                if low != 0:
                    raise ValueError('Selected shaders require zero-based combo ranges')
                coefficients[key] = stride
                stride *= high - low + 1
        actual = {key: int(value) for value, key in re.findall(
            r'\(\s*(\d+)\s*\*\s*m_n(\w+)\s*\)',
            combo_header(name).read_text(encoding='latin1'))}
        if actual != coefficients:
            raise ValueError('HLSL combo indices disagree with C++ header: ' + name)


def sparse_vcs(layout, variants, codes):
    groups = {}
    for defines, code in zip(variants, codes):
        static, static_count = combo_index(layout['STATIC'], defines)
        dynamic, dynamic_count = combo_index(layout['DYNAMIC'], defines)
        groups.setdefault(static, {})[dynamic] = code
    # VCS v6: sparse static dictionary + sentinel, no aliases. Each dynamic
    # record uses a local ID and an uncompressed block accepted by the loader.
    offset = 28 + 8 * (len(groups) + 1) + 4
    records, payload = bytearray(), bytearray()
    for static, dynamics in sorted(groups.items()):
        records += struct.pack('<2I', static, offset + len(payload))
        for dynamic, code in sorted(dynamics.items()):
            block = struct.pack('<2I', dynamic, len(code)) + code
            if len(block) > 1 << 17:
                raise ValueError('Shader exceeds VCS block limit')
            payload += struct.pack('<I', 0x80000000 | len(block)) + block
        payload += struct.pack('<I', 0xffffffff)
    records += struct.pack('<2I', 0xffffffff, offset + len(payload))
    return (struct.pack('<7I', 6, static_count * dynamic_count, dynamic_count,
                        0, 0, len(groups) + 1, 0) + records + struct.pack('<I', 0) + payload)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def source_hashes():
    result = {}
    def visit(path):
        path = path.resolve()
        relative = path.relative_to(ROOT).as_posix()
        if relative in result:
            return
        result[relative] = digest(path)
        for include in re.findall(r'^\s*#\s*include\s*"([^"]+)"',
                                  path.read_text(encoding='latin1'), re.M):
            visit(path.parent / include)
    validate_combo_indices()
    for name in SPARSE:
        header = combo_header(name)
        result[header.relative_to(ROOT).as_posix()] = digest(header)
    visit(ROOT / 'public/materialsystem/shader_vcs_version.h')
    for source, _, _ in JOBS.values():
        visit(ROOT / source)
    result[Path(__file__).relative_to(ROOT).as_posix()] = digest(Path(__file__))
    return dict(sorted(result.items()))


def windows_path(path):
    path = path.resolve()
    return str(path) if os.name == 'nt' else 'Z:' + str(path).replace('/', '\\')


def vcs(combos):
    # VCS v4: direct dictionary entries, one dynamic combo per static combo.
    offset = 28 + 8 * len(combos)
    dictionary = bytearray()
    for code in combos:
        dictionary += struct.pack('<2I', offset, len(code))
        offset += len(code)
    return struct.pack('<7I', 4, len(combos), 1, 0, 0, 0, 0) + dictionary + b''.join(combos)


def stage(cache, destination):
    manifest = json.loads((cache / 'manifest.json').read_text())
    if manifest.get('schema') != 1 or manifest.get('sources') != source_hashes():
        raise ValueError('Shader cache is stale; rebuild it from the current sources')
    expected = {name + '.vcs' for name in JOBS}
    if set(manifest.get('outputs', {})) != expected:
        raise ValueError('Shader cache has an unexpected set of outputs')
    for name, sha in manifest['outputs'].items():
        if digest(cache / name) != sha:
            raise ValueError('Shader cache checksum mismatch: ' + name)
    target = destination / 'shaders/fxc'
    target.mkdir(parents=True, exist_ok=True)
    for name in sorted(expected):
        shutil.copy2(cache / name, target / name)
    shutil.copy2(cache / 'manifest.json', destination / 'compiled-shaders.json')


def compile_cache(cache, compiler, runner):
    cache.mkdir(parents=True, exist_ok=True)
    manifest = {'schema': 1, 'compiler_sha256': digest(compiler),
                'sources': source_hashes(), 'jobs': JOBS, 'outputs': {}}
    # Build everything before replacing the manifest; failed builds cannot be
    # mistaken for a complete cache. No timestamps or absolute paths in outputs.
    with tempfile.TemporaryDirectory(prefix='fxc-', dir=cache) as temporary:
        temporary = Path(temporary)
        for name, (source, profile, variants) in JOBS.items():
            codes = []
            for index, defines in enumerate(variants):
                output = temporary / (name + '-' + str(index) + '.bin')
                arguments = ['/nologo', '/T' + profile, '/Emain',
                             '/DSHADER_MODEL_' + profile.upper() + '=1']
                arguments += ['/D' + key + '=' + str(value) for key, value in sorted(defines.items())]
                arguments += ['/Fo' + windows_path(output), windows_path(ROOT / source)]
                subprocess.run(runner + [str(compiler)] + arguments, cwd=ROOT,
                               check=True, timeout=120)
                code = output.read_bytes()
                version = {'vs_2_0': 0xfffe0200, 'ps_2_0': 0xffff0200, 'ps_2_b': 0xffff0201}[profile]
                if len(code) % 4 or len(code) < 8 or struct.unpack_from('<I', code)[0] != version or code[-4:] != b'\xff\xff\x00\x00':
                    raise ValueError('Invalid SM2 bytecode: ' + name + ' version=' + code[:4].hex())
                codes.append(code)
            binary = temporary / (name + '.vcs')
            binary.write_bytes(sparse_vcs(SPARSE[name], variants, codes) if name in SPARSE else vcs(codes))
            manifest['outputs'][binary.name] = digest(binary)
        for name in manifest['outputs']:
            shutil.copy2(temporary / name, cache / name)
        (cache / 'manifest.json').write_text(json.dumps(manifest, indent=2, sort_keys=True) + '\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'build-ios-shaders/compiled')
    parser.add_argument('--fxc', type=Path, default=ROOT / 'dx9sdk/utilities/fxc.exe')
    parser.add_argument('--runner-json', default='[]', help='JSON array prepended to FXC invocation')
    parser.add_argument('--stage', type=Path, help='Validate cache and copy it into this asset directory; do not compile')
    args = parser.parse_args()
    try:
        if args.stage:
            stage(args.output.resolve(), args.stage.resolve())
        else:
            runner = json.loads(args.runner_json)
            if not isinstance(runner, list) or any(not isinstance(item, str) for item in runner):
                raise ValueError('--runner-json must be an array of strings')
            if os.name != 'nt' and not runner:
                raise ValueError('Non-Windows hosts require --runner-json with a Wine/CrossOver command')
            compile_cache(args.output.resolve(), args.fxc.resolve(), runner)
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        parser.exit(1, str(error) + '\n')


if __name__ == '__main__':
    main()
