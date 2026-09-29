cmake_minimum_required(VERSION 3.20)
project(sea_trial_native LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20) # bgfx/bx headers use C++20 features (designated initializers)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# The self-test runner is dependency-free; the windowed app pulls in the
# SDL3/bgfx/imgui submodules. CI builds only the runner (no submodules, no
# graphics), so this switch lets it skip the whole app subtree.
option(SEA_TRIAL_BUILD_APP "Build the windowed app (SDL3 + bgfx + Dear ImGui). OFF = self-test runner only." ON)

# Steamworks is proprietary and is never vendored. OFF (the default) compiles
# steam_platform as local stubs so CI and a clean checkout need no SDK.
# ON links a user-supplied SDK — see steam.md. Search order when ON:
#   1. -DSTEAMWORKS_SDK=...   2. $STEAMWORKS_SDK   3. native/extern/steamworks/
option(SEA_TRIAL_WITH_STEAM "Link a user-supplied Steamworks SDK. OFF builds no-op local stubs." OFF)
set(STEAMWORKS_SDK "" CACHE PATH
    "Steamworks SDK root (folder with public/steam/steam_api.h, or the zip root that contains sdk/). Used only when SEA_TRIAL_WITH_STEAM=ON.")

set(SEA_TRIAL_STEAM_ROOT "")
set(SEA_TRIAL_STEAM_LIB "")
set(SEA_TRIAL_STEAM_SHARED "")

if(SEA_TRIAL_WITH_STEAM)
    set(_steam_cand "")
    set(_steam_source "")
    if(STEAMWORKS_SDK)
        set(_steam_cand "${STEAMWORKS_SDK}")
        set(_steam_source "-DSTEAMWORKS_SDK")
    elseif(DEFINED ENV{STEAMWORKS_SDK} AND NOT "$ENV{STEAMWORKS_SDK}" STREQUAL "")
        set(_steam_cand "$ENV{STEAMWORKS_SDK}")
        set(_steam_source "the STEAMWORKS_SDK environment variable")
    else()
        set(_steam_cand "${CMAKE_CURRENT_SOURCE_DIR}/extern/steamworks")
        set(_steam_source "native/extern/steamworks (gitignored drop-in)")
    endif()

    if(EXISTS "${_steam_cand}/public/steam/steam_api.h")
        set(SEA_TRIAL_STEAM_ROOT "${_steam_cand}")
    elseif(EXISTS "${_steam_cand}/sdk/public/steam/steam_api.h")
        set(SEA_TRIAL_STEAM_ROOT "${_steam_cand}/sdk")
    endif()

    if(NOT SEA_TRIAL_STEAM_ROOT)
        message(FATAL_ERROR
            "SEA_TRIAL_WITH_STEAM is ON, but the Steamworks SDK was not found.\n"
            "The SDK is proprietary and is not part of this repository.\n"
            "Download it from your Steamworks partner account:\n"
            "  https://partner.steamgames.com/doc/sdk\n"
            "Then point the build at the extracted SDK in one of these ways:\n"
            "  - cmake -DSTEAMWORKS_SDK=/path/to/sdk -DSEA_TRIAL_WITH_STEAM=ON\n"
            "  - export STEAMWORKS_SDK=/path/to/sdk, then reconfigure with -DSEA_TRIAL_WITH_STEAM=ON\n"
            "  - drop it at native/extern/steamworks/ (gitignored) and reconfigure\n"
            "The SDK root must contain public/steam/steam_api.h\n"
            "(or sdk/public/steam/steam_api.h if you pass the zip root).\n"
            "Looked at ${_steam_source}:\n  ${_steam_cand}\n"
            "Leave SEA_TRIAL_WITH_STEAM at its default OFF to build without Steam.")
    endif()

    if(WIN32)
        if(CMAKE_SIZEOF_VOID_P EQUAL 8)
            set(SEA_TRIAL_STEAM_LIB "${SEA_TRIAL_STEAM_ROOT}/redistributable_bin/win64/steam_api64.lib")
            set(SEA_TRIAL_STEAM_SHARED "${SEA_TRIAL_STEAM_ROOT}/redistributable_bin/win64/steam_api64.dll")
        else()
            set(SEA_TRIAL_STEAM_LIB "${SEA_TRIAL_STEAM_ROOT}/redistributable_bin/steam_api.lib")
            set(SEA_TRIAL_STEAM_SHARED "${SEA_TRIAL_STEAM_ROOT}/redistributable_bin/steam_api.dll")
        endif()
    elseif(APPLE)
        set(SEA_TRIAL_STEAM_LIB "${SEA_TRIAL_STEAM_ROOT}/redistributable_bin/osx/libsteam_api.dylib")
        set(SEA_TRIAL_STEAM_SHARED "${SEA_TRIAL_STEAM_LIB}")
    else()
        if(CMAKE_SIZEOF_VOID_P EQUAL 8)
            set(SEA_TRIAL_STEAM_LIB "${SEA_TRIAL_STEAM_ROOT}/redistributable_bin/linux64/libsteam_api.so")
        else()
            set(SEA_TRIAL_STEAM_LIB "${SEA_TRIAL_STEAM_ROOT}/redistributable_bin/linux32/libsteam_api.so")
        endif()
        set(SEA_TRIAL_STEAM_SHARED "${SEA_TRIAL_STEAM_LIB}")
    endif()

    if(NOT EXISTS "${SEA_TRIAL_STEAM_LIB}")
        message(FATAL_ERROR
            "Steamworks headers found at:\n  ${SEA_TRIAL_STEAM_ROOT}\n"
            "but this platform's Steam API import library is missing:\n  ${SEA_TRIAL_STEAM_LIB}\n"
            "Use the matching redistributable from the same SDK (win64 / linux64 / osx).")
    endif()
    if(NOT EXISTS "${SEA_TRIAL_STEAM_SHARED}")
        message(FATAL_ERROR
            "Steamworks headers found at:\n  ${SEA_TRIAL_STEAM_ROOT}\n"
            "but this platform's Steam API redistributable is missing:\n  ${SEA_TRIAL_STEAM_SHARED}\n"
            "The shared library is copied next to the executable at build time. Do not commit it.")
    endif()

    message(STATUS "Steamworks: ON (${SEA_TRIAL_STEAM_ROOT})")
else()
    message(STATUS "Steamworks: OFF (local stubs). -DSEA_TRIAL_WITH_STEAM=ON links a local SDK.")
endif()

add_library(steam_platform STATIC src/steam_platform.cpp)
target_include_directories(steam_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
if(MSVC)
    target_compile_options(steam_platform PRIVATE /W4 /permissive-)
else()
    target_compile_options(steam_platform PRIVATE -Wall -Wextra)
endif()
if(SEA_TRIAL_WITH_STEAM)
    target_compile_definitions(steam_platform PRIVATE SEA_TRIAL_WITH_STEAM=1)
    target_include_directories(steam_platform PRIVATE "${SEA_TRIAL_STEAM_ROOT}/public")
    # Full path to the import library / shared object. PUBLIC so executables
    # that link steam_platform also pull in the Steam API.
    target_link_libraries(steam_platform PUBLIC "${SEA_TRIAL_STEAM_LIB}")
endif()

# Link the façade and, when the SDK is on, copy its redistributable beside
# the binary. $ORIGIN / @executable_path let the loader find that copy.
function(sea_trial_attach_steam target)
    target_link_libraries(${target} PRIVATE steam_platform)
    if(NOT SEA_TRIAL_WITH_STEAM)
        return()
    endif()
    add_custom_command(
        TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${SEA_TRIAL_STEAM_SHARED}"
                "$<TARGET_FILE_DIR:${target}>"
        COMMENT "Copy Steam API redistributable next to ${target}"
        VERBATIM)
    if(APPLE)
        set_property(TARGET ${target} PROPERTY BUILD_RPATH "@executable_path")
        set_property(TARGET ${target} PROPERTY INSTALL_RPATH "@executable_path")
    elseif(UNIX)
        set_property(TARGET ${target} PROPERTY BUILD_RPATH "\$ORIGIN")
        set_property(TARGET ${target} PROPERTY INSTALL_RPATH "\$ORIGIN")
    endif()
endfunction()

# --- Milestone 0, part 1: self-test runner (no graphics; Steam stubs by default) ---
add_executable(sea_trial_selftest
    src/ship_model.cpp
    src/self_test.cpp
)
target_include_directories(sea_trial_selftest PRIVATE src)
sea_trial_attach_steam(sea_trial_selftest)

if(MSVC)
    target_compile_options(sea_trial_selftest PRIVATE /W4 /permissive-)
else()
    target_compile_options(sea_trial_selftest PRIVATE -Wall -Wextra)
endif()

enable_testing()
add_test(NAME model_spine COMMAND sea_trial_selftest)

# --- Milestone 0, part 2: the app window ---
if(NOT SEA_TRIAL_BUILD_APP)
    return() # self-test runner only; skip the submodule/graphics subtree
endif()

# SDL3 is vendored as a pinned submodule (extern/SDL). Build it static and
# minimal — no SDL tests/examples. No package manager involved.
set(SDL_TEST OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
add_subdirectory(extern/SDL EXCLUDE_FROM_ALL)

# bgfx (with bx + bimg) is vendored via the bgfx.cmake wrapper submodule.
# Tools ON: we need shaderc to compile the water shaders (bgfx_compile_shaders).
# Upstream bgfx builds with GENie/premake; the wrapper gives it CMake.
set(BGFX_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS ON CACHE BOOL "" FORCE)
set(BGFX_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(extern/bgfx.cmake EXCLUDE_FROM_ALL)

# The app: SDL3 + bgfx + Dear ImGui (self-contained bgfx renderer). Water and
# ship both render through the bgfx shader pipeline (shaders/ below).
set(BGFX_SRC ${CMAKE_CURRENT_SOURCE_DIR}/extern/bgfx.cmake/bgfx)
set(BGFX_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/extern/bgfx.cmake)
add_executable(sea_trial
    src/main.cpp
    src/ship_model.cpp
    src/ship_view.cpp
    src/water_gpu.cpp
    src/island_gpu.cpp
    src/sky_gpu.cpp
    src/shadow_gpu.cpp
    src/ship_mesh.cpp
    src/screenshot.cpp
    src/imgui/imgui_bgfx.cpp
    extern/imgui/imgui.cpp
    extern/imgui/imgui_draw.cpp
    extern/imgui/imgui_tables.cpp
    extern/imgui/imgui_widgets.cpp
)
target_include_directories(sea_trial PRIVATE
    src
    src/imgui
    extern/imgui
    extern/bgfx.cmake/bimg/include
)
target_link_libraries(sea_trial PRIVATE SDL3::SDL3-static bgfx bx bimg)
sea_trial_attach_steam(sea_trial)

# Compile shaders (D3D11 / dxbc profile) into headers via shaderc. The app
# forces the D3D11 renderer so this single profile is enough.
include(${BGFX_ROOT}/cmake/bgfxToolUtils.cmake)
set(SHADER_OUT ${CMAKE_CURRENT_BINARY_DIR}/shaders)
set(SHADER_SRC ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(VARYING ${SHADER_SRC}/varying.def.sc)

function(sea_trial_shader type file outvar)
    bgfx_compile_shaders(
        TYPE ${type}
        SHADERS ${SHADER_SRC}/${file}
        VARYING_DEF ${VARYING}
        OUTPUT_DIR ${SHADER_OUT}
        OUT_FILES_VAR ${outvar}
        INCLUDE_DIRS ${BGFX_SRC}/src
        PROFILES s_5_0
        AS_HEADERS
    )
    set(${outvar} ${${outvar}} PARENT_SCOPE)
endfunction()

sea_trial_shader(VERTEX   vs_water.sc WATER_VS_OUT)
sea_trial_shader(FRAGMENT fs_water.sc WATER_FS_OUT)
sea_trial_shader(VERTEX   vs_mesh.sc  MESH_VS_OUT)
sea_trial_shader(FRAGMENT fs_mesh.sc  MESH_FS_OUT)
sea_trial_shader(VERTEX   vs_terrain.sc TERRAIN_VS_OUT)
sea_trial_shader(FRAGMENT fs_terrain.sc TERRAIN_FS_OUT)
sea_trial_shader(VERTEX   vs_sky.sc SKY_VS_OUT)
sea_trial_shader(FRAGMENT fs_sky.sc SKY_FS_OUT)
sea_trial_shader(VERTEX   vs_shadow.sc SHADOW_VS_OUT)
sea_trial_shader(FRAGMENT fs_shadow.sc SHADOW_FS_OUT)
target_sources(sea_trial PRIVATE ${WATER_VS_OUT} ${WATER_FS_OUT} ${MESH_VS_OUT} ${MESH_FS_OUT}
               ${TERRAIN_VS_OUT} ${TERRAIN_FS_OUT} ${SKY_VS_OUT} ${SKY_FS_OUT}
               ${SHADOW_VS_OUT} ${SHADOW_FS_OUT})
target_include_directories(sea_trial PRIVATE ${SHADER_OUT})
