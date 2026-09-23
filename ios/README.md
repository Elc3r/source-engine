# iOS foundation, Metal and ANGLE probes

These are the initial porting milestones, not a playable game. The implementation is
written in this repository; no patches or source files from other iOS forks are
imported.

## Build and run

Requires a Mac with Xcode, an iOS SDK and Python 3 (plus CMake for graphics). The initial target is ARM64
(including an Apple Silicon simulator), with deployment target iOS 15.0 for
foundation/Metal and iOS 16.0 for the pinned ANGLE binaries.

```sh
# Build the foundation libraries and an ad-hoc signed simulator application.
python3 scripts/build-ios-bootstrap.py

# List available simulator identifiers, then install, launch and check the app.
xcrun simctl list devices available
python3 scripts/build-ios-bootstrap.py --simulator <UDID>

# Compile against the device SDK as a separate check.
python3 scripts/build-ios-bootstrap.py --target device

# Build and run the independent SDL/Metal graphics probe.
python3 scripts/build-ios-bootstrap.py --graphics --simulator <UDID>
python3 scripts/build-ios-bootstrap.py --graphics --target device

# Download pinned ANGLE and verify GLES rendering through its Metal backend.
python3 scripts/build-ios-bootstrap.py --angle --simulator <UDID>
python3 scripts/build-ios-bootstrap.py --angle --target device

# Run the actual ToGLES shader translator and DXT decoder through ANGLE.
python3 scripts/build-ios-bootstrap.py --togles --simulator <UDID>
python3 scripts/build-ios-bootstrap.py --togles --target device
```

The device build is only ad-hoc signed. Installing on a physical iPhone still
requires a development identity and provisioning; this script does not configure
either. A simulator run does not validate device signing or GPU behavior.

Outputs are under `build-ios-simulator/` or `build-ios-device/`:

- `tier0/libtier0.dylib`, `tier1/libtier1.a`, `mathlib/libmathlib.a`
- `SourceBootstrap.app`
- `ios/libfoundation_checks.dylib`, containing runtime checks linked to the engine libraries
- `bootstrap-result.json` after a successful simulator launch/check
- `SourceGraphics.app` and `graphics-result.json` for the optional graphics probe
- `SourceANGLE.app` and `angle-result.json` for the optional GLES probe
- `SourceToGLES.app`, `togles-result.json`, `togles-vertex.glsl` and
  `togles-fragment.glsl` for the engine rendering-component probe

The app loads the bundled **real tier0** with `dlopen(RTLD_NOW)`, resolves `Msg`
and `Plat_FloatTime`, calls engine logging and checks that its timer advances.
It then loads the foundation-check module and runs the following off the UIKit
main thread:

- Allocation size, reallocation preserving data, and 16–256-byte alignment.
- Event timeout, auto-reset and manual-reset semantics.
- Four engine-created workers: 8,000 mutex-protected increments, isolated
  thread-local storage, concurrent allocations, and joining/releasing workers.
- Binary integer/float/string serialization through `CUtlBuffer` and string helpers.
- `MathLib_Init`, vector normalization and sine/cosine.

It displays PASS/FAIL and writes each check to `Documents/bootstrap.json` inside its sandbox.
The runner removes the previous result before launching, propagates failure and
times out if no new result appears. On timeout it attempts to terminate the app;
this also bounds a hung POSIX `ThreadJoin`, whose timeout argument the engine
currently does not implement.

## SDL/Metal probe

`--graphics` builds the existing SDL 2.0.17 sources with CMake in the target's
own output directory. It does not modify the thirdparty submodule. This probe
uses a separate app identifier, `org.sourceengine.bootstrap.graphics`.

UIKit owns the `UIScene` lifecycle. After SDL creates a window, the launcher
retrieves it through `SDL_GetWindowWMInfo` and attaches it to the connected
`UIWindowScene`. This is necessary on iOS 27: the original SDL app delegate
crashes at launch because it does not adopt the scene lifecycle. No SDK-version
spoofing or changes to SDL are used.

The probe creates an SDL Metal view and renders a green triangle on a blue
background using a small Metal shader. After four frames it copies the drawable
to CPU-accessible memory and checks center/background BGRA values. The result
includes the GPU name, logical window dimensions and physical drawable dimensions.
The readback makes `framebufferOnly = NO` necessary here; this is diagnostic work,
not a proposed per-frame operation for the game renderer.

Tap handling displays the SDL finger coordinates and event count. That is a manual
input check, not part of the automated GPU PASS. The scene pauses rendering when
inactive and releases its SDL/Metal resources on disconnect. Physical-device
input and background/foreground transitions still need runtime validation.

This verifies **SDL → Metal surface → GPU output**, not ToGLES or ANGLE. The
Source material/shader renderer has not yet been connected.

Verified with Xcode/iOS SDK 27 and the iPhone 18 Pro iOS 27 simulator:
eight foundation checks passed; Metal rendered at 1206×2622 pixels for a
402×874-point window and returned the expected pixel values. Both apps also
compile and package against the device SDK; neither has been run on a physical iPhone.

## ANGLE/GLES probe

`--angle` uses its own build directory and app identifier
`org.sourceengine.bootstrap.angle`. The path is **SDL window → CAMetalLayer →
EGL window surface → GLES 3.0 → ANGLE Metal backend**. SDL's native OpenGLES
support is disabled for this build to avoid mixing Apple's and ANGLE's GLES symbols.
The probe explicitly selects the Metal backend and checks `GL_RENDERER`.

It compiles and links GLSL ES 3.00 shaders, uploads a 2×2 RGBA texture, draws a
fullscreen triangle, and swaps buffers. On the fourth frame, `glReadPixels`
checks four quadrants before presentation: red/green in the bottom row,
blue/yellow in the top row. PASS requires the expected pixels, no GLES error,
and a successful EGL swap. The JSON includes the actual renderer/version,
surface dimensions, sample colors and frame count. Readback is only part of
this diagnostic; the app continues rendering after the check.

Verified on the iPhone 18 Pro iOS 27 simulator: GLES 3.0, ANGLE Metal renderer,
1206×2622 pixels, all four RGBA values exact, `gl_error=0`, successful swap.
The ARM64 device app also compiles, packages and passes ad-hoc signature
verification. Device execution and lifecycle transitions remain untested.
This establishes the GLES backend; it does not yet exercise Source shaders,
ToGLES, compressed textures, depth/stencil, or the material system.

### Dependency provenance

The helper downloads the independently published
[jeremyfa/build-angle release angle-97d33bc](https://github.com/jeremyfa/build-angle/releases/tag/angle-97d33bc),
built from Google ANGLE commit `97d33bc6e1356dcb2e63ea4ca7e6ebd2bc81a39d`.
It uses `angle-ios-universal.zip` with SHA-256
`dca3d8520a0dc5b2a334441d861c01d2d7524c7547c3a54d4286cccf0b57de99`.
The hash is checked on every build, and `commit.txt` is checked after extraction.
This is a third-party binary build, not an official Google binary release or
a patch imported from another Source port. Our EGL/SDL integration and shaders
are implemented here.

Downloads and extraction live in ignored `build-ios-deps/`; no submodule is
modified. Network access is needed on the first build. The pinned ANGLE BSD
license is downloaded with its own checksum and included in the app alongside
`ANGLE-PROVENANCE.json`. The release's simulator frameworks lack `Info.plist`,
so the packager supplies framework metadata in the staged app and signs each
framework before signing the app. The cached binaries are unchanged.
`--min-version` below 16.0 is rejected for ANGLE because its binaries declare
that deployment minimum. A later production integration should build ANGLE
from the pinned sources and collect its complete dependency notices.

## ToGLES shader and texture probe

`--togles` adds the repository's **actual** `togles/linuxwin/dx9asmtogl2.cpp`
and `decompress.c` to the ANGLE host. Waf builds real `tier0`/`tier1` first;
the app links tier1 statically and packages tier0 as a dynamic library. The app
identifier is `org.sourceengine.bootstrap.togles`, and its minimum iOS version
is 16.0, matching ANGLE. It runs these checks in the current GLES context:

- Translate original, minimal D3D9 shader-model-2 vertex/pixel bytecode fixtures
  with `D3DToGL`, compile their generated GLSL ES 3.00, and link a program.
- Decode 4×4 DXT1, DXT3 and DXT5 fixtures through the existing ToGLES CPU decoder.
  Compare every RGBA texel against independently specified expected colors and
  alpha values, including palette interpolation and DXT5's eight-selector mode.
- Upload each decoded texture, render with the **translated** shaders into an
  RGBA8 framebuffer, and compare all 16 GPU pixels with the expected data.
- Raise `alpha_ref` and check that the translated fragment shader discards the
  correct texels, leaving the clear color in their place.
- Draw indexed geometry with positive and negative base-vertex offsets through
  ToGLES's actual `gGL` dispatch table. Check every pixel and verify that vertex
  attribute pointers and the array-buffer binding are restored after the draw.
- Release and rebind the EGL context on the same thread, initialize the real
  `COpenGLEntryPoints`, and tear it down after the checks.
- Exercise the production DXT upload helper with 28 format/size combinations:
  RGB/RGBA DXT1, DXT3 and DXT5 at 1×1, 2×1, 2×2, 3×5, 5×3, 4×4 and 8×8.
  Read back the complete uploaded textures. Verify restoration of unpack
  alignment, row length, skip offsets and a deliberately bound unpack buffer.
  Reject truncated data, zero dimensions and unsupported formats.
- Restore the host bindings and run the original fullscreen ANGLE texture and
  presentation check. Overall PASS requires both groups to succeed.

The translator now takes the native alpha-test capability explicitly instead
of reading `gGL`. Its renderer callers pass their existing QCOM capability;
the iOS probe selects shader-based alpha testing. The translator includes only
its required type headers and uses tier0 diagnostics, so it can compile without
the GL window manager. The alpha-test condition checks the shader stage rather
than the address of the output stage flag. The individual DXT block algorithms
are unchanged; the image-level decode/upload path now crops partial edge blocks
and validates compressed byte counts.

`ios/ToGLESBackend.cpp` connects the entry-point lookup to `eglGetProcAddress`.
The iOS table requires GLES 3.0 and excludes unused desktop fixed-function
entries. GLES core capabilities are mapped explicitly instead of interpreting
desktop GL version numbers, and optional QCOM alpha-test lookup no longer
invalidates the core capability flag when the extension is absent. Teardown
also fixes an existing loop that used the GPU-vendor count to free a smaller
driver-string array.

The simulator does not advertise EXT/OES base-vertex drawing. The adapter
therefore adjusts per-vertex VBO attribute offsets for a draw, then restores
the VAO. It uses an advertised native extension when available. This fallback
is intended for translated D3D9 shaders, which do not use `gl_VertexID`; it is
not a general GLES base-vertex emulation. Its state queries prioritize correct
behavior; integration with GLM's state cache will be needed for performance.

The shared upload helper lives in `togles/linuxwin/texture_upload.cpp` and is
called by both `CGLMTex` and the probe. It no longer mistakes small compressed
mips for raw pixels when byte counts happen to match, and it decodes each block
into scratch storage before cropping to the actual image bounds. Its input is
CPU compressed data, not a PBO offset. Upload temporarily unbinds a PBO and sets
tightly packed unpack state, then restores all modified state. This also fixes
RGB rows whose byte length is not a multiple of the default alignment.

Verified on the iPhone 18 Pro iOS 27 simulator: shader translation, compilation,
linking, all three DXT CPU/GPU checks, alpha discard, signed base-vertex draws,
28 production uploads, EGL rebind and fullscreen presentation passed. The
device target compiles/packages but has not run on a physical iPhone.
The translator, entry-point loader and upload helper pass macOS syntax checks;
the object-integration changes in `glmgr.cpp`, `glmgrbasics.cpp`, `cglmtex.cpp`
and `glentrypoints.cpp` also pass with the macOS x86_64 definitions. The full
desktop ToGLES renderer was not rebuilt. The helper copies the generated shaders out of the app sandbox
for diagnosis after a simulator run, alongside the JSON result.

The probe also creates the real `GLMgr` / `GLMContext` and `CGLMTex` objects.
An optional `GLMContextHost` supplies a borrowed native context, binding and
presentation callbacks, drawable size and renderer capabilities. The normal
launcher path remains the default. The factory binds the host context before
GL state member constructors run. The probe uses a separate EGL pbuffer context,
then restores the window context and checks its original four-color image.
It compiles the existing GL manager, texture, FBO, program and query sources,
plus the real tier2 interface registry and KeyValues system; no launcher or
filesystem implementation is substituted. The filesystem remains unconnected,
and persistent shader-cache writes are not exercised.

Two GLM create/release/rebind/destroy cycles on one EGL context pass. Each cycle
locks and uploads DXT1, DXT3 and DXT5 textures at 4x4, 2x2 and 1x1 mip levels,
then replaces each mip with a different color. All **36 object-level uploads**
are verified by framebuffer readback of every pixel, with no GL errors. Texture
sampling initialization now checks extension support before setting border color
or anisotropy on iOS; framebuffer sRGB toggling requires `EXT_sRGB_write_control`.
Context teardown releases all scratch FBOs, preload shaders, sampler objects and
cached texture layouts. Callers must still delete their textures before the
context, as required by the existing ownership model.

Each GLM cycle also creates real `CGLMProgram` / `CGLMShaderPair` objects from
shared original D3D9 SM2 fixtures, plus vertex/index `CGLMBuffer` objects filled
through `Lock` / `Unlock`. Four indexed draws per cycle sample a `CGLMTex` DXT3
texture. GPU readback verifies the original colors, a second translated shader
with a tint constant, a constant change without relinking, and switching back
with alpha discard. All **8 draws** pass; fractional color conversion allows
at most one byte of rounding error per channel.

The same shader-pair object first rejects compiled stages with incompatible
varying types, then successfully relinks and renders. This caught an existing
error in link-failure diagnostics: the code queried a program with
`glGetShaderiv`, causing `GL_INVALID_OPERATION`. It now uses `glGetProgramiv`.
The regression failed with GL error `0x502` before the fix and passes after it.
The context's shader-pair cache is separately checked for insertion, reuse of
an existing pair, a second pair and explicit purge.

The probe also links the actual `dxabstract.cpp`. `ToGLESCreateD3D9` obtains a
borrowed `GLMContextHost` through the application's `CreateInterfaceFn` under
`ToGLESContextHost001`. It rejects missing or invalid hosts instead of falling
back to the desktop launcher. The resulting `IDirect3D9` exposes one windowed
adapter and creates devices through `CreateDevice`. The host must outlive the
adapter and its devices. The plain `Direct3DCreate9` entry point preserves the
desktop path. Adapter queries
report the GLES renderer/version, current drawable size and queried texture-size
limits; unknown PCI IDs and video-memory capacity remain zero. The remaining
shader/format capabilities still come from the existing ToGLES tables and are
not a complete hardware capability audit. Hosted MSAA is conservatively rejected
until tested. Adapter and device capability results are checked for agreement,
with invalid adapter/mode and unsupported MSAA requests rejected.

Four `IDirect3DDevice9` instances are created and released: two on an 8x8 EGL
pbuffer and two on the real SDL/ANGLE window surface. Each surface uses a separate
EGL context dedicated to GLM, with the original probe context restored afterward.
This does not support arbitrary GL state left by another renderer in the same
context. A rejected host is released safely before the valid cycles. Each device
creates its color and D24S8 depth/stencil targets, shaders
from D3D9 bytecode, vertex declaration, vertex/index buffers and a DXT3 texture.
Texture upload uses `GetSurfaceLevel` and surface `LockRect` / `UnlockRect`;
the existing texture-level `LockRect` / `UnlockRect` stubs remain unimplemented.

All **32 D3D9 indexed draws and presentations** pass. GPU readback checks every
8x8 render-target pixel and all four corners of the destination before swapping.
These use `BeginScene`, `DrawIndexedPrimitive`, `EndScene` and the real `FlushDrawStates`,
including shader constants, sampler and vertex attribute binding. Eight frames
per device check color, changed tint, alpha rejection, gamma conversion enabled
then disabled, depth rejection then acceptance, and a scissored corner pattern.
The latter verifies the vertical flip during presentation. The window path scales
8x8 to the 1206x2622 drawable. Framebuffer and scissor state are checked after
`Present`; resource/device teardown and restoration of the original EGL window
rendering also pass without GL errors.

For hosted presentation, GLM always blits to framebuffer zero; the host only
synchronizes the view and swaps EGL buffers. A failed host callback now returns
`D3DERR_DEVICELOST` from D3D9 `Present` instead of terminating the process.
Injected view-sync and swap failures, followed by successful retries, are checked
for every device. Both `gl_blitmode=0` and `gl_blitmode=1` are exercised, with
the original value restored. This checks error propagation, not real EGL
context-loss recovery.

The repeated frames exposed a stale vertex attribute cache: `BeginFrame` disabled
arrays but an unchanged declaration prevented their re-enabling. It now clears
the cached attributes. Half-intensity output also exposed unconditional GLES
encoding on sRGB render targets. On iOS without `EXT_sRGB_write_control`, GLM
uses linear targets and delivers the existing shader gamma-enable uniform.
This fallback approximates gamma with a power of 1/2.2; it does not implement
exact sRGB transfer or linear-space blending. Teardown now frees the device FBO
map and GLM debug-font buffers as well.

The ARM64 device target compiles, packages and passes ad-hoc signature validation;
it has not run on a physical iPhone. The changed `dxabstract.cpp` and `glmgr.cpp`
also pass macOS x86_64 syntax checks, not a complete desktop renderer build.

### Engine services and shader-API connection

The probe links the real `vstdlib/cvar.cpp`, obtains `ICvar` through
`VStdLib_GetICVarFactory`, connects tier1 and registers the linked engine ConVars.
The service is process-owned, matching the lifetime of the statically linked
ConVars; repeated scene initialization reuses it. The checks verify lookup,
integer/string changes, global callbacks with previous values, unchanged-value
suppression, clamping, deferred material-thread updates and explicit cleanup of
a temporary ConVar. This fixes the probe's previous crash when `SetValue`
dispatched global callbacks through a null `g_pCVar`.

`CShaderDeviceMgrDx8::Connect` now selects `ToGLESCreateD3D9` on iOS/ToGLES.
ToGLES retains ownership of its existing `gGL` definition, avoiding a duplicate
in the shader API. The same factory used by this production connection is
runtime-tested by the probe, including absent and invalid host cases. The actual
`shaderdevicedx8.cpp` is compiled by the `ShaderAPICompileCheck` dependency for
both iOS targets. This is a compile check, not a linked or running shader-device
manager: its base connection still requires a real filesystem and `IShaderUtil`
from the material system. The changed manager also passes the desktop macOS
syntax check using the normal ToGL path.

These are synthetic fixtures, not full engine materials or game assets. Full
shader-API startup and a material draw still need filesystem/material-system
integration. The plain `Direct3DCreate9` export remains the desktop factory;
iOS uses the explicit factory above. Device `Reset`, vsync timing and physical-
device presentation are not runtime-tested here. Native compressed uploads,
other DXT endpoint modes, uncompressed texture channel ordering, shader model 3,
stencil operations, border sampling and full sRGB semantics remain outside this
probe. EGL rebinding does not validate context loss, thread handoff or iOS lifecycle.

## Build boundaries

`--ios-target=simulator|device` selects the foundation-only Waf configuration.
It uses the existing module source lists and explicitly chooses the SDK and
compiler target. `IOS` is separate from `OSX`: the port enables shared Darwin
malloc/Mach/pthread paths without enabling desktop AppKit/Carbon/OpenGL.
UIKit owns the app lifecycle. SDL is only needed by the optional graphics probes;
it is not yet a foundation-build dependency.

For library-only work:

```sh
python3 waf configure --ios-target=simulator -o build-ios-simulator
python3 waf build
```

Waf uses a shared configuration lock in the repository root. The helper always
reconfigures before building. Before returning to desktop development, run your
normal desktop `waf configure` again. Do not run different Waf configurations
concurrently in the same checkout.

## Next milestones

1. Connect the real filesystem and material-system services needed by shader-API
   startup, then exercise a material draw and audit its capability assumptions.
2. Connect the remaining engine modules, game-data paths and a single HL2 map.
3. Validate physical-device signing, graphics, audio, touch, save/load and lifecycle.
