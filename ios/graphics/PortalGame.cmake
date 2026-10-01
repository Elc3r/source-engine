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
    add_library(Portal${upper}Module SHARED EXCLUDE_FROM_ALL ../../tier1/interface.cpp)
    target_compile_definitions(Portal${upper}Module PRIVATE
        $<TARGET_PROPERTY:Portal${upper},COMPILE_DEFINITIONS>)
    target_include_directories(Portal${upper}Module PRIVATE
        $<TARGET_PROPERTY:Portal${upper},INCLUDE_DIRECTORIES>)
    target_link_options(Portal${upper}Module PRIVATE
        "-Wl,-force_load,$<TARGET_FILE:Portal${upper}>"
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
