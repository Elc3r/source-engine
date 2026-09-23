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
    add_library(ToGLESChecks STATIC ToGLESChecks.cpp
        ../../togles/linuxwin/dx9asmtogl2.cpp ../../togles/linuxwin/decompress.c)
    target_compile_features(ToGLESChecks PRIVATE cxx_std_11)
    target_compile_options(ToGLESChecks PRIVATE -fsigned-char)
    target_compile_definitions(ToGLESChecks PRIVATE IOS=1 POSIX=1 _POSIX=1
        PLATFORM_POSIX=1 PLATFORM_64BITS=1 GNUC NDEBUG
        NO_HOOK_MALLOC NO_MEMOVERRIDE_NEW_DELETE TIER1_STATIC_LIB=1)
    target_include_directories(ToGLESChecks PRIVATE ../../public ../../public/tier0
        ../../common ../../togles/linuxwin "${ANGLE_ROOT}/include")
    add_library(ToGLESRuntime STATIC ToGLESRuntime.cpp EngineServices.cpp ../ToGLESBackend.cpp
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
        DX_TO_GL_ABSTRACTION=1 USE_SDL=1 TOGL_DLL_EXPORT=1 VSTDLIB_DLL_EXPORT=1)
    target_include_directories(ToGLESRuntime PRIVATE ../../public ../../public/tier0
        ../../public/tier1 ../../common ../../togles/linuxwin
        ../../thirdparty/SDL-src/include "${ANGLE_ROOT}/include")
    # Compile the production consumer of the hosted factory on both iOS SDKs.
    # Full shader-API linking still needs filesystem and material-system services.
    add_library(ShaderAPICompileCheck OBJECT ../../materialsystem/shaderapidx9/shaderdevicedx8.cpp)
    target_compile_features(ShaderAPICompileCheck PRIVATE cxx_std_11)
    target_compile_definitions(ShaderAPICompileCheck PRIVATE
        $<TARGET_PROPERTY:ToGLESRuntime,COMPILE_DEFINITIONS>
        TOGLES=1 SHADERAPIDX9=1 SHADER_DLL_EXPORT=1)
    target_include_directories(ShaderAPICompileCheck PRIVATE
        $<TARGET_PROPERTY:ToGLESRuntime,INCLUDE_DIRECTORIES> ../../materialsystem)
    add_executable(SourceToGLES ANGLEProbe.m)
    add_dependencies(SourceToGLES ShaderAPICompileCheck)
    set_target_properties(SourceToGLES PROPERTIES MACOSX_BUNDLE TRUE
        BUILD_WITH_INSTALL_RPATH TRUE INSTALL_RPATH "@executable_path/Frameworks")
    target_compile_options(SourceToGLES PRIVATE -fobjc-arc)
    target_compile_definitions(SourceToGLES PRIVATE SOURCE_TOGLES_PROBE=1)
    target_include_directories(SourceToGLES PRIVATE "${ANGLE_ROOT}/include")
    target_link_options(SourceToGLES PRIVATE -Wl,-dead_strip)
    target_link_libraries(SourceToGLES PRIVATE ToGLESChecks ToGLESRuntime
        "${ENGINE_BUILD}/mathlib/libmathlib.a" "${ENGINE_BUILD}/tier1/libtier1.a" "${ENGINE_BUILD}/tier0/libtier0.dylib"
        SDL2-static libEGL libGLESv2 "-framework UIKit" "-framework Foundation"
        "-framework Metal" "-framework QuartzCore" iconv)
endif()
