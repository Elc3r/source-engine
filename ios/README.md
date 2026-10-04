# Source Engine on iOS

The iOS application hosts the original Source client/server and GameUI with
SDL, ANGLE's Metal backend, and the existing ToGLES renderer. Portal is currently
the only configured and verified game. Other games require their own client/server
build configuration and per-game application identity.

## Requirements

- macOS with Xcode, its command-line tools and the iOS SDK.
- Python 3, CMake, and the repository's initialized dependencies/submodules.
- Legally obtained Portal game data, including the `portal`, `hl2`, and `platform`
  directories. Game assets are not included in the application.
- A shader cache built with the repository's FXC compiler. On macOS FXC runs
  through Wine/CrossOver; neither component is shipped in the app.

The build downloads a pinned ANGLE release and records its source revision,
archive checksum and license in the application. See `scripts/ios_angle.py`.

## Build shaders

For the existing CrossOver configuration:

```sh
python3 scripts/ios-compile-shaders.py \
  --runner-json '["/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/bin/wine", "--bottle", "source-ios-shaders", "--no-gui"]'
```

Use `--fxc` and `--runner-json` for another FXC installation or Wine runner.
Shader staging verifies source and output checksums; rebuild a stale cache.

## Build the application

```sh
python3 scripts/build-ios.py --game portal --target device \
  --ipa ~/Downloads/Portal-iOS.ipa
```

The application is named **Portal** with bundle identifier
**org.sourceengine.portal**. The script creates an ad-hoc signed IPA; deployment
requires a compatible signing or container setup. Simulator and device builds
use separate `build-ios-*` directories. `--build-dir`, `--min-version`, and
`--shader-cache` override the defaults.

Builds use an original neutral application icon by default. To use the icon from
your own Portal installation, pass `--game-root` for either build target:

```sh
python3 scripts/build-ios.py --game portal --target device \
  --game-root /absolute/path/to/Portal-arm64 --ipa ~/Downloads/Portal-iOS.ipa
```

The build reads `portal/resource/game.icns` (or `game.ico`), generates opaque
iPhone/iPad icons in the ignored build directory, and includes them in the app.
If neither source file exists, it uses the neutral icon. Game artwork is not
stored in the repository, and game data is not copied into the app. Device
builds still read game data from Documents at runtime; the Mac's `--game-root`
path is used only to generate the icon. For an IPA with no imported game artwork,
omit `--game-root` when building for a device.

On a device, put the game data in `Documents/Portal-arm64`. The application exposes
its Documents directory through file sharing. Saves and settings use writable
paths separate from imported desktop saves. Changing from an older bootstrap
bundle identifier creates a new application/container: copy the game data to the
new Portal container or configure it through the deployment workflow. The root
shown by file sharing is already Documents; place `Portal-arm64` directly there,
without creating another Documents directory. Startup output is written to
`Documents/Portal.log` and replaced on each launch.

For a booted Simulator with existing local data:

```sh
python3 scripts/build-ios.py --game portal --target simulator \
  --game-root /absolute/path/to/Portal-arm64 --simulator SIMULATOR-UDID
```

The simulator build reads that directory directly. With `--simulator`, the build command installs and launches the application.

## Optional touch icons

Desktop Portal data does not include the Android port's touch icons. Without
them, the iOS port draws fallback controls and logs missing `vgui/touch/*`
materials. The original icons are available in
[`extras_dir.vpk` from nillerusr's Android launcher](https://github.com/nillerusr/srceng-android/blob/android-fixes/assets/extras_dir.vpk).

1. Download the archive and open it with a VPK extraction tool.
2. Extract only `materials/vgui/touch/`, keeping both `.vmt` and `.vtf` files.
3. Quit Portal and copy the extracted folder into the game data:

   ```text
   Documents/Portal-arm64/portal/materials/vgui/touch/
   ```

   The file-sharing root is already Documents. For the Simulator, use
   `<game-root>/portal/materials/vgui/touch/` in the directory passed to
   `--game-root`.
4. Launch Portal again. Rebuilding or reinstalling the IPA is not required.

For example, the destination should contain both `jump.vmt` and `jump.vtf`.
These files resolve the touch icon warnings; `TitleBarIcon`,
`TitleBarDisabledIcon`, and `vgui/servers/icon_replay*` belong to other UI
components and are not included in this touch icon pack.

## Runtime

- `ios/graphics/IOSApp.m`: UIKit lifecycle, SDL window/input and EGL surface.
- `EGLHost.cpp`, `MaterialRuntime.cpp`: GLM context ownership, presentation,
  drawable resizing and original client rendering.
- `EngineRuntime.cpp`, `EngineServices.cpp`, `FilesystemRuntime.cpp`,
  `MapServices.cpp`: original engine services, memory, filesystem and module wiring.
- `GameModules.cpp`, `GameStartup.cpp`, `PortalLevelStartup.cpp`: game module
  initialization, native startup menu, host ticks, audio and level transitions.
- `CMakeLists.txt`, `Physics.cmake`, `PortalGame.cmake`: original engine/game
  sources and dependencies. `scripts/ios-game-sources.py` reads the original VPCs.

Startup uses the original animated menu background and sound. The FPS/frame
counter remains enabled. Touch controls use the original client input path;
simulator keyboard input supports gameplay. Orientation changes resize the
native drawable, renderer and VGUI font/layout state. Quit waits for pending save
writes and exits through the UIKit host.

Game dialogs scale to the available display area in both orientations,
including chapter selection, saved games and confirmation prompts. The console
uses the current safe workspace when opened and after rotation, and shrinks
above the keyboard. Console text input and command output are supported;
the keyboard's Done button or closing the console dismisses text input.
The main menu, dialogs and startup/frame counter respect UIKit safe-area insets
to avoid the camera cutout and home indicator.

Video quality settings use the original material system and persist in
`videoconfig_ios.cfg` in the writable game directory. The application requests
the display's maximum refresh rate and opts into ProMotion on supported iPhones.
V-sync controls the EGL swap interval; enabling it does not raise the display's
refresh rate. Actual frame rate depends on rendering load and system scheduling.
The output follows the native drawable resolution. Window mode, aspect ratio
and multicore rendering are fixed by the UIKit/EGL host; MSAA is not currently
supported by the hosted ToGLES device.

## Compatibility

Requires iOS or iPadOS 16.0 or later on ARM64. Tested devices:

| Device | OS |
| --- | --- |
| iPhone X | iOS 16.7.14 |
| iPhone 15 Pro | iOS 27.0.1 |

Simulator and device builds are supported. iPhone X is the oldest tested device;
performance on older hardware and compatibility with other Source games have
not been established. Signing and container requirements depend on the deployment
setup, including LiveContainer.
