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
    add_library(ToGLESRuntime SHARED ToGLESRuntime.cpp EngineServices.cpp FilesystemChecks.cpp MaterialChecks.cpp ../ToGLESBackend.cpp
        ../../togles/linuxwin/glentrypoints.cpp ../../togles/linuxwin/cglmbuffer.cpp
        ../../togles/linuxwin/texture_upload.cpp ToGLESObjectChecks.cpp ToGLESShaderChecks.cpp ToGLESD3DChecks.cpp
        ../../togles/linuxwin/dxabstract.cpp
        ../../togles/linuxwin/glmgr.cpp ../../togles/linuxwin/glmgrbasics.cpp
        ../../togles/linuxwin/cglmtex.cpp ../../togles/linuxwin/cglmfbo.cpp
        ../../togles/linuxwin/cglmprogram.cpp ../../togles/linuxwin/cglmquery.cpp
        ../../tier2/tier2.cpp ../../vstdlib/KeyValuesSystem.cpp ../../vstdlib/cvar.cpp)
    target_compile_features(ToGLESRuntime PRIVATE cxx_std_11)
    target_compile_definitions(ToGLESRuntime PRIVATE IOS=1 POSIX=1 _POSIX=1
        PLATFORM_POSIX=1 PLATFORM_64BITS=1 GNUC NDEBUG
        NO_HOOK_MALLOC NO_MEMOVERRIDE_NEW_DELETE TIER1_STATIC_LIB=1
        DX_TO_GL_ABSTRACTION=1 USE_SDL=1 TOGL_DLL_EXPORT=1 VSTDLIB_DLL_EXPORT=1 _DLL_EXT=.dylib)
    target_include_directories(ToGLESRuntime PRIVATE ../../public ../../public/tier0
        ../../public/tier1 ../../common ../../togles/linuxwin
        ../../thirdparty/SDL-src/include "${ANGLE_ROOT}/include")
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
    target_link_libraries(ToGLESRuntime PRIVATE EngineFilesystem ToGLESChecks
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
        set_target_properties(${target} PROPERTIES BUILD_WITH_INSTALL_RPATH TRUE
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
    add_executable(SourceToGLES ANGLEProbe.m)
    add_dependencies(SourceToGLES materialsystem shaderapidx9)
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
