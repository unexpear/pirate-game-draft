# Native — engineless C++ build

The production path for the pirate game: an engineless native C++ app assembled
from permissive libraries. See [../tech-stack.md](../tech-stack.md) and
[../native-bootstrap-plan.md](../native-bootstrap-plan.md).

## Status — Milestone 0

- [x] Model spine ported to C++ + **11 self-tests passing** (`sea_trial_selftest`)
- [x] Builds from a clean checkout, exits cleanly, `ctest`-wired
- [x] SDL3 window (`sea_trial`) — opens, runs the self-tests, clean exit
- [x] bgfx clears the screen (Direct3D 11; debug-text overlay)
- [ ] Dear ImGui debug panel

The self-test runner is pure C++17 with **no external dependencies**. The
`sea_trial` app adds SDL3 and bgfx, vendored as **pinned submodules** under
`extern/` (SDL @ release-3.4.10; bgfx via the bgfx.cmake wrapper, which
carries bgfx/bx/bimg). No package manager — see
[../native-bootstrap-plan.md](../native-bootstrap-plan.md). Dear ImGui is next.

## Layout

- `src/ship_model.hpp` / `.cpp` — ship generation, weight-vs-buoyancy stats,
  validation, deterministic Gerstner sampling, serialize/deserialize. Ported
  faithfully from the Three.js spike so the numbers match.
- `src/self_test.cpp` — runs the 11 model-spine checks; exit code 0 iff all
  pass, so it doubles as a CI gate.
- `CMakeLists.txt` — builds `sea_trial_selftest`; `ctest` registered.

## Build & run

CMake (any generator):

    cmake -S . -B build
    cmake --build build
    ctest --test-dir build --output-on-failure

Or a one-shot compile:

    g++ -std=c++17 -Wall -Wextra src/ship_model.cpp src/self_test.cpp -o selftest
    ./selftest

Verified 11/11 on **g++ (MSYS2 UCRT64)** and **MSVC 2022** (via CMake).

## The 11 checks

Base validates · cargo ↑ mass · remove-cargo restores mass · damage ↓ buoyancy ·
repair restores buoyancy · cargo ↓ float-margin · damage ↓ float-margin ·
no-helm fails validation · no-sail fails validation · water sampling
deterministic · serialize round-trips lossless.
