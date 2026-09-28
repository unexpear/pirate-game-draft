cmake_minimum_required(VERSION 3.20)
project(sea_trial_native LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20) # bgfx/bx headers use C++20 features (designated initializers)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# The model self-test runner is dependency-free. The Jolt adapter test links
# the vendored Jolt submodule and does not need a display. The windowed app
# adds SDL3/bgfx/imgui on top of that. CI builds the two test runners, not the
# app (no graphics device).
option(SEA_TRIAL_BUILD_APP "Build the windowed app (SDL3 + bgfx + Dear ImGui). OFF = self-test runners only." ON)
option(SEA_TRIAL_WITH_JOLT "Build the Jolt physics adapter and its headless self-test." ON)

function(sea_trial_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra)
    endif()
endfunction()

# --- Model spine: pure logic, no external libraries ---
add_library(sea_model STATIC src/ship_model.cpp)
target_include_directories(sea_model PUBLIC src)
sea_trial_warnings(sea_model)

add_executable(sea_trial_selftest src/self_test.cpp)
target_link_libraries(sea_trial_selftest PRIVATE sea_model)
sea_trial_warnings(sea_trial_selftest)

enable_testing()
add_test(NAME model_spine COMMAND sea_trial_selftest)

# --- Jolt physics adapter (headless; no SDL/bgfx) ---
set(SEA_TRIAL_JOLT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/extern/JoltPhysics")
if(SEA_TRIAL_WITH_JOLT AND NOT EXISTS "${SEA_TRIAL_JOLT_DIR}/Build/CMakeLists.txt")
    message(FATAL_ERROR
        "Jolt Physics submodule is missing at native/extern/JoltPhysics.\n"
        "  git submodule update --init native/extern/JoltPhysics\n"
        "Or configure with -DSEA_TRIAL_WITH_JOLT=OFF to build the model self-test only.")
endif()

if(SEA_TRIAL_WITH_JOLT)
    # Embedded build: keep Jolt from clobbering our flags, skip samples/install,
    # and match the app's exceptions/RTTI. OBJECT_LAYER_BITS must stay the
    # integer 16 — Jolt declares it with option(), which would coerce 16 to ON.
    set(OVERRIDE_CXX_FLAGS OFF CACHE BOOL "" FORCE)
    set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
    set(FLOATING_POINT_EXCEPTIONS_ENABLED OFF CACHE BOOL "" FORCE)
    set(CPP_EXCEPTIONS_ENABLED ON CACHE BOOL "" FORCE)
    set(CPP_RTTI_ENABLED ON CACHE BOOL "" FORCE)
    set(ENABLE_ALL_WARNINGS OFF CACHE BOOL "" FORCE)
    set(ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    set(GENERATE_DEBUG_SYMBOLS OFF CACHE BOOL "" FORCE)
    set(PROFILER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "" FORCE)
    set(DEBUG_RENDERER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "" FORCE)
    set(USE_ASSERTS ON CACHE BOOL "" FORCE)
    set(OBJECT_LAYER_BITS 16 CACHE STRING "" FORCE)
    # Rigid bodies only. Skip the graphics compute backends so configure does
    # not search for Vulkan / DirectX / Metal.
    set(JPH_USE_VK OFF CACHE BOOL "" FORCE)
    set(JPH_USE_DX12 OFF CACHE BOOL "" FORCE)
    set(JPH_USE_MTL OFF CACHE BOOL "" FORCE)
    add_subdirectory(${SEA_TRIAL_JOLT_DIR}/Build jolt-build EXCLUDE_FROM_ALL)

    add_library(sea_physics STATIC src/physics_jolt.cpp)
    target_link_libraries(sea_physics PUBLIC sea_model)
    target_link_libraries(sea_physics PRIVATE Jolt)
    target_include_directories(sea_physics PUBLIC src)
    sea_trial_warnings(sea_physics)

    add_executable(sea_trial_physics_test src/physics_test.cpp)
    target_link_libraries(sea_trial_physics_test PRIVATE sea_physics)
    sea_trial_warnings(sea_trial_physics_test)
    add_test(NAME physics_jolt COMMAND sea_trial_physics_test)
endif()

# --- Milestone 0, part 2: the app window ---
if(NOT SEA_TRIAL_BUILD_APP)
    return() # self-test runners only; skip the graphics subtree
endif()

if(NOT SEA_TRIAL_WITH_JOLT)
    message(FATAL_ERROR
        "sea_trial drives the player hull through Jolt. "
        "Do not combine SEA_TRIAL_BUILD_APP=ON with SEA_TRIAL_WITH_JOLT=OFF.")
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
target_link_libraries(sea_trial PRIVATE SDL3::SDL3-static bgfx bx bimg sea_physics)

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
