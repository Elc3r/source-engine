#!/usr/bin/env python3
"""Build an isolated foundation app; optionally install and verify in Simulator."""
import argparse
import json
import os
from pathlib import Path
import plistlib
import shutil
import struct
import subprocess
import time
import zlib

ROOT = Path(__file__).resolve().parents[1]
BUNDLE_ID = 'org.sourceengine.bootstrap'


def run(*args, capture=False):
    return subprocess.run([str(arg) for arg in args], cwd=ROOT, check=True,
                          text=True, stdout=subprocess.PIPE if capture else None).stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--target', choices=['simulator', 'device'], default='simulator')
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument('--graphics', action='store_true', help='Build the independent SDL/Metal GPU probe')
    mode.add_argument('--angle', action='store_true', help='Build the SDL/ANGLE GLES texture probe (downloads pinned ANGLE)')
    mode.add_argument('--togles', action='store_true', help='Test engine shader translation and DXT decoding through ANGLE')
    parser.add_argument('--game-startup', nargs='?', const='server', choices=['server','server-cycle','client'],
                        help='Initialize Portal server, check its shutdown, or attempt client Init (requires game modules/data)')
    parser.add_argument('--game-modules', action='store_true', help='Build and verify actual Portal client/server factories (ToGLES only)')
    parser.add_argument('--world-loader-check', action='store_true',
                        help='Compile actual engine world-loading units and report link dependencies (ToGLES only)')
    parser.add_argument('--shader-cache', type=Path, help='Validated FXC shader cache from ios-compile-shaders.py (ToGLES only)')
    parser.add_argument('--portal-root', type=Path, help='Read existing Portal game data directly (simulator only)')
    parser.add_argument('--world-map', default='maps/testchmb_a_00.bsp', help='Map path for --portal-root')
    parser.add_argument('--min-version', help='Default: 16.0 for ANGLE, 15.0 otherwise')
    parser.add_argument('--simulator', metavar='UDID', help='Install, launch and verify on this simulator')
    args = parser.parse_args()
    if args.game_startup and (not args.game_modules or not args.portal_root):
        parser.error("--game-startup requires --game-modules and --portal-root")
    if args.game_modules and not args.togles:
        parser.error('--game-modules requires --togles')
    if args.world_loader_check and not args.togles:
        parser.error('--world-loader-check requires --togles')
    if args.shader_cache and not args.togles:
        parser.error('--shader-cache requires --togles')
    if args.shader_cache:
        args.shader_cache = args.shader_cache.resolve()
    if args.portal_root and (not args.togles or not args.simulator or args.target != "simulator"):
        parser.error("--portal-root requires --togles and --simulator")
    if args.portal_root:
        args.portal_root=args.portal_root.resolve()
        if not (args.portal_root / "portal/gameinfo.txt").is_file():
            parser.error("--portal-root must contain portal/gameinfo.txt")
    args.angle = args.angle or args.togles
    args.min_version = args.min_version or ('16.0' if args.angle else '15.0')
    if args.simulator and args.target != 'simulator':
        parser.error('--simulator requires --target=simulator')
    os.chdir(ROOT)
    build = ROOT / ('build-ios-' + args.target)
    if args.world_loader_check:
        (build / 'world-loader-check.json').unlink(missing_ok=True)
    if args.portal_root:
        (build / 'portal-map-load-result.json').unlink(missing_ok=True)
    # Waf's lock file is shared by configurations; always configure the requested
    # output first so a preceding desktop/device build cannot select the wrong SDK.
    import sys
    import re
    if not re.fullmatch(r'\d+\.\d+(\.\d+)?', args.min_version):
        parser.error('--min-version must be a version such as 15.0')
    if args.angle and tuple(map(int, args.min_version.split('.'))) < (16, 0):
        parser.error('The pinned ANGLE binaries require iOS 16.0 or newer')
    bundle_id = BUNDLE_ID + ('.togles' if args.togles else '.angle' if args.angle else '.graphics' if args.graphics else '')
    executable = 'SourceToGLES' if args.togles else 'SourceANGLE' if args.angle else 'SourceGraphics' if args.graphics else 'SourceBootstrap'
    result_name = 'togles' if args.togles else 'angle' if args.angle else 'graphics' if args.graphics else 'bootstrap'
    sdk = 'iphonesimulator' if args.target == 'simulator' else 'iphoneos'
    sdk_path = run('xcrun', '--sdk', sdk, '--show-sdk-path', capture=True).strip()
    triple = 'arm64-apple-ios' + args.min_version + ('-simulator' if args.target == 'simulator' else '')
    if args.graphics or args.angle:
        graphics_build = build / result_name
        angle_options = []
        if args.angle:
            from ios_angle import prepare, COMMIT, TAG, ARCHIVE_SHA256
            angle_root, angle_license = prepare(ROOT)
            angle_slice = 'ios-arm64_x86_64-simulator' if args.target == 'simulator' else 'ios-arm64'
            angle_options = ['-DANGLE_ROOT=' + str(angle_root), '-DANGLE_SLICE=' + angle_slice]
        if args.togles:
            run(sys.executable, 'waf', 'configure', '--ios-target=' + args.target,
                '--ios-min-version=' + args.min_version, '-o', build)
            run(sys.executable, 'waf', 'build')
            angle_options.append('-DENGINE_BUILD=' + str(build))
            angle_options.append('-DENGINE_MAP_INTERFACE_SCOPE=OFF')
        run('cmake', '-S', ROOT / 'ios/graphics', '-B', graphics_build,
            '-DCMAKE_SYSTEM_NAME=iOS', '-DCMAKE_OSX_SYSROOT=' + sdk_path,
            '-DCMAKE_OSX_ARCHITECTURES=arm64', '-DCMAKE_OSX_DEPLOYMENT_TARGET=' + args.min_version,
            '-DCMAKE_BUILD_TYPE=Debug', *angle_options)
        run('cmake', '--build', graphics_build, '--parallel', '8', '--target', executable)
        if args.game_modules:
            run('cmake', '--build', graphics_build, '--parallel', '8', '--target', 'PortalGameModules')
        if args.world_loader_check:
            run('cmake', '--build', graphics_build, '--parallel', '4', '--target', 'EngineWorldLoader')
            run(sys.executable, ROOT / 'scripts/ios-world-loader-check.py',
                '--build-dir', build, '--target', args.target)
    else:
        run(sys.executable, 'waf', 'configure', '--ios-target=' + args.target,
            '--ios-min-version=' + args.min_version, '-o', build)
        run(sys.executable, 'waf', 'build')
    app = build / (executable + '.app')
    if app.exists():
        shutil.rmtree(app)
    app.mkdir(parents=True)
    libraries = []
    if args.graphics or args.angle:
        shutil.copy2(graphics_build / (executable + '.app') / executable, app / executable)
        if args.togles:
            shutil.copytree(ROOT / 'ios/fixtures', app / 'probe-assets')
            run(sys.executable, ROOT / 'scripts/ios-material-fixtures.py', app / 'probe-assets')
            run(sys.executable, ROOT / 'scripts/ios-bsp-fixtures.py', app / 'probe-assets')
            if args.shader_cache:
                run(sys.executable, ROOT / 'scripts/ios-compile-shaders.py',
                    '--output', args.shader_cache, '--stage', app / 'probe-assets')
            # Original VPK v2 fixture: three preload bytes plus embedded data.
            # Layout follows vpklib/packedstore_internal.h and packedstore.cpp.
            material = (ROOT / 'ios/fixtures/materials/ios/probe.vmt').read_bytes()
            entry = struct.pack('<IHHIIH', zlib.crc32(material), 3, 0x7fff,
                                0, len(material) - 3, 0xffff)
            tree = b'vmt\0materials/ios\0packed\0' + entry + material[:3] + b'\0\0\0'
            header = struct.pack('<7I', 0x55aa1234, 2, len(tree), len(material) - 3, 0, 0, 0)
            (app / 'probe-assets/probe_dir.vpk').write_bytes(header + tree + material[3:])
        if args.angle:
            for name in ['libEGL', 'libGLESv2']:
                framework = app / 'Frameworks' / (name + '.framework')
                framework.mkdir(parents=True)
                source = angle_root / (name + '.xcframework') / angle_slice / framework.name
                shutil.copy2(source / name, framework / name)
                # The release's simulator slices lack bundle metadata. Stage
                # complete framework bundles without changing the cached SDK.
                with (framework / 'Info.plist').open('wb') as stream:
                    plistlib.dump({'CFBundleExecutable': name, 'CFBundleName': name,
                        'CFBundleIdentifier': 'org.angle.' + name,
                        'CFBundlePackageType': 'FMWK', 'CFBundleVersion': '1',
                        'CFBundleShortVersionString': '1.0', 'MinimumOSVersion': '16.0',
                        'CFBundleSupportedPlatforms': ['iPhoneSimulator' if args.target == 'simulator' else 'iPhoneOS']}, stream)
                libraries.append(framework)
            shutil.copy2(angle_license, app / 'ANGLE-LICENSE.txt')
            (app / 'ANGLE-PROVENANCE.json').write_text(json.dumps({
                'source': 'https://github.com/google/angle', 'commit': COMMIT,
                'binary_release': 'https://github.com/jeremyfa/build-angle/releases/tag/' + TAG,
                'archive_sha256': ARCHIVE_SHA256}, indent=2) + '\n')
    else:
        frameworks = app / 'Frameworks'
        frameworks.mkdir()
        for relative in ['tier0/libtier0.dylib', 'ios/libfoundation_checks.dylib']:
            library = frameworks / Path(relative).name
            shutil.copy2(build / relative, library)
            run('xcrun', 'install_name_tool', '-id', '@rpath/' + library.name, library)
            libraries.append(library)
        run('xcrun', 'install_name_tool', '-change', build / 'tier0/libtier0.dylib',
            '@rpath/libtier0.dylib', frameworks / 'libfoundation_checks.dylib')
        run('xcrun', '--sdk', sdk, 'clang', '-target', triple, '-isysroot', sdk_path,
            '-fobjc-arc', '-g', '-framework', 'UIKit', '-framework', 'Foundation',
            ROOT / 'ios/Bootstrap.m', '-Wl,-rpath,@executable_path/Frameworks', '-o', app / executable)
    if args.togles:
        library = app / 'Frameworks/libtier0.dylib'
        shutil.copy2(build / 'tier0/libtier0.dylib', library)
        run('xcrun', 'install_name_tool', '-id', '@rpath/libtier0.dylib', library)
        run('xcrun', 'install_name_tool', '-change', build / 'tier0/libtier0.dylib',
            '@rpath/libtier0.dylib', app / executable)
        libraries.append(library)
        sdl = app / 'Frameworks/libSDL2.dylib'
        shutil.copy2(graphics_build / 'sdl2/libSDL2.dylib', sdl)
        libraries.append(sdl)
        module_names = ['libToGLESRuntime', 'libshaderapidx9', 'libmaterialsystem', 'stdshader_dx9', 'stdshader_dbg', 'libdatacache', 'libvphysics', 'libstudiorender', 'libEngineMapLinkCheck']
        if args.game_modules:
            module_names += ['libclient', 'libserver', 'libsoundemittersystem', 'libscenefilecache', 'libinputsystem']
        for name in module_names:
            module = app / 'Frameworks' / (name + '.dylib')
            shutil.copy2(graphics_build / module.name, module)
            run('xcrun', 'install_name_tool', '-id', '@rpath/' + module.name, module)
            run('xcrun', 'install_name_tool', '-change', build / 'tier0/libtier0.dylib',
                '@rpath/libtier0.dylib', module)
            libraries.append(module)
    with (app / 'Info.plist').open('wb') as stream:
        plistlib.dump({
            'CFBundleIdentifier': bundle_id, 'CFBundleExecutable': executable,
            'CFBundleName': executable, 'CFBundlePackageType': 'APPL',
            'CFBundleVersion': '1', 'CFBundleShortVersionString': '0.1',
            'MinimumOSVersion': args.min_version, 'UIDeviceFamily': [1, 2],
            'CFBundleSupportedPlatforms': ['iPhoneSimulator' if args.target == 'simulator' else 'iPhoneOS'],
            'UILaunchScreen': {},
            'UIApplicationSceneManifest': {'UIApplicationSupportsMultipleScenes': False},
            'UISupportedInterfaceOrientations': ['UIInterfaceOrientationPortrait', 'UIInterfaceOrientationLandscapeLeft', 'UIInterfaceOrientationLandscapeRight'],
        }, stream)
    for library in libraries:
        run('codesign', '--force', '--sign', '-', library)
    run('codesign', '--force', '--sign', '-', app)
    run('codesign', '--verify', '--deep', '--strict', app)
    print('Built:', app, flush=True)
    if not args.simulator:
        return
    devices = json.loads(run('xcrun', 'simctl', 'list', 'devices', 'available', '--json', capture=True))
    device = next((device for group in devices['devices'].values() for device in group
                   if device['udid'] == args.simulator), None)
    if device is None:
        parser.error('Simulator UDID is not available')
    if device['state'] != 'Booted':
        run('xcrun', 'simctl', 'boot', args.simulator)
    run('xcrun', 'simctl', 'bootstatus', args.simulator, '-b')
    run('xcrun', 'simctl', 'install', args.simulator, app)
    container = Path(run('xcrun', 'simctl', 'get_app_container', args.simulator, bundle_id, 'data', capture=True).strip())
    result_file = container / ('Documents/' + result_name + '.json')
    result_file.unlink(missing_ok=True)
    if args.togles:
        for stage in ['vertex', 'fragment']:
            (container / ('Documents/togles-' + stage + '.glsl')).unlink(missing_ok=True)
    launch_env=os.environ.copy()
    if args.game_modules:
        launch_env["SIMCTL_CHILD_SOURCE_IOS_GAME_MODULE_CHECK"]="1"
    if args.game_startup:
        launch_env["SIMCTL_CHILD_SOURCE_IOS_GAME_STARTUP"]=args.game_startup
    if args.portal_root:
        launch_env['SIMCTL_CHILD_SOURCE_IOS_GAME_ROOT']=str(args.portal_root)
        launch_env['SIMCTL_CHILD_SOURCE_IOS_WORLD_MAP']=args.world_map
    subprocess.run(['xcrun','simctl','launch','--terminate-running-process',args.simulator,bundle_id],
                   cwd=ROOT,env=launch_env,check=True)
    deadline = time.monotonic() + 30
    while not result_file.exists() and time.monotonic() < deadline:
        time.sleep(0.25)
    if not result_file.exists():
        # A crashed process is already gone; do not hide the original timeout
        # behind simctl's "no such process" exit code.
        subprocess.run(['xcrun', 'simctl', 'terminate', args.simulator, bundle_id], check=False)
        raise SystemExit('App did not write its probe result within 30 seconds')
    result = json.loads(result_file.read_text())
    print(json.dumps(result, indent=2))
    shutil.copy2(result_file, build / (result_name + '-result.json'))
    if args.portal_root:
        shutil.copy2(result_file, build / 'portal-map-load-result.json')
    if args.togles:
        for stage in ['vertex', 'fragment']:
            shader = container / ('Documents/togles-' + stage + '.glsl')
            if shader.exists():
                shutil.copy2(shader, build / shader.name)
    if not result['passed']:
        raise SystemExit('iOS smoke test failed')
    if args.portal_root and not result.get('world_map_loaded'):
        raise SystemExit('Requested Portal map did not load')
    if args.portal_root and not result.get('world_map_rendered'):
        raise SystemExit('Requested Portal map did not produce a verified world frame')


if __name__ == '__main__':
    main()
