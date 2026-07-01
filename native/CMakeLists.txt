cmake_minimum_required(VERSION 3.20)
project(sea_trial_native LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20) # bgfx/bx headers use C++20 features (designated initializers)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# The self-test runner is dependency-free; the windowed app pulls in the
# SDL3/bgfx/imgui submodules. CI builds only the runner (no submodules, no
# graphics), so this switch lets it skip the whole app subtree.
option(SEA_TRIAL_BUILD_APP "Build the windowed app (SDL3 + bgfx + Dear ImGui). OFF = self-test runner only." ON)

# --- Milestone 0, part 1: self-test runner (pure logic, no dependencies) ---
add_executable(sea_trial_selftest
    src/ship_model.cpp
    src/self_test.cpp
)
target_include_directories(sea_trial_selftest PRIVATE src)

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

# Dear ImGui (stock v1.92.8 submodule) + a self-contained bgfx renderer.
# 3D ship rendered via bgfx's debugdraw (precompiled lit shaders — no shaderc
# pipeline needed yet). debugdraw.cpp only *includes* bgfx_utils.h/packrect.h
# (no calls), so we compile it directly with the right include dirs.
set(BGFX_SRC ${CMAKE_CURRENT_SOURCE_DIR}/extern/bgfx.cmake/bgfx)
set(BGFX_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/extern/bgfx.cmake)
add_executable(sea_trial
    src/main.cpp
    src/ship_model.cpp
    src/ship_view.cpp
    src/water_gpu.cpp
    src/imgui/imgui_bgfx.cpp
    ${BGFX_SRC}/examples/common/debugdraw/debugdraw.cpp
    extern/imgui/imgui.cpp
    extern/imgui/imgui_draw.cpp
    extern/imgui/imgui_tables.cpp
    extern/imgui/imgui_widgets.cpp
)
target_include_directories(sea_trial PRIVATE
    src
    src/imgui
    extern/imgui
    ${BGFX_SRC}/examples/common   # debugdraw/debugdraw.h, bgfx_utils.h, packrect.h
    ${BGFX_SRC}/3rdparty          # tinystl (pulled by bgfx_utils.h)
    ${BGFX_ROOT}/bimg/include     # bimg (pulled by bgfx_utils.h)
)
target_link_libraries(sea_trial PRIVATE SDL3::SDL3-static bgfx bx)

# Compile the water shaders (D3D11 / dxbc profile) into headers via shaderc.
# The app forces the D3D11 renderer so this single profile is enough.
include(${BGFX_ROOT}/cmake/bgfxToolUtils.cmake)
set(SHADER_OUT ${CMAKE_CURRENT_BINARY_DIR}/shaders)
bgfx_compile_shaders(
    TYPE VERTEX
    SHADERS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/vs_water.sc
    VARYING_DEF ${CMAKE_CURRENT_SOURCE_DIR}/shaders/varying.def.sc
    OUTPUT_DIR ${SHADER_OUT}
    OUT_FILES_VAR WATER_VS_OUT
    INCLUDE_DIRS ${BGFX_SRC}/src
    PROFILES s_5_0
    AS_HEADERS
)
bgfx_compile_shaders(
    TYPE FRAGMENT
    SHADERS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/fs_water.sc
    VARYING_DEF ${CMAKE_CURRENT_SOURCE_DIR}/shaders/varying.def.sc
    OUTPUT_DIR ${SHADER_OUT}
    OUT_FILES_VAR WATER_FS_OUT
    INCLUDE_DIRS ${BGFX_SRC}/src
    PROFILES s_5_0
    AS_HEADERS
)
target_sources(sea_trial PRIVATE ${WATER_VS_OUT} ${WATER_FS_OUT})
target_include_directories(sea_trial PRIVATE ${SHADER_OUT})
