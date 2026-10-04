"""Generate build-local iOS icons from supplied game artwork or a neutral design."""
import json
import plistlib
import shutil
import subprocess
from pathlib import Path


def stage_icons(build, app, game, game_root, target, min_version):
    source = None
    if game_root:
        resource = game_root.resolve() / game / 'resource'
        source = next((resource / name for name in ('game.icns', 'game.ico')
                       if (resource / name).is_file()), None)
    catalog = build / 'app-icons' / 'Icons.xcassets'
    if catalog.parent.exists():
        shutil.rmtree(catalog.parent)
    iconset = catalog / 'AppIcon.appiconset'
    iconset.mkdir(parents=True)
    images = []
    for idiom, sizes, scales in [('iphone', [20, 29, 40, 60], [2, 3]),
                                  ('ipad', [20, 29, 40, 76], [1, 2]),
                                  ('ipad', [83.5], [2]),
                                  ('ios-marketing', [1024], [1])]:
        for size in sizes:
            for scale in scales:
                pixels = int(size * scale)
                images.append({'idiom': idiom, 'size': f'{size}x{size}',
                               'scale': f'{scale}x', 'filename': f'icon-{pixels}.png'})
    (iconset / 'Contents.json').write_text(json.dumps(
        {'images': images, 'info': {'version': 1, 'author': 'xcode'}}, indent=2) + '\n')
    pixels = sorted({int(float(image['size'].split('x')[0]) *
                         int(image['scale'][:-1])) for image in images})
    subprocess.run(['xcrun', 'swift', str(Path(__file__).with_suffix('.swift')),
                    str(iconset), str(source) if source else '-',
                    *map(str, pixels)], check=True)
    metadata = catalog.parent / 'icon-info.plist'
    subprocess.run(['xcrun', 'actool', str(catalog), '--compile', str(app),
                    '--platform', 'iphonesimulator' if target == 'simulator' else 'iphoneos',
                    '--minimum-deployment-target', min_version, '--app-icon', 'AppIcon',
                    '--target-device', 'iphone', '--target-device', 'ipad',
                    '--output-partial-info-plist', str(metadata)], check=True)
    print('Application icon:', source or 'neutral built-in design', flush=True)
    with metadata.open('rb') as stream:
        return plistlib.load(stream)
