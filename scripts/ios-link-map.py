#!/usr/bin/env python3
"""Strictly link the real iOS map loader and renderer; report actual failures.

Requires an existing --togles bootstrap build. This does not run the loader.
An unresolved link returns a nonzero status, never a successful probe result.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, default=ROOT / 'build-ios-simulator')
    parser.add_argument('--interface-scope', action='store_true',
                        help='Export loader/renderer anchors only and remove unreachable code')
    args = parser.parse_args()
    build = args.build_dir.resolve()
    cmake = build / 'togles'
    if not (cmake / 'CMakeCache.txt').exists():
        parser.error('First run scripts/build-ios-bootstrap.py --togles for this build directory')
    report_stem = 'map-interface-link' if args.interface_scope else 'map-link'
    report = build / (report_stem + '.json')
    log_name = report_stem + '.log'
    report.unlink(missing_ok=True)
    commands = [
        ('configure', ['cmake', '-S', str(ROOT / 'ios/graphics'), '-B', str(cmake),
                       '-DENGINE_MAP_INTERFACE_SCOPE=' + ('ON' if args.interface_scope else 'OFF')]),
        ('build', ['cmake', '--build', str(cmake), '--parallel', '8', '--target', 'EngineMapLinkCheck']),
    ]
    transcript = ''
    for phase, command in commands:
        result = subprocess.run(command, cwd=ROOT, text=True, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT)
        transcript += result.stdout
        if result.returncode:
            break
    (build / log_name).write_text(transcript)
    missing = []
    for match in re.finditer(r'^  "(.+)", referenced from:\n((?: {6,}.*\n)*)',
                             transcript, re.M):
        missing.append({'symbol': match[1],
                        'references': [line.strip() for line in match[2].splitlines()]})
    failure_phase = 'link' if missing else phase
    report.write_text(json.dumps({
        'linked': result.returncode == 0, 'runtime_map_load_tested': False,
        'phase': failure_phase, 'exit_code': result.returncode,
        'unresolved_count': len(missing), 'unresolved': missing,
        'log': log_name,
        'interface_scope': args.interface_scope,
        'exported_anchors': ['SourceIOSMapLoaderLinkAnchor', 'SourceIOSWorldRendererLinkAnchor',
                             'SourceIOSInitializeMapLoader', 'SourceIOSShutdownMapLoader',
                             'SourceIOSLoadWorldMap'],
        'scope': 'Strict dylib link of actual engine sources against the iOS graphics runtime. Includes loader bootstrap entry points; this command does not execute startup or load maps.',
    }, indent=2) + '\n')
    if result.returncode and not missing:
        print(f'Map link: NOT COMPLETED; {failure_phase} failed (exit {result.returncode})')
    else:
        print(f'Map link: {"PASS" if result.returncode == 0 else "FAIL"}; {len(missing)} unresolved symbols')
    print(f'Report: {report}\nFull diagnostics: {build / log_name}')
    return result.returncode


if __name__ == '__main__':
    raise SystemExit(main())
