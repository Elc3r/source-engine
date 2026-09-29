cmake_minimum_required(VERSION 3.20)
project(SourceIOSGraphicsProbe LANGUAGES C OBJC)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    message(FATAL_ERROR "This probe requires an iOS toolchain")
endif()

# Keep the vendored SDL sources untouched. Its 2021 CMake build predates modern
# CMake policy defaults and promotes mixed C declarations to errors in ObjC.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
set(HAVE_GCC_WERROR_DECLARATION_AFTER_STATEMENT OFF CACHE BOOL "" FORCE)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
set(VIDEO_METAL ON CACHE BOOL "" FORCE)
if(ANGLE_ROOT)
    # The probe uses ANGLE's GLES symbols, never Apple's OpenGLES framework.
    set(VIDEO_OPENGLES OFF CACHE BOOL "" FORCE)
endif()
add_subdirectory(../../thirdparty/SDL-src sdl2)

add_executable(SourceGraphics GraphicsProbe.m)
set_target_properties(SourceGraphics PROPERTIES MACOSX_BUNDLE TRUE)
target_compile_options(SourceGraphics PRIVATE -fobjc-arc)
target_link_libraries(SourceGraphics PRIVATE SDL2-static "-framework UIKit"
    "-framework Foundation" "-framework Metal" "-framework QuartzCore")

if(ANGLE_ROOT)
    add_executable(SourceANGLE ANGLEProbe.m)
    set_target_properties(SourceANGLE PROPERTIES MACOSX_BUNDLE TRUE
        BUILD_WITH_INSTALL_RPATH TRUE INSTALL_RPATH "@executable_path/Frameworks")
    target_compile_options(SourceANGLE PRIVATE -fobjc-arc)
    target_include_directories(SourceANGLE PRIVATE "${ANGLE_ROOT}/include")
    foreach(library IN ITEMS libEGL libGLESv2)
        add_library(${library} SHARED IMPORTED)
        set_target_properties(${library} PROPERTIES IMPORTED_LOCATION
            "${ANGLE_ROOT}/${library}.xcframework/${ANGLE_SLICE}/${library}.framework/${library}")
    endforeach()
    target_link_libraries(SourceANGLE PRIVATE SDL2-static libEGL libGLESv2
        "-framework UIKit" "-framework Foundation" "-framework Metal" "-framework QuartzCore")
endif()

if(ENGINE_BUILD AND ANGLE_ROOT)
    enable_language(CXX)
    add_library(ToGLESChecks OBJECT ToGLESChecks.cpp
        ../../togles/linuxwin/dx9asmtogl2.cpp ../../togles/linuxwin/decompress.c)
    target_compile_features(ToGLESChecks PRIVATE cxx_std_11)
    target_compile_options(ToGLESChecks PRIVATE -fsigned-char)
    target_compile_definitions(ToGLESChecks PRIVATE IOS=1 POSIX=1 _POSIX=1
        PLATFORM_POSIX=1 PLATFORM_64BITS=1 GNUC NDEBUG
        NO_HOOK_MALLOC NO_MEMOVERRIDE_NEW_DELETE TIER1_STATIC_LIB=1)
    target_include_directories(ToGLESChecks PRIVATE ../../public ../../public/tier0
        ../../common ../../togles/linuxwin "${ANGLE_ROOT}/include")
    add_library(ToGLESRuntime SHARED ToGLESRuntime.cpp EngineServices.cpp WorldMemoryChecks.cpp ../../engine/zone.cpp FilesystemChecks.cpp MaterialChecks.cpp SceneChecks.cpp BlendChecks.cpp FogChecks.cpp LightmapChecks.cpp BspGeometry.cpp ../ToGLESBackend.cpp
        ../../togles/linuxwin/glentrypoints.cpp ../../togles/linuxwin/cglmbuffer.cpp
        ../../togles/linuxwin/texture_upload.cpp ToGLESObjectChecks.cpp ToGLESShaderChecks.cpp ToGLESD3DChecks.cpp
        ../../togles/linuxwin/dxabstract.cpp
        ../../togles/linuxwin/glmgr.cpp ../../togles/linuxwin/glmgrbasics.cpp
        ../../togles/linuxwin/cglmtex.cpp ../../togles/linuxwin/cglmfbo.cpp
        ../../togles/linuxwin/cglmprogram.cpp ../../togles/linuxwin/cglmquery.cpp
        ../../tier2/tier2.cpp ../../tier2/meshutils.cpp ../../vstdlib/KeyValuesSystem.cpp ../../vstdlib/cvar.cpp)
    target_compile_features(ToGLESRuntime PRIVATE cxx_std_11)
    target_compile_definitions(ToGLESRuntime PRIVATE IOS=1 POSIX=1 _POSIX=1
        PLATFORM_POSIX=1 PLATFORM_64BITS=1 GNUC NDEBUG
        NO_HOOK_MALLOC NO_MEMOVERRIDE_NEW_DELETE TIER1_STATIC_LIB=1
        DX_TO_GL_ABSTRACTION=1 USE_SDL=1 TOGL_DLL_EXPORT=1 VSTDLIB_DLL_EXPORT=1 _DLL_EXT=.dylib)
    target_include_directories(ToGLESRuntime PRIVATE ../../public ../../public/tier0
        ../../public/tier1 ../../common ../../togles/linuxwin
        ../../thirdparty/SDL-src/include "${ANGLE_ROOT}/include")
    # Compile the actual engine world-loading units separately. This archive is
    # intentionally not linked into the probe until their engine services exist.
    add_library(EngineWorldLoader STATIC EXCLUDE_FROM_ALL
        ../../engine/modelloader.cpp ../../engine/gl_lightmap.cpp
        ../../engine/gl_rsurf.cpp ../../engine/cmodel_bsp.cpp)
    target_compile_features(EngineWorldLoader PRIVATE cxx_std_11)
    target_compile_options(EngineWorldLoader PRIVATE -fsigned-char)
    target_compile_definitions(EngineWorldLoader PRIVATE IOS=1 POSIX=1 _POSIX=1
        PLATFORM_POSIX=1 PLATFORM_64BITS=1 GNUC NDEBUG ENGINE_DLL=1
        NO_HOOK_MALLOC NO_MEMOVERRIDE_NEW_DELETE TIER1_STATIC_LIB=1
        DX_TO_GL_ABSTRACTION=1 TOGLES=1 USE_SDL=1 _DLL_EXT=.dylib)
    target_include_directories(EngineWorldLoader PRIVATE
        ../../engine ../../engine/audio ../../public ../../public/tier0
        ../../public/tier1 ../../public/engine/audio ../../common
        ../../thirdparty/SDL-src/include)
    add_library(EngineWorldServices OBJECT ../../engine/world_state.cpp WorldStateChecks.cpp
        ../../engine/surface_sort.cpp SurfaceSortChecks.cpp)
    target_compile_features(EngineWorldServices PRIVATE cxx_std_11)
    target_compile_options(EngineWorldServices PRIVATE -fsigned-char)
    target_compile_definitions(EngineWorldServices PRIVATE
        $<TARGET_PROPERTY:EngineWorldLoader,COMPILE_DEFINITIONS>)
    target_include_directories(EngineWorldServices PRIVATE
        $<TARGET_PROPERTY:EngineWorldLoader,INCLUDE_DIRECTORIES>)
    add_library(EngineFilesystem OBJECT
        ../../filesystem/basefilesystem.cpp ../../filesystem/packfile.cpp
        ../../filesystem/filetracker.cpp ../../filesystem/filesystem_async.cpp
        ../../filesystem/filesystem_stdio.cpp ../../filesystem/linux_support.cpp
        ../../filesystem/QueuedLoader.cpp ../../public/kevvaluescompiler.cpp
        ../../public/zip_utils.cpp ../../vpklib/packedstore.cpp
        ../../common/simplebitstring.cpp ../../vstdlib/jobthread.cpp ../../vstdlib/random.cpp)
    target_compile_features(EngineFilesystem PRIVATE cxx_std_11)
    target_compile_definitions(EngineFilesystem PRIVATE
        $<TARGET_PROPERTY:ToGLESRuntime,COMPILE_DEFINITIONS>
        FILESYSTEM_STDIO_EXPORTS=1 DONT_PROTECT_FILEIO_FUNCTIONS=1 SUPPORT_PACKED_STORE=1)
    target_include_directories(EngineFilesystem PRIVATE
        $<TARGET_PROPERTY:ToGLESRuntime,INCLUDE_DIRECTORIES> ../../filesystem)
    set_target_properties(ToGLESRuntime PROPERTIES BUILD_WITH_INSTALL_RPATH TRUE
        INSTALL_RPATH "@loader_path" INSTALL_NAME_DIR "@rpath")
    target_link_libraries(ToGLESRuntime PRIVATE EngineFilesystem EngineWorldServices ToGLESChecks
        "${ENGINE_BUILD}/mathlib/libmathlib.a" "${ENGINE_BUILD}/tier1/libtier1.a"
        "${ENGINE_BUILD}/tier0/libtier0.dylib" SDL2-static libEGL libGLESv2
        "-framework UIKit" "-framework Foundation" "-framework Metal" "-framework QuartzCore" iconv)

    function(engine_module target)
        target_compile_features(${target} PRIVATE cxx_std_11)
        target_compile_definitions(${target} PRIVATE
            $<TARGET_PROPERTY:ToGLESRuntime,COMPILE_DEFINITIONS> TOGLES=1)
        target_include_directories(${target} PRIVATE
            $<TARGET_PROPERTY:ToGLESRuntime,INCLUDE_DIRECTORIES>
            ../../materialsystem ../../thirdparty/stb ../..)
        # Each engine module owns its tier1 interface/cvar registry and tier2 bindings.
        target_sources(${target} PRIVATE ../../tier2/tier2.cpp ../../tier2/meshutils.cpp)
        target_link_options(${target} PRIVATE "-Wl,-force_load,${ENGINE_BUILD}/tier1/libtier1.a")
        target_link_libraries(${target} PRIVATE ToGLESRuntime "${ENGINE_BUILD}/mathlib/libmathlib.a"
            "${ENGINE_BUILD}/tier0/libtier0.dylib" iconv)
        # Shaderlib inline helpers reference module-local static draw state.
        # Do not let dyld coalesce them with another shader/material module.
        set_target_properties(${target} PROPERTIES VISIBILITY_INLINES_HIDDEN YES BUILD_WITH_INSTALL_RPATH TRUE
            INSTALL_RPATH "@loader_path" INSTALL_NAME_DIR "@rpath")
    endfunction()
    add_library(IOSImage STATIC
        ../../bitmap/ImageByteSwap.cpp
        ../../bitmap/colorconversion.cpp
        ../../bitmap/float_bm.cpp
        ../../bitmap/float_bm2.cpp
        ../../bitmap/float_bm3.cpp
        ../../bitmap/float_bm_bilateral_filter.cpp
        ../../bitmap/float_cube.cpp
        ../../bitmap/imageformat.cpp
        ../../bitmap/psd.cpp
        ../../bitmap/resample.cpp
        ../../bitmap/tgaloader.cpp
        ../../bitmap/tgawriter.cpp
        ../../bitmap/bitmap.cpp
        ../../vtf/convert_x360.cpp
        ../../vtf/vtf.cpp)
    target_compile_features(IOSImage PRIVATE cxx_std_11)
    target_compile_definitions(IOSImage PRIVATE $<TARGET_PROPERTY:ToGLESRuntime,COMPILE_DEFINITIONS>)
    target_include_directories(IOSImage PRIVATE $<TARGET_PROPERTY:ToGLESRuntime,INCLUDE_DIRECTORIES> ../../thirdparty/stb)
    add_library(shaderapidx9 SHARED
        ../../materialsystem/shaderapidx9/colorformatdx8.cpp
        ../../materialsystem/shaderapidx9/../../public/filesystem_helpers.cpp
        ../../materialsystem/shaderapidx9/hardwareconfig.cpp
        ../../materialsystem/shaderapidx9/meshbase.cpp
        ../../materialsystem/shaderapidx9/meshdx8.cpp
        ../../materialsystem/shaderapidx9/shaderapidx8.cpp
        ../../materialsystem/shaderapidx9/shaderdevicebase.cpp
        ../../materialsystem/shaderapidx9/shaderapibase.cpp
        ../../materialsystem/shaderapidx9/shaderdevicedx8.cpp
        ../../materialsystem/shaderapidx9/shadershadowdx8.cpp
        ../../materialsystem/shaderapidx9/texturedx8.cpp
        ../../materialsystem/shaderapidx9/TransitionTable.cpp
        ../../materialsystem/shaderapidx9/cvballoctracker.cpp
        ../../materialsystem/shaderapidx9/vertexdecl.cpp
        ../../materialsystem/shaderapidx9/vertexshaderdx8.cpp
        ../../materialsystem/shaderapidx9/winutils.cpp)
    engine_module(shaderapidx9)
    target_compile_definitions(shaderapidx9 PRIVATE SHADERAPIDX9=1 SHADER_DLL_EXPORT=1)
    target_link_libraries(shaderapidx9 PRIVATE IOSImage)
    add_library(materialsystem SHARED
        ../../materialsystem/CColorCorrection.cpp
        ../../materialsystem/cmaterial.cpp
        ../../materialsystem/cmaterial_queuefriendly.cpp
        ../../materialsystem/CMaterialSubRect.cpp
        ../../materialsystem/cmaterialvar.cpp
        ../../materialsystem/cmatnullrendercontext.cpp
        ../../materialsystem/colorspace.cpp
        ../../materialsystem/ctexture.cpp
        ../../materialsystem/../public/filesystem_helpers.cpp
        ../../materialsystem/imagepacker.cpp
        ../../materialsystem/mat_stub.cpp
        ../../materialsystem/materialsystem_global.cpp
        ../../materialsystem/morph.cpp
        ../../materialsystem/occlusionquerymgr.cpp
        ../../materialsystem/shadersystem.cpp
        ../../materialsystem/texturemanager.cpp
        ../../materialsystem/pch_materialsystem.cpp
        ../../materialsystem/cmaterialdict.cpp
        ../../materialsystem/cmaterialsystem.cpp
        ../../materialsystem/cmatlightmaps.cpp
        ../../materialsystem/cmatrendercontext.cpp
        ../../materialsystem/cmatqueuedrendercontext.cpp
        ../../materialsystem/ctexturecompositor.cpp)
    engine_module(materialsystem)
    target_compile_definitions(materialsystem PRIVATE DEFINE_MATERIALSYSTEM_INTERFACE=1 MATERIALSYSTEM_EXPORTS=1)
    target_sources(materialsystem PRIVATE ../../tier2/renderutils.cpp ../../public/materialsystem/MaterialSystemUtil.cpp
        ../../materialsystem/shaderlib/BaseShader.cpp
        ../../materialsystem/shaderlib/ShaderDLL.cpp ../../materialsystem/shaderlib/shaderlib_cvar.cpp)
    target_link_libraries(materialsystem PRIVATE IOSImage)
    add_library(stdshader_dx9 SHARED MaterialProbeShader.cpp
        ../../materialsystem/stdshaders/aftershock.cpp
        ../../materialsystem/stdshaders/aftershock_helper.cpp
        ../../materialsystem/stdshaders/AccumBuff4Sample.cpp
        ../../materialsystem/stdshaders/accumbuff5sample.cpp
        ../../materialsystem/stdshaders/BaseVSShader.cpp
        ../../materialsystem/stdshaders/bik_dx90.cpp
        ../../materialsystem/stdshaders/Bloom.cpp
        ../../materialsystem/stdshaders/BlurFilterX.cpp
        ../../materialsystem/stdshaders/BlurFilterY.cpp
        ../../materialsystem/stdshaders/BufferClearObeyStencil_dx9.cpp
        ../../materialsystem/stdshaders/cable_dx9.cpp
        ../../materialsystem/stdshaders/cloak.cpp
        ../../materialsystem/stdshaders/cloak_blended_pass_helper.cpp
        ../../materialsystem/stdshaders/cloak_dx9_helper.cpp
        ../../materialsystem/stdshaders/cloud_dx9.cpp
        ../../materialsystem/stdshaders/colorcorrection.cpp
        ../../materialsystem/stdshaders/compositor.cpp
        ../../materialsystem/stdshaders/core_dx9.cpp
        ../../materialsystem/stdshaders/color_projection.cpp
        ../../materialsystem/stdshaders/debugmrttexture.cpp
        ../../materialsystem/stdshaders/DebugTextureView.cpp
        ../../materialsystem/stdshaders/DecalBaseTimesLightmapAlphaBlendSelfIllum_dx9.cpp
        ../../materialsystem/stdshaders/DecalModulate_dx9.cpp
        ../../materialsystem/stdshaders/depthwrite.cpp
        ../../materialsystem/stdshaders/Downsample.cpp
        ../../materialsystem/stdshaders/downsample_nohdr.cpp
        ../../materialsystem/stdshaders/Engine_Post_dx9.cpp
        ../../materialsystem/stdshaders/emissive_scroll_blended_pass_helper.cpp
        ../../materialsystem/stdshaders/eye_refract.cpp
        ../../materialsystem/stdshaders/eye_refract_helper.cpp
        ../../materialsystem/stdshaders/eyes_dx8_dx9_helper.cpp
        ../../materialsystem/stdshaders/eyes_dx9.cpp
        ../../materialsystem/stdshaders/eyeglint_dx9.cpp
        ../../materialsystem/stdshaders/filmdust_dx8_dx9.cpp
        ../../materialsystem/stdshaders/filmgrain_dx8_dx9.cpp
        ../../materialsystem/stdshaders/flesh_interior_blended_pass_helper.cpp
        ../../materialsystem/stdshaders/floatcombine.cpp
        ../../materialsystem/stdshaders/floatcombine_autoexpose.cpp
        ../../materialsystem/stdshaders/floattoscreen.cpp
        ../../materialsystem/stdshaders/floattoscreen_vanilla.cpp
        ../../materialsystem/stdshaders/HDRCombineTo16Bit.cpp
        ../../materialsystem/stdshaders/HDRSelectRange.cpp
        ../../materialsystem/stdshaders/hsl_filmgrain_pass1.cpp
        ../../materialsystem/stdshaders/hsl_filmgrain_pass2.cpp
        ../../materialsystem/stdshaders/hsv.cpp
        ../../materialsystem/stdshaders/introscreenspaceeffect.cpp
        ../../materialsystem/stdshaders/lightmappedgeneric_dx9.cpp
        ../../materialsystem/stdshaders/lightmappedgeneric_dx9_helper.cpp
        ../../materialsystem/stdshaders/lightmappedreflective.cpp
        ../../materialsystem/stdshaders/modulate_dx9.cpp
        ../../materialsystem/stdshaders/MonitorScreen_dx9.cpp
        ../../materialsystem/stdshaders/motion_blur_dx9.cpp
        ../../materialsystem/stdshaders/occlusion_dx9.cpp
        ../../materialsystem/stdshaders/particlelitgeneric_dx9.cpp
        ../../materialsystem/stdshaders/particlelitgeneric_dx9_helper.cpp
        ../../materialsystem/stdshaders/particlesphere_dx9.cpp
        ../../materialsystem/stdshaders/pbr_dx9.cpp
        ../../materialsystem/stdshaders/portal.cpp
        ../../materialsystem/stdshaders/portalstaticoverlay.cpp
        ../../materialsystem/stdshaders/portal_refract.cpp
        ../../materialsystem/stdshaders/portal_refract_helper.cpp
        ../../materialsystem/stdshaders/pyro_vision.cpp
        ../../materialsystem/stdshaders/refract.cpp
        ../../materialsystem/stdshaders/refract_dx9_helper.cpp
        ../../materialsystem/stdshaders/sample4x4.cpp
        ../../materialsystem/stdshaders/sample4x4_blend.cpp
        ../../materialsystem/stdshaders/screenspace_general.cpp
        ../../materialsystem/stdshaders/sfm_blurfilterx.cpp
        ../../materialsystem/stdshaders/sfm_blurfiltery.cpp
        ../../materialsystem/stdshaders/sfm_downsample.cpp
        ../../materialsystem/stdshaders/sfm_integercombine.cpp
        ../../materialsystem/stdshaders/shadow.cpp
        ../../materialsystem/stdshaders/shadowbuild_dx9.cpp
        ../../materialsystem/stdshaders/shadowmodel_dx9.cpp
        ../../materialsystem/stdshaders/shatteredglass.cpp
        ../../materialsystem/stdshaders/showz.cpp
        ../../materialsystem/stdshaders/skin_dx9_helper.cpp
        ../../materialsystem/stdshaders/sky_dx9.cpp
        ../../materialsystem/stdshaders/sky_hdr_dx9.cpp
        ../../materialsystem/stdshaders/sprite_dx9.cpp
        ../../materialsystem/stdshaders/spritecard.cpp
        ../../materialsystem/stdshaders/teeth.cpp
        ../../materialsystem/stdshaders/TreeLeaf.cpp
        ../../materialsystem/stdshaders/unlitgeneric_dx9.cpp
        ../../materialsystem/stdshaders/unlittwotexture_dx9.cpp
        ../../materialsystem/stdshaders/vertexlitgeneric_dx9.cpp
        ../../materialsystem/stdshaders/vertexlitgeneric_dx9_helper.cpp
        ../../materialsystem/stdshaders/volume_clouds.cpp
        ../../materialsystem/stdshaders/volume_clouds_helper.cpp
        ../../materialsystem/stdshaders/vortwarp_dx9.cpp
        ../../materialsystem/stdshaders/vr_distort_hud.cpp
        ../../materialsystem/stdshaders/vr_distort_texture.cpp
        ../../materialsystem/stdshaders/warp.cpp
        ../../materialsystem/stdshaders/water.cpp
        ../../materialsystem/stdshaders/weapon_sheen_pass_helper.cpp
        ../../materialsystem/stdshaders/windowimposter_dx90.cpp
        ../../materialsystem/stdshaders/wireframe_dx9.cpp
        ../../materialsystem/stdshaders/worldtwotextureblend.cpp
        ../../materialsystem/stdshaders/worldvertexalpha.cpp
        ../../materialsystem/stdshaders/worldvertextransition.cpp
        ../../materialsystem/stdshaders/worldvertextransition_dx8_helper.cpp
        ../../materialsystem/stdshaders/writez_dx9.cpp
        ../../materialsystem/stdshaders/writestencil_dx9.cpp
        ../../materialsystem/stdshaders/eyeball.cpp
        ../../materialsystem/shaderlib/BaseShader.cpp
        ../../materialsystem/shaderlib/ShaderDLL.cpp
        ../../materialsystem/shaderlib/shaderlib_cvar.cpp)
    engine_module(stdshader_dx9)
    target_compile_definitions(stdshader_dx9 PRIVATE STDSHADER_DX9_DLL_EXPORT=1 FAST_MATERIALVAR_ACCESS=1)
    target_include_directories(stdshader_dx9 PRIVATE ../../materialsystem/stdshaders
        ../../materialsystem/stdshaders/fxctmp9 ../../materialsystem/stdshaders/vshtmp9)
    set_target_properties(stdshader_dx9 PROPERTIES PREFIX "")
    add_library(stdshader_dbg SHARED
        ../../materialsystem/stdshaders/BaseVSShader.cpp
        ../../materialsystem/stdshaders/debugdepth.cpp
        ../../materialsystem/stdshaders/DebugDrawEnvmapMask.cpp
        ../../materialsystem/stdshaders/debugluxel.cpp
        ../../materialsystem/stdshaders/debugnormalmap.cpp
        ../../materialsystem/stdshaders/debugtangentspace.cpp
        ../../materialsystem/stdshaders/fillrate.cpp
        ../../materialsystem/shaderlib/BaseShader.cpp
        ../../materialsystem/shaderlib/ShaderDLL.cpp
        ../../materialsystem/shaderlib/shaderlib_cvar.cpp)
    engine_module(stdshader_dbg)
    target_compile_definitions(stdshader_dbg PRIVATE STDSHADER_DBG_DLL_EXPORT=1 FAST_MATERIALVAR_ACCESS=1)
    target_include_directories(stdshader_dbg PRIVATE ../../materialsystem/stdshaders
        ../../materialsystem/stdshaders/fxctmp9 ../../materialsystem/stdshaders/vshtmp9)
    set_target_properties(stdshader_dbg PROPERTIES PREFIX "")
    add_executable(SourceToGLES ANGLEProbe.m)
    add_dependencies(SourceToGLES materialsystem shaderapidx9 stdshader_dx9 stdshader_dbg)
    set_target_properties(SourceToGLES PROPERTIES MACOSX_BUNDLE TRUE
        BUILD_WITH_INSTALL_RPATH TRUE INSTALL_RPATH "@executable_path/Frameworks")
    target_compile_options(SourceToGLES PRIVATE -fobjc-arc)
    target_compile_definitions(SourceToGLES PRIVATE SOURCE_TOGLES_PROBE=1)
    target_include_directories(SourceToGLES PRIVATE "${ANGLE_ROOT}/include")
    target_link_options(SourceToGLES PRIVATE -Wl,-dead_strip)
    target_link_libraries(SourceToGLES PRIVATE ToGLESRuntime
        "${ENGINE_BUILD}/mathlib/libmathlib.a" "${ENGINE_BUILD}/tier1/libtier1.a" "${ENGINE_BUILD}/tier0/libtier0.dylib"
        SDL2-static libEGL libGLESv2 "-framework UIKit" "-framework Foundation"
        "-framework Metal" "-framework QuartzCore" iconv)
endif()
