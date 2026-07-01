cmake_minimum_required(VERSION 3.20)
project(sea_trial_native LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Milestone 0 — the self-test runner. Pure logic, no external dependencies.
# (SDL3 + bgfx + Dear ImGui window comes next; see ../native-bootstrap-plan.md.)
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
