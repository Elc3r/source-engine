#!/usr/bin/env python3
"""Build and optionally install an iOS Source game application."""
import argparse
import json
import sys
from pathlib import Path
import plistlib
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
GAMES = {'portal': {'name': 'Portal', 'bundle_id': 'org.sourceengine.portal', 'data': 'Portal-arm64'}}


def run(*args, capture=False):
    return subprocess.run([str(arg) for arg in args], cwd=ROOT, check=True,
                          text=True, stdout=subprocess.PIPE if capture else None).stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', required=True, choices=GAMES)
    parser.add_argument('--target', choices=['simulator', 'device'], default='simulator')
    parser.add_argument('--min-version', default='16.0')
    parser.add_argument('--build-dir', type=Path)
    parser.add_argument('--shader-cache', type=Path, default=ROOT / 'build-ios-shaders/compiled')
    parser.add_argument('--game-root', type=Path, help='Existing game data for Simulator')
    parser.add_argument('--simulator', help='Install and launch on this Simulator UDID')
    parser.add_argument('--ipa', type=Path)
    args = parser.parse_args()
    if args.game_root and (args.target != 'simulator' or not args.simulator):
        parser.error('--game-root requires a Simulator launch')
    game = GAMES[args.game]
    executable = game['name']
    bundle_id = game['bundle_id']
    build = (args.build_dir or ROOT / ('build-ios-' + args.target)).resolve()
    graphics_build = build / 'togles'
    sdk = 'iphonesimulator' if args.target == 'simulator' else 'iphoneos'
    sdk_path = run('xcrun', '--sdk', sdk, '--show-sdk-path', capture=True).strip()
    from ios_angle import prepare, COMMIT, TAG, ARCHIVE_SHA256
    angle_root, angle_license = prepare(ROOT)
    angle_slice = 'ios-arm64_x86_64-simulator' if args.target == 'simulator' else 'ios-arm64'
    run(sys.executable, 'waf', 'configure', '--ios-target=' + args.target,
        '--ios-min-version=' + args.min_version, '-o', build)
    run(sys.executable, 'waf', 'build')
    run('cmake', '-S', ROOT / 'ios/graphics', '-B', graphics_build,
        '-DCMAKE_SYSTEM_NAME=iOS', '-DCMAKE_OSX_SYSROOT=' + sdk_path,
        '-DCMAKE_OSX_ARCHITECTURES=arm64', '-DCMAKE_OSX_DEPLOYMENT_TARGET=' + args.min_version,
        '-DCMAKE_BUILD_TYPE=Debug', '-DANGLE_ROOT=' + str(angle_root),
        '-DANGLE_SLICE=' + angle_slice, '-DENGINE_BUILD=' + str(build))
    run('cmake', '--build', graphics_build, '--parallel', '8', '--target', executable, 'PortalGameModules')
    app = build / (executable + '.app')
    if app.exists():
        shutil.rmtree(app)
    app.mkdir(parents=True)
    with (app / 'ios-launch.plist').open('wb') as stream:
        plistlib.dump({'SOURCE_IOS_GAME_STARTUP': 'menu',
                      'SOURCE_IOS_GAME_ROOT': str(args.game_root.resolve()) if args.game_root else '@documents/' + game['data'],
                      'SOURCE_IOS_WORLD_MAP': 'maps/testchmb_a_00.bsp'}, stream)
    libraries = []
    shutil.copy2(graphics_build / (executable + '.app') / executable, app / executable)
    run(sys.executable, ROOT / 'scripts/ios-compile-shaders.py',
        '--output', args.shader_cache, '--stage', app / 'engine-assets')
    # Stage the pinned ANGLE frameworks and original game modules.
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
    library = app / 'Frameworks/libtier0.dylib'
    shutil.copy2(build / 'tier0/libtier0.dylib', library)
    run('xcrun', 'install_name_tool', '-id', '@rpath/libtier0.dylib', library)
    run('xcrun', 'install_name_tool', '-change', build / 'tier0/libtier0.dylib',
        '@rpath/libtier0.dylib', app / executable)
    libraries.append(library)
    sdl = app / 'Frameworks/libSDL2.dylib'
    shutil.copy2(graphics_build / 'sdl2/libSDL2.dylib', sdl)
    libraries.append(sdl)
    module_names = ['libToGLESRuntime', 'libshaderapidx9', 'libmaterialsystem', 'stdshader_dx9', 'stdshader_dbg', 'libdatacache', 'libvphysics', 'libstudiorender', 'libEngineRuntime']
    module_names += ['libclient', 'libserver', 'libsoundemittersystem', 'libscenefilecache', 'libinputsystem', 'libvgui2', 'libvguimatsurface', 'libGameUI', 'libvaudio_minimp3']
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
            'CFBundleDisplayName': game['name'], 'CFBundleName': game['name'], 'CFBundlePackageType': 'APPL',
            'CFBundleVersion': '1', 'CFBundleShortVersionString': '0.1',
            'MinimumOSVersion': args.min_version, 'UIDeviceFamily': [1, 2],
            'CFBundleSupportedPlatforms': ['iPhoneSimulator' if args.target == 'simulator' else 'iPhoneOS'],
            'UILaunchScreen': {},
            'UIApplicationSceneManifest': {'UIApplicationSupportsMultipleScenes': False},
            'UIFileSharingEnabled': True, 'LSSupportsOpeningDocumentsInPlace': True,
            'UISupportedInterfaceOrientations': ['UIInterfaceOrientationPortrait', 'UIInterfaceOrientationLandscapeLeft', 'UIInterfaceOrientationLandscapeRight'],
        }, stream)
    for library in libraries:
        run('codesign', '--force', '--sign', '-', library)
    run('codesign', '--force', '--sign', '-', app)
    run('codesign', '--verify', '--deep', '--strict', app)
    print('Built:', app, flush=True)
    if args.ipa:
        import zipfile
        ipa = args.ipa.resolve()
        ipa.parent.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(ipa, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
            for path in sorted(app.rglob('*')):
                if path.is_file():
                    archive.write(path, 'Payload/' + str(path.relative_to(app.parent)))
        print('IPA:', ipa, flush=True)
    if args.simulator:
        run('xcrun', 'simctl', 'bootstatus', args.simulator, '-b')
        run('xcrun', 'simctl', 'install', args.simulator, app)
        run('xcrun', 'simctl', 'launch', '--terminate-running-process', args.simulator, bundle_id)


if __name__ == '__main__':
    main()
