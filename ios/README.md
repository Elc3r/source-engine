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
