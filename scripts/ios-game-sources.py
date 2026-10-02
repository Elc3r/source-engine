#!/usr/bin/env python3
"""Generate iOS Portal source lists from the repository's original VPC projects."""
import argparse
import ast
from pathlib import Path
import sys
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts/waifulib'))
import vpc_parser

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    lines = ['# Generated from original Portal VPC projects; do not edit.']
    for side in ('client', 'server'):
        directory = ROOT / 'game' / side
        env = SimpleNamespace(SUBPROJECT_PATH=[str(directory)],
                              DEFINES=['IOS', 'POSIX', '_POSIX', 'PLATFORM_POSIX'])
        project = vpc_parser.parse_vpcs(env, [side+'_base.vpc', side+'_portal.vpc'], '../..')
        sources = list(dict.fromkeys(str((directory / s).resolve()) for s in project['sources']
                                     if Path(s).suffix.lower() in ('.cpp', '.c', '.cc')))
        missing = [s for s in sources if not Path(s).is_file()]
        if missing:
            parser.error('Missing VPC sources: '+', '.join(missing))
        if side == 'client':
            sources.append(str(directory / 'in_touch.cpp'))
        includes = list(dict.fromkeys(str((directory / s).resolve()) for s in project['includes']))
        defines = [s for s in project['defines'] if s != 'PROTECTED_THINGS_ENABLE']
        for key, values in [('SOURCES', sources), ('INCLUDES', includes), ('DEFINES', defines)]:
            lines.append('set(PORTAL_'+side.upper()+'_'+key)
            lines.extend('  "'+v.replace('"', '\\"')+'"' for v in values)
            lines.append(')')
    for module in ('particles', 'choreoobjects', 'tier2', 'tier3', 'vgui2/matsys_controls'):
        directory = ROOT / module
        tree = ast.parse((directory / 'wscript').read_text())
        build = next(n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name == 'build')
        source = next(n.value for n in build.body if isinstance(n, ast.Assign)
                      and any(isinstance(t, ast.Name) and t.id == 'source' for t in n.targets))
        lines.append('list(APPEND PORTAL_SUPPORT_SOURCES')
        lines.extend('  "'+str((directory / name).resolve())+'"' for name in ast.literal_eval(source))
        lines.append(')')
    for module, variable, extra in (
        ('gameui', 'IOS_GAMEUI_SOURCES', []),
        ('vgui2/src', 'IOS_VGUI_SOURCES', ['system_posix.cpp']),
        ('vguimatsurface', 'IOS_MATSURFACE_SOURCES', []),
        ('vgui2/vgui_surfacelib', 'IOS_SURFACELIB_SOURCES', ['linuxfont.cpp'])):
        directory = ROOT / module
        tree = ast.parse((directory / 'wscript').read_text())
        build = next(n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name == 'build')
        source = next(n.value for n in build.body if isinstance(n, ast.Assign)
                      and any(isinstance(t, ast.Name) and t.id == 'source' for t in n.targets))
        names = ast.literal_eval(source) + extra
        lines.append('set(' + variable)
        lines.extend('  "' + str((directory / name).resolve()) + '"'
                     for name in names if not name.endswith('memoverride.cpp'))
        lines.append(')')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text('\n'.join(lines)+'\n')

if __name__ == '__main__':
    main()
