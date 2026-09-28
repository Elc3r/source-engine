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
filesystem implementation is substituted. The real filesystem is now connected
as described below; persistent shader-cache writes are not exercised.

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
material system and shader API now build as separate dylibs, loaded by the engine's
`Sys_LoadModule`. `ToGLESRuntime` is shared so both modules use the application's
GLES backend, filesystem and cvar service. Each module retains its own tier1
interface/ConVar registry and tier2 bindings.

The probe loads `libmaterialsystem.dylib`, selects `libshaderapidx9.dylib`, and
connects the real `CMaterialSystem` / `IShaderUtil` through its application factory.
The material factory forwards the borrowed iOS context host to the shader API,
without a desktop SDL launcher. The application factory also forwards material
interfaces so tier2 receives the actual hardware configuration before Init.

The probe now builds and loads `stdshader_dx9.dylib` and `stdshader_dbg.dylib`
from the repository's shader sources and generated combination headers. The
bundle's Frameworks directory is an explicit `EXECUTABLE_PATH` search root.
The shader dictionary contains the standard implementations plus the probe-only
`IOSProbe` shader; the startup check requires `UnlitGeneric` and `DebugNormalMap`. Older DX6–8 modules are not packaged; the normal loader
attempts them and continues to DX9.

Two complete cycles exercise `CMaterialSystem::Init`, `ModInit`, `SetMode`,
`ModShutdown`, `Shutdown`, disconnect and module unload. Each uses a single-threaded
8×8 BGRA device without MSAA, including the standard fullscreen readback texture.
The real `IMatRenderContext` clears color/depth/stencil and reads all 64 pixels
back through the shader API and D3D surface path. Every RGBA byte must match
(37, 91, 163, 255). The check also verifies adapter caps/native drawable dimensions,
GL errors, retained application services and release of the current GLM context.
The original four-color window render still runs afterward.

Integration fixes include signed 32-bit HRESULT values on LP64 (with failure/success
checks), thread-local tracking of borrowed GLM contexts, client dimensions from
the device backbuffer, legal RGBA storage for XRGB uploads with opaque alpha
swizzle, and RGBA-to-BGRA conversion when locking a readback surface. Texture
workers start in Init and stop during Shutdown rather than starting at module load.

Both cycles and prior graphics/filesystem checks pass on the iOS 27 simulator.
The device app builds and passes ad-hoc signature verification, but has not run
on hardware. Shared material-system code also passes a macOS x86_64 syntax check;
this is not a complete desktop build.

### First material draws

`MaterialProbeShader.cpp` is an original, probe-only C++ shader registered in the
DX9 shader module. It uses a position/UV vertex format, texture sampler 0 and
minimal SM2 vertex/pixel shaders. Depth tests, blending and sRGB conversion are
disabled so byte-exact expected colors are independent of lighting/game content.
It does not replace any standard shader implementation.

The packager runs `scripts/ios-material-fixtures.py` to generate two original
4×4 VTF 7.2 textures and single-combo VCS v4 shaders (no compression or diffs).
The shader token instructions and binary layout are documented in the generator;
no game binaries or external shader compiler are needed. `ios/draw.vmt` uses an
opaque four-color texture and `ios/draw-alpha.vmt` uses alpha 255/192/128/64.

After SetMode, the test loads each VMT with `FindMaterial`, checks its shader and
texture dimensions, binds it through `IMatRenderContext`, fills a real dynamic
mesh with `CMeshBuilder`, and calls `IMesh::Draw`. The normal shader manager reads
the VCS files, ToGLES translates their D3D9 tokens, and ANGLE executes the result.
Each cycle draws opaque → alpha → opaque, checking all 64 RGBA pixels after each
draw. All six draws must pass, including quadrant orientation, channel ordering,
alpha output and rebinding the cached opaque material. These draw readbacks are
from the 8×8 device backbuffer. The ToGLES app now keeps a diagnostic material
on screen using the persistent loop described below.

Drawing exposed dyld coalescing inline shaderlib functions across modules that
have separate static draw state. Engine modules now hide their inline symbols.
It also exposed BGR/BGRA uploads being interpreted as RGB/RGBA. Static CPU-backed
uploads now convert channels into scratch storage without mutating source data,
use byte-aligned unpacking and restore the caller's unpack alignment. Twelve
additional GLM upload checks cover 24/32-bit colors with odd-width rows, full
replacement and a one-pixel subimage that must preserve its neighbors.

Simulator checks pass with no GL errors. This validates the original diagnostic
material, not the standard UnlitGeneric shader's GPU bytecode or all registered
C++ shaders. Full compiled standard shader libraries, lighting,
compressed VCS blocks, dynamic/PBO texture uploads, other texture formats,
queued rendering and device Reset still need coverage.

### Standard shader and matrix transforms

The unchanged production `screenspace_general_dx9` C++ shader is exercised by
`ios/standard.vmt` and `ios/standard-transform.vmt`. The generator supplies
hand-assembled SM2 equivalents of the two `screenspaceeffect_vs20` static
X360APPCHOOSER variants, plus an original texture-sampling pixel shader. These
selected fixtures are not a compiler-generated production shader library.

Each material-system cycle checks four draws: passthrough, model scale/translation
to the upper-left quadrant, noncommuting model/view/projection transforms to the
lower-right quadrant, and a return to identity. Every RGBA pixel of the 8×8
backbuffer is checked, including the untouched background. All eight standard
shader draws pass alongside the six diagnostic draws, exercising static combo
selection, MVP constant uploads and invalidation of cached transforms.

### Material-system presentation

After the standard shader draws, the probe calls `IMaterialSystem::SwapBuffers`
through the real shader API and D3D9 device to the hosted ANGLE window. Each of
two lifecycle cycles presents three frames: the standard material, a solid clear,
and a newly drawn diagnostic material (eight diagnostic draws in total).

A host callback checks the native default framebuffer immediately before each
real EGL swap. Four quadrant samples validate colors, vertical orientation and
scaling from 8×8 to the drawable size; it also checks default read/draw framebuffer
bindings, GL errors, successful swaps and exactly three presentations per cycle.
Alternating content catches stale frames, while drawing again after SwapBuffers
exercises restored render state. All six presentations pass in the simulator.
These six material frames are presented during startup. Device compilation and
signing are verified; physical-device execution remains untested.

### Persistent material loop and app lifecycle

The ToGLES app retains a material-system session after startup checks and draws
`ios/unlit` when the compiled cache is present (`ios/draw` otherwise) on every
SDL animation callback through BeginFrame/EndFrame and
SwapBuffers. Its own EGL context shares the window surface, but not mutable GL
state with the original ANGLE test context. Shutdown releases the material system
and modules before destroying this context and the host window.

Startup checks validate all 8×8 source pixels. Live frames validate source corners
and quadrant centers plus four native-window quadrant samples,
GL errors and exactly one successful swap. JSON results update every 120 frames;
a rendering failure stops the loop and replaces the previous PASS. This is a
correctness probe with synchronous readbacks, not a performance benchmark.
The standalone ANGLE target retains its original GLSL loop.

Scene deactivation pauses rendering and releases the current EGL context. The
next active frame rebinds the material context. Simulator checks cover thousands
of frames, Home/foreground transitions, and portrait/landscape changes between
1206×2622 and 2622×1206. ANGLE lazily updates its drawable; the host sync callback
accesses the default framebuffer before GLM queries its dimensions, preventing
the first rotated frame from using stale destination bounds. Read bindings are
restored after this probe-only synchronization readback.

The live D3D render target follows the drawable size using material-system
OverrideConfig → ChangeVideoMode → ResizeWindow → D3D9 Reset. The current frame
finishes on the old target; Present processes the pending reset, and the next
frame uses the new dimensions. The probe verifies the actual reported backbuffer
size after Reset, then tests all corners of the recreated color/depth/stencil
buffers. Startup fixtures retain their deterministic 8×8 targets.

`ios/depth.vmt` enables depth testing/writes in the original diagnostic shader.
At each new backbuffer size, five draws check cleared depth/stencil, stencil
replacement, rejection of farther geometry, rejection by stencil, and successful
nearer geometry with a matching stencil value. Native readbacks sample the bound
render FBO directly so each sample does not allocate/download a full-screen
staging surface. All read bindings are restored after inspection.

Per-pixel startup readbacks exposed a ToGLES read-only subrectangle lock bug:
ReadTexels returned the whole slice origin instead of the requested region.
The returned pointer now includes the region offset; exhaustive 8×8 per-pixel
checks cover both axes and preserve the existing RGBA/alpha checks. Hosted Reset
also permits a null desktop focus window in its debug assertion.

Simulator checks cover 1206×2622 → 2622×1206 → 1206×2622 and background/foreground
with the same material session. Context loss, failed Reset recovery, MSAA,
scene-disconnect/reconnect execution, physical-device lifecycle and frame pacing
still need coverage.

### Compiling the first HLSL shaders

`scripts/ios-compile-shaders.py` compiles the repository's unchanged
`screenspaceeffect_vs20.fxc` for both X360APPCHOOSER static variants, plus the
original diagnostic HLSL in `ios/shaders` and selected UnlitGeneric combinations.
The small probes use VCS v4 files with one dynamic
combo per static combo. Passing this cache to the packager replaces all four
hand-assembled shader files used by the current material probes. The standard
screenspace C++ shader, matrix tests and persistent diagnostic renderer then run
FXC-produced bytecode through the normal VCS loader and ToGLES translator.

On this Mac, the repository's `dx9sdk/utilities/fxc.exe` (D3DX9 compiler
5.04.00.2904) runs in a dedicated CrossOver bottle. Create the bottle once:

```sh
/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/bin/cxbottle --bottle source-ios-shaders --create --template win10_64
```

Compile and package without driving a simulator:

```sh
python3 scripts/ios-compile-shaders.py --runner-json '["/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/bin/wine", "--bottle", "source-ios-shaders", "--no-gui"]'
python3 scripts/build-ios-bootstrap.py --togles --shader-cache build-ios-shaders/compiled
python3 scripts/build-ios-bootstrap.py --togles --target device --shader-cache build-ios-shaders/compiled
```

The compiler accepts a different `--fxc` and a JSON command prefix for other Wine
installations (host files must be accessible through Z:). Windows hosts can omit
the runner. Generated caches are ignored by Git. The compiler and compatibility
runtime stay on the build host; neither is packaged into the app.

`manifest.json` records compiler, source/include and output SHA-256 hashes, shader
profiles and selected defines. Packaging validates current sources and every
output before staging the files and a `compiled-shaders.json` provenance copy.
Two independent builds produced identical manifests and bytecode hashes; corrupt
outputs and stale source hashes are rejected. Without `--shader-cache`, the
existing hand-assembled fixtures remain available. Runtime JSON explicitly
reports `compiled_shaders` so results from these two paths can be distinguished.

Compiled screenspace HLSL exposed a diagnostic shader overwriting register c0
with incomplete values. It now preserves the engine contract `(0,1,2,0.5)`;
FXC uses these constants to construct positions as specified in common_vs_fxc.h.

Simulator matrix/depth/stencil tests and native rendering pass with the compiled
cache. Device compilation, packaging and code signing also pass; physical-device
execution remains untested.

### Selected UnlitGeneric and VertexLitGeneric combinations

The FXC pipeline now also compiles the unchanged production
`vertexlit_and_unlit_generic_vs20.fxc` and `vertexlit_and_unlit_generic_ps2x.fxc`
(ps_2_b). Combo ranges and ordering come from profile-filtered STATIC/DYNAMIC
source annotations. Their index coefficients are checked against the engine's
existing C++ `.inc` headers, which are also included in cache provenance.

The selected vertex variants use static IDs 0 and 128 (static control flow off/on),
with dynamic ID 0 for unlit geometry. Lighting adds DYNAMIC_LIGHT=1 with zero,
one or two lights on the unrolled path, and NUM_LIGHTS=0 on the static-control-flow
path. The pixel shader includes DIFFUSELIGHTING=0/1, with dynamic ID 0.
All other feature switches remain zero. VCS v6 stores these as sparse static
records with a sentinel,
no aliases and uncompressed dynamic blocks. This preserves the full engine index
space without allocating an enormous dense dictionary. The compiler validates
FXC's ps_2_b bytecode version 2.1 separately from ps_2_0 version 2.0.

`ios/unlit.vmt` and `ios/unlit-tint.vmt` use the original UnlitGeneric C++ shader.
Each startup cycle draws plain → magenta color modulation → plain, checking all
64 pixels per draw. With this compiled cache, the persistent native-size loop
also uses UnlitGeneric for a rotating textured cube with a 60-degree perspective
camera, separate model/view/projection matrices and six independently textured
faces. Face submission order reverses every frame to exercise depth occlusion.
An independent CPU ray/box reference checks a 7×7 sample grid against exact
texture/background colors, excluding silhouette and texture boundaries. A
two-pixel neighborhood must agree with each sample, including on grazing faces.
Every frame must include at least three object and three background samples. The same
samples are checked again after presentation to the native window, including
portrait/landscape Reset. Existing depth/stencil tests remain active. The compact
on-screen status leaves the scene visible; full startup results remain in JSON.
No standard shader C++ or HLSL source was changed.

The live scene cycles through 1,200-frame phases of UnlitGeneric, VertexLitGeneric
with a uniform colored ambient cube, and VertexLitGeneric with ambient plus one
colored directional, point or spot light. Each mode includes a full cube revolution.
The CPU reference computes Lambert lighting from the face normal in world space and
accounts for native sRGB output or the backend's gamma fallback. Lit object RGB
allows two byte levels of rounding error; alpha, background and unlit samples
remain exact. Native-window presentation uses the same per-sample tolerance.
Point and spot modes use constant/linear/quadratic distance attenuation. Spot
mode adds an inner/outer cone with quadratic angular falloff. Since this shader
lights vertices, the independent CPU reference lights the four face corners and
interpolates the active triangle at the object-space ray hit, rather than
incorrectly comparing against per-pixel lighting. Neighborhood checks permit the
same two-level RGB tolerance for smooth gradients, while excluding texture and
silhouette edges. Both local light types reuse the existing one-light bytecode.

The cycle includes a two-light phase using a spot light and a differently
colored directional light. Every 120 frames it switches between both lights,
only the directional light in slot 1, and both lights with their slots swapped.
The CPU reference adds their independent contributions in linear space before
sRGB encoding. Both slots are explicitly updated every frame, including when
returning to ambient/unlit modes, to catch stale light state. NUM_LIGHTS=2 adds
vertex dynamic ID 130 on the unrolled path; pixel bytecode is unchanged.

This covers opaque, unskinned, uncompressed geometry with one base texture,
material color modulation, ambient, individual directional/point/spot lights,
and a spot-plus-directional pair.
It is not a complete standard shader library: vertex colors, detail textures,
environment maps, height/water fog, skinning, compressed vertices, more than
two simultaneous lights and additional lighting variants remain uncompiled/untested.
Missing combinations are not filled with unrelated bytecode. Compressed VCS
blocks and full-library build times also remain outside this milestone.

### Standard translucent materials

The live cycle includes an UnlitGeneric alpha-blending phase. An opaque
four-color base is followed by one or two mirrored layers of `ios/draw-alpha`.
The VTF contains alpha values 255, 192, 128 and 64. Every 120 frames the test
switches between one layer and two layers in either order. A CPU source-over
reference includes sRGB decode/blend/encode and intermediate 8-bit rounding
(or encoded blending on the backend's shader-gamma fallback). Sixteen samples
check RGB within two byte levels and preserved destination alpha exactly,
both in the backbuffer and after native-window presentation.

An opaque red center drawn behind the translucent layers must still appear,
proving those layers did not write depth. A subsequent, farther opaque blue
center must fail the depth test, verifying restoration of opaque depth writes.
This also checks that blending is disabled again for opaque draws. The existing
compiled UnlitGeneric bytecode is reused; the production shader selects the
blend/depth/alpha-write states from the new translucent VMT. Sorting arbitrary
transparent 3D geometry and other blend modes remain outside this check.

### Range fog

The cycle includes a range-fog phase using the production UnlitGeneric shader
and a material without `$nofog`. Five bands have projected depths .1, .3,
.5, .7 and .9 with fog start/end .3/.7 and identity projection. This deliberately
isolates the shader's projected-depth fog input from camera transformations.
The CPU reference squares the clamped range ramp, as required by the production
ps_2_b shader, and encodes the resulting linear color for the backbuffer.

Every 120 frames the phase switches between full blue fog, maximum density .5,
fog disabled, and full green fog. Fifteen samples check before/start/middle/end/
after distances, a `$nofog` center override, and a fogged draw after that override.
Within each fog phase, four material variants alternate every 30 frames:
unlit/lit opaque red and unlit/lit translucent blue (alpha 128/255). Lit materials
receive a colored ambient cube plus a directional light, producing a known .5
linear intensity in each channel. All bands draw over an opaque green background.
The CPU reference applies lighting, then fog, then source-over blending; the
post-override square exercises a second transparent layer too. This distinguishes
fogging the source from incorrectly fogging the already-composited background.
RGB tolerance is two byte levels (exact for the no-fog override); alpha is always
exact. Samples are also checked after native-window presentation. Lights and fog
are reset before returning to the other scenes. Existing compiled bytecode is
reused. Height/water fog remains untested.

### LightmappedGeneric atlas

The cycle includes two quads rendered by production `LightmappedGeneric`.
The material system allocates two 4×4 regions in one atlas, resolves their sort
IDs to a page and uploads original linear RGB light values. One region stays
unchanged while the other rotates four lighting patterns every 120 frames.
Updates alternate direct subrectangle locks and `Begin/EndUpdateLightmaps`
whole-page locks. Base UVs and horizontally mirrored lightmap UVs are independent.
Eight pixel references verify both regions, RGB channel order, gamma/overbright
conversion and native-window presentation, with two-byte RGB tolerance and exact
alpha. A material-system restore callback invalidates uploaded contents after
Device Reset, so both regions are repopulated on the next draw. The callback is
removed and atlas state reset when the retained material system shuts down.

On iOS, dynamic ToGLES textures now use the existing CPU-backed upload path.
The legacy PBO path ignored a subrectangle's origin and source row stride and
bypassed the D3D channel-order conversion. CPU backing preserves neighboring
atlas regions and uses the existing stride-aware GLES upload/conversion logic;
asynchronous PBO uploads remain a future optimization. Other platforms retain
their existing allocation path.

The shader cache adds static-zero LightmappedGeneric VS/PS combinations for
normal and fast paths (including the fast-path contrast switch), and the pixel
shader's TEXCOORD2/3 centroid mask. This is still a synthetic geometry/atlas test;
The full engine BSP loader, bumped lightmaps, HDR lightmaps and flashlight
shader combinations are not covered. The render-lump fixture below now supplies
the geometry and static lighting for this scene.

Validated on iOS 27 Simulator at 1206×2622 and 2622×1206, including rotation
within the lightmap phase, all four update patterns, background/resume and the
transition back to the perspective scene (1,680 frames, all checks PASS).
The device-SDK build and ad-hoc signature verification pass; physical-device
runtime behavior is still untested.

### BSP render-lump input

`scripts/ios-bsp-fixtures.py` packages an original `maps/ios-lightmap.bsp` (BSP 21).
It contains two planar world faces and the render lumps needed for their vertices,
signed surfedges, planes, texinfo, texture-name indirection and RGBExp32 light
samples. It is a synthetic render fixture, not a VBSP/VVIS-built playable map;
its nodes, visibility, collision and gameplay data are not supplied.

`BspGeometry.cpp` reads through the real `IFileSystem` GAME path and uses the
repository's `bspfile.h` records. It reconstructs ordered convex polygons,
calculates base UVs from texture-space vectors and source texture dimensions,
calculates luxels from the independent lightmap vectors/minima, and decodes
RGBExp32 with the engine's `TexLightToLinear`. The renderer triangulates those
polygons and uploads their light samples through the existing atlas path. Later
update phases permute the fixture's stored luxels; no vertices, UVs or light
samples are synthesized in the draw code. The independent pixel reference still
uses the known fixture colors/positions, including the negative light exponent.

The reader intentionally accepts only uncompressed BSP 21 world geometry with
planar convex faces, zero surface flags and a single static LDR light style.
Limits include a 16 MiB file, 256 world faces, 64 vertices per face, coordinates
within ±32768, 128×128 luxels per face and linear light values up to 4. Unsupported
features return an error rather than silently falling back. The reader validates
lump bounds/overlap, record versions/sizes, all followed indices, relative and
terminated material names, finite coordinates, closed edge chains, face planes,
convex winding and light-sample bounds. Partial parse results are never returned.

At fixture load, 15 mutated inputs exercise truncation, wrong magic/version,
overlap, compression, record size, signed-edge overflow, invalid vertex indices,
light offsets/extents/styles, unterminated names, NaN UVs/vertices and broken edge
chains. These must all fail before GPU allocation. Simulator validation passed
2,640 frames, all update phases, landscape-to-portrait reset during the BSP scene,
background/resume and subsequent perspective scenes. Device-SDK build and ad-hoc
signature verification also pass; physical-device runtime remains untested.

The full `engine/modelloader.cpp` is not linked by this probe. Integrating its
world/visibility/model lifecycle and a complete compiled map remains a separate
milestone; this reader only establishes the render-data boundary.

### Perspective BSP scene

The cycle now starts with `maps/ios-spatial.bsp`, generated alongside the flat
fixture. It places a larger rear face at z=.5 and a smaller inclined front face
at z=.4*x-.4, each with its own plane and texinfo. All vertices, UVs and lightmap
samples still come through the BSP reader. A 60-degree perspective camera moves
sideways over a 1,200-frame period. Face submission order reverses every frame
to expose incorrect depth occlusion. The flat BSP scene follows, rebuilding the
atlas and its restore callback when the fixture changes.

An analytic CPU reference intersects camera rays with the two known fixture
planes without reading the parsed geometry or reusing engine matrix operations.
It checks the nearest visible face, base-texture quadrant, independently mirrored
lightmap coordinates, quantized LDR upload, sRGB decode before bilinear lightmap
filtering, overbright and output encoding. Up to 49 pixel locations are checked
both offscreen and after native presentation. Edge/unstable footprints are
excluded, but every frame must retain at least three samples each of foreground,
rear surface and background. The RGB tolerance is two bytes; alpha is exact.
This adds perspective/occlusion coverage, not BSP visibility traversal, collision
or a playable map.

Validated on iOS 27 Simulator in both native orientations, including reset during
the perspective phase, background/resume and transition to the flat BSP fixture
(3,120 frames, all checks PASS). Device-SDK build and ad-hoc signature
verification pass; physical-device runtime remains untested.

### Filesystem and material data

The app now links the repository's actual stdio/base filesystem, asynchronous
I/O, file tracking, ZIP/VPK readers, KeyValues compiler and queued-loader sources,
plus the vstdlib job pool and random-number implementation. No filesystem shim
is used. The object target ensures its interface registrations are linked into
the app. The shared application factory supplies `IFileSystem`; its normal
`Connect` / `Init` path starts the I/O worker pool. `ToGLConnectLibraries` connects
the service to ToGLES and `g_pFullFileSystem`.

The service remains process-owned, like the linked cvar service. Two consecutive
probe cycles reuse it, rebuild search paths and clean up their temporary files.
This checks reuse, not filesystem shutdown/restart or iOS background suspension.
The search roots are explicit:

- `GAME`: original fixture data under the app bundle's `probe-assets` directory.
- `GAMEWRITE`: `Documents/engine-probe`, used explicitly for writable test files.
- `PACKED`: a small generated `probe_dir.vpk`, requested explicitly by its path ID.

The packager copies `ios/fixtures/materials/ios/probe.vmt` and constructs an
original VPK v2 containing another copy of that VMT, with three preload bytes
and the remaining bytes in its embedded data section. Nothing from installed
game content is bundled. `KeyValues::LoadFromFile` loads and checks both loose
and packed `UnlitGeneric` material descriptions through the engine filesystem.
This is material-data loading, not a rendered material; the referenced texture
and compiled material shaders are not supplied by this fixture.

Runtime checks also verify binary write/read, rename, seek, wildcard enumeration,
path-ID separation, missing-file handling and removal. Three asynchronous
requests per cycle check partial loose-file reads, a missing-file error and a
partial VPK read crossing into embedded data. Completion callbacks must run on
an engine worker thread, return the expected status/bytes and finish before their
request state is released. The overall simulator check still requires every
existing graphics test and all 32 D3D9 presentations to pass. Multipart VPKs,
archive signatures, ZIP loading and a complete game content tree remain untested.

These are synthetic fixtures, not full engine materials or game assets. The first diagnostic material draws are covered above. The plain `Direct3DCreate9` export remains the desktop factory;
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

1. Extend coverage to additional material features and map/world shaders,
   with reference checks in the perspective scene.
2. Connect the remaining engine modules, game-data paths and a single HL2 map.
3. Validate physical-device signing, graphics, audio, touch, save/load and lifecycle.
