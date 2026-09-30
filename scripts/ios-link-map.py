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
    args = parser.parse_args()
    build = args.build_dir.resolve()
    cmake = build / 'togles'
    if not (cmake / 'CMakeCache.txt').exists():
        parser.error('First run scripts/build-ios-bootstrap.py --togles for this build directory')
    report = build / 'map-link.json'
    report.unlink(missing_ok=True)
    commands = [
        ('configure', ['cmake', '-S', str(ROOT / 'ios/graphics'), '-B', str(cmake)]),
        ('build', ['cmake', '--build', str(cmake), '--parallel', '8', '--target', 'EngineMapLinkCheck']),
    ]
    transcript = ''
    for phase, command in commands:
        result = subprocess.run(command, cwd=ROOT, text=True, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT)
        transcript += result.stdout
        if result.returncode:
            break
    (build / 'map-link.log').write_text(transcript)
    missing = []
    for match in re.finditer(r'^  "(.+)", referenced from:\n((?: {6,}.*\n)*)',
                             transcript, re.M):
        missing.append({'symbol': match[1],
                        'references': [line.strip() for line in match[2].splitlines()]})
    report.write_text(json.dumps({
        'linked': result.returncode == 0, 'runtime_map_load_tested': False,
        'phase': 'link' if missing else phase, 'exit_code': result.returncode,
        'unresolved_count': len(missing), 'unresolved': missing,
        'log': 'map-link.log',
        'scope': 'Strict dylib link of actual engine sources against the iOS graphics runtime. No startup or map execution.',
    }, indent=2) + '\n')
    print(f'Map link: {"PASS" if result.returncode == 0 else "FAIL"}; {len(missing)} unresolved symbols')
    print(f'Report: {report}\nFull diagnostics: {build / "map-link.log"}')
    return result.returncode


if __name__ == '__main__':
    raise SystemExit(main())
