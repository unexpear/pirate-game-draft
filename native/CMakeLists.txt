cmake_minimum_required(VERSION 3.20)
project(sea_trial_native LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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
# SDL3 is vendored as a pinned submodule (extern/SDL). Build it static and
# minimal — no SDL tests/examples. No package manager involved.
set(SDL_TEST OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
add_subdirectory(extern/SDL EXCLUDE_FROM_ALL)

add_executable(sea_trial
    src/main.cpp
    src/ship_model.cpp
)
target_include_directories(sea_trial PRIVATE src)
target_link_libraries(sea_trial PRIVATE SDL3::SDL3-static)
