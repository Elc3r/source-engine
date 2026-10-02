# Compile the actual Portal DLL sources independently of the inspection adapter.
# Source archives feed strict dylib links; UIKit still owns process startup.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
execute_process(COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/ios-game-sources.py"
    --output "${CMAKE_CURRENT_BINARY_DIR}/PortalSources.cmake"
    COMMAND_ERROR_IS_FATAL ANY)
include("${CMAKE_CURRENT_BINARY_DIR}/PortalSources.cmake")
foreach(side IN ITEMS client server)
    string(TOUPPER "${side}" upper)
    add_library(Portal${upper} STATIC EXCLUDE_FROM_ALL ${PORTAL_${upper}_SOURCES})
    target_compile_features(Portal${upper} PRIVATE cxx_std_11)
    target_compile_options(Portal${upper} PRIVATE -fsigned-char)
    target_compile_definitions(Portal${upper} PRIVATE
        IOS=1 POSIX=1 _POSIX=1 PLATFORM_POSIX=1 PLATFORM_64BITS=1 GNUC NDEBUG
        NO_HOOK_MALLOC NO_MEMOVERRIDE_NEW_DELETE TIER1_STATIC_LIB=1
        DISABLE_STEAM=1 NO_STEAM=1 USE_SDL=1 _DLL_EXT=.dylib
        ${PORTAL_${upper}_DEFINES})
    target_include_directories(Portal${upper} PRIVATE
        ../../game/${side} ../../game/shared ../../common ../../public
        ../../public/tier0 ../../public/tier1 ../../vgui2/include
        ../../vgui2/controls ../../thirdparty/SDL-src/include
        ${PORTAL_${upper}_INCLUDES})
endforeach()
add_custom_target(PortalGameCompile DEPENDS PortalCLIENT PortalSERVER)

add_library(PortalSupport STATIC EXCLUDE_FROM_ALL ${PORTAL_SUPPORT_SOURCES})
target_compile_features(PortalSupport PRIVATE cxx_std_11)
target_compile_options(PortalSupport PRIVATE -fsigned-char)
target_compile_definitions(PortalSupport PRIVATE
    IOS=1 POSIX=1 _POSIX=1 PLATFORM_POSIX=1 PLATFORM_64BITS=1 GNUC NDEBUG
    NO_HOOK_MALLOC NO_MEMOVERRIDE_NEW_DELETE TIER1_STATIC_LIB=1
    DISABLE_STEAM=1 NO_STEAM=1 USE_SDL=1 _DLL_EXT=.dylib)
target_include_directories(PortalSupport PRIVATE
    ../../public ../../public/tier0 ../../public/tier1 ../../public/tier2
    ../../public/tier3 ../../common ../../game/shared ../../utils/common
    ../../thirdparty/SDL-src/include ../../vgui2/matsys_controls)
foreach(side IN ITEMS client server)
    string(TOUPPER "${side}" upper)
    # A force-loaded archive retains the real interface registry and entity
    # factories. Unresolved dependencies are errors; never dynamic lookup.
    add_library(Portal${upper}Module SHARED EXCLUDE_FROM_ALL PortalModule.cpp)
    target_compile_definitions(Portal${upper}Module PRIVATE
        $<TARGET_PROPERTY:Portal${upper},COMPILE_DEFINITIONS>)
    target_include_directories(Portal${upper}Module PRIVATE
        $<TARGET_PROPERTY:Portal${upper},INCLUDE_DIRECTORIES>)
    target_link_options(Portal${upper}Module PRIVATE
        "-Wl,-force_load,$<TARGET_FILE:Portal${upper}>"
        "-Wl,-force_load,${ENGINE_BUILD}/tier1/libtier1.a"
        # Keep module-local allocator/template code out of Mach-O weak-symbol
        # coalescing with the renderer and the other game DLL.
        "-Wl,-exported_symbol,_CreateInterface")
    target_link_libraries(Portal${upper}Module PRIVATE Portal${upper}
        PortalSupport EngineMapSupport EngineVGUIControls IOSImage EngineBZip2
        libcurl ToGLESRuntime ${IOS_SDL_TARGET} iconv
        "${ENGINE_BUILD}/mathlib/libmathlib.a"
        "${ENGINE_BUILD}/tier1/libtier1.a" "${ENGINE_BUILD}/tier0/libtier0.dylib")
    set_target_properties(Portal${upper}Module PROPERTIES OUTPUT_NAME "${side}"
        BUILD_WITH_INSTALL_RPATH TRUE INSTALL_RPATH "@loader_path" INSTALL_NAME_DIR "@rpath")
endforeach()
add_custom_target(PortalGameModules DEPENDS PortalCLIENTModule PortalSERVERModule)

add_library(soundemittersystem SHARED EXCLUDE_FROM_ALL
    ../../soundemittersystem/soundemittersystembase.cpp
    ../../game/shared/interval.cpp ../../public/SoundParametersInternal.cpp
    ../../tier1/interface.cpp)
add_library(scenefilecache SHARED EXCLUDE_FROM_ALL
    ../../scenefilecache/SceneFileCache.cpp ../../tier1/interface.cpp)
add_library(inputsystem SHARED EXCLUDE_FROM_ALL
    ../../inputsystem/inputsystem.cpp ../../inputsystem/joystick_sdl.cpp
    ../../inputsystem/touch_sdl.cpp ../../inputsystem/key_translation.cpp
    ../../inputsystem/steamcontroller.cpp ../../tier1/interface.cpp)
foreach(module IN ITEMS soundemittersystem scenefilecache inputsystem)
    target_compile_features(${module} PRIVATE cxx_std_11)
    target_compile_definitions(${module} PRIVATE
        $<TARGET_PROPERTY:PortalSupport,COMPILE_DEFINITIONS>)
    target_include_directories(${module} PRIVATE
        $<TARGET_PROPERTY:PortalSupport,INCLUDE_DIRECTORIES>)
    target_link_libraries(${module} PRIVATE ToGLESRuntime
        "${ENGINE_BUILD}/tier1/libtier1.a" "${ENGINE_BUILD}/mathlib/libmathlib.a"
        "${ENGINE_BUILD}/tier0/libtier0.dylib")
    target_link_options(${module} PRIVATE "-Wl,-exported_symbol,_CreateInterface")
    set_target_properties(${module} PROPERTIES
        BUILD_WITH_INSTALL_RPATH TRUE INSTALL_RPATH "@loader_path" INSTALL_NAME_DIR "@rpath")
endforeach()
target_compile_definitions(soundemittersystem PRIVATE SOUNDEMITTERSYSTEM_EXPORTS=1 SOUNDEMITTERSYSTEM_DLL=1)
target_link_libraries(inputsystem PRIVATE PortalSupport ${IOS_SDL_TARGET})
target_include_directories(inputsystem PRIVATE ../../inputsystem)
target_compile_definitions(inputsystem PRIVATE VERSION_SAFE_STEAM_API_INTERFACES=1)
add_dependencies(PortalGameModules soundemittersystem scenefilecache inputsystem)

# Reuse the repository's FreeType and original VGUI implementation.
set(FT_DISABLE_ZLIB OFF CACHE BOOL "" FORCE)
set(FT_REQUIRE_ZLIB ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
set(_ios_vgui_shared_libs "${BUILD_SHARED_LIBS}")
set(BUILD_SHARED_LIBS OFF)
add_subdirectory(../../thirdparty/freetype freetype EXCLUDE_FROM_ALL)
set(BUILD_SHARED_LIBS "${_ios_vgui_shared_libs}")
unset(_ios_vgui_shared_libs)
set_target_properties(freetype PROPERTIES POSITION_INDEPENDENT_CODE TRUE)
add_library(IOSVGUISurfaceLib STATIC EXCLUDE_FROM_ALL ${IOS_SURFACELIB_SOURCES} IOSFontLookup.cpp)
add_library(vgui2 SHARED EXCLUDE_FROM_ALL ${IOS_VGUI_SOURCES} ../../tier1/interface.cpp)
# The Waf list contains empty ASAN VPanel stubs. Use the real panel methods:
# the surface traverses panels allocated by IVGui across the module boundary.
list(FILTER IOS_MATSURFACE_SOURCES EXCLUDE REGEX "/asanstubs\\.cpp$")
add_library(vguimatsurface SHARED EXCLUDE_FROM_ALL ${IOS_MATSURFACE_SOURCES}
    ../../vgui2/src/VPanel.cpp ../../vgui2/src/vgui_internal.cpp ../../tier1/interface.cpp)
foreach(module IN ITEMS IOSVGUISurfaceLib vgui2 vguimatsurface)
    target_compile_features(${module} PRIVATE cxx_std_11)
    target_compile_options(${module} PRIVATE -fsigned-char)
    target_compile_definitions(${module} PRIVATE
        $<TARGET_PROPERTY:PortalSupport,COMPILE_DEFINITIONS>
        DONT_PROTECT_FILEIO_FUNCTIONS=1 DX_TO_GL_ABSTRACTION=1 TOGLES=1)
    target_include_directories(${module} PRIVATE
        $<TARGET_PROPERTY:PortalSupport,INCLUDE_DIRECTORIES>
        ../../vgui2/src ../../vgui2/vgui_surfacelib ../../vguimatsurface
        ../../thirdparty/freetype/include)
    target_link_libraries(${module} PRIVATE freetype)
endforeach()
foreach(module IN ITEMS vgui2 vguimatsurface)
    target_link_libraries(${module} PRIVATE PortalSupport EngineVGUIControls
        IOSVGUISurfaceLib IOSImage ToGLESRuntime ${IOS_SDL_TARGET}
        "${ENGINE_BUILD}/tier1/libtier1.a" "${ENGINE_BUILD}/mathlib/libmathlib.a"
        "${ENGINE_BUILD}/tier0/libtier0.dylib" "-framework CoreText" "-framework CoreFoundation")
    target_link_options(${module} PRIVATE "-Wl,-exported_symbol,_CreateInterface")
    set_target_properties(${module} PROPERTIES
        BUILD_WITH_INSTALL_RPATH TRUE INSTALL_RPATH "@loader_path" INSTALL_NAME_DIR "@rpath")
endforeach()
target_sources(vguimatsurface PRIVATE VGUIChecks.cpp)
target_link_options(vguimatsurface PRIVATE "-Wl,-exported_symbol,_SourceIOSCheckVGUI")
target_compile_definitions(vguimatsurface PRIVATE VGUIMATSURFACE_DLL_EXPORT=1 GAMEUI_EXPORTS=1)
add_dependencies(PortalGameModules vgui2 vguimatsurface)

# Build the repository JPEG codec without desktop utilities or tests.
set(BUILD_STATIC ON CACHE BOOL "" FORCE)
set(BUILD_EXECUTABLES OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(../../thirdparty/libjpeg jpeg EXCLUDE_FROM_ALL)
set_target_properties(jpeg PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

# Original GameUI supplies the engine menu, console and client panel hierarchy.
add_library(GameUI SHARED EXCLUDE_FROM_ALL ${IOS_GAMEUI_SOURCES} ../../tier1/interface.cpp)
target_compile_features(GameUI PRIVATE cxx_std_11)
target_compile_options(GameUI PRIVATE -fsigned-char)
target_compile_definitions(GameUI PRIVATE
    $<TARGET_PROPERTY:PortalSupport,COMPILE_DEFINITIONS>
    GAMEUI_EXPORTS=1 VERSION_SAFE_STEAM_API_INTERFACES=1
    DX_TO_GL_ABSTRACTION=1 TOGLES=1)
target_include_directories(GameUI PRIVATE
    $<TARGET_PROPERTY:PortalSupport,INCLUDE_DIRECTORIES>
    ../../gameui ../../common/GameUI ../../thirdparty
    ../../thirdparty/libjpeg "${CMAKE_CURRENT_BINARY_DIR}/jpeg")
target_link_libraries(GameUI PRIVATE jpeg PortalSupport EngineVGUIControls IOSImage
    ToGLESRuntime ${IOS_SDL_TARGET}
    "${ENGINE_BUILD}/tier1/libtier1.a" "${ENGINE_BUILD}/mathlib/libmathlib.a"
    "${ENGINE_BUILD}/tier0/libtier0.dylib")
target_link_options(GameUI PRIVATE "-Wl,-exported_symbol,_CreateInterface")
set_target_properties(GameUI PROPERTIES
    BUILD_WITH_INSTALL_RPATH TRUE INSTALL_RPATH "@loader_path" INSTALL_NAME_DIR "@rpath")
add_dependencies(PortalGameModules GameUI)
