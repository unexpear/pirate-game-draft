# Native — engineless C++ build

The production path for the pirate game: an engineless native C++ app assembled
from permissive libraries. See [../tech-stack.md](../tech-stack.md) and
[../native-bootstrap-plan.md](../native-bootstrap-plan.md).

## Status — Milestone 0 ✅ complete

- [x] Model spine ported to C++ + **model self-tests passing** (`sea_trial_selftest`)
- [x] Builds from a clean checkout, exits cleanly, `ctest`-wired
- [x] SDL3 window (`sea_trial`)
- [x] bgfx clears the screen (Direct3D 11)
- [x] Dear ImGui debug panel — the 11 self-tests + Test Sloop stats, live

The self-test runner is pure C++ with **no external dependencies**. The
`sea_trial` app adds SDL3, bgfx, and Dear ImGui, vendored as **pinned git
submodules** under `extern/` (no package manager — see
[../native-bootstrap-plan.md](../native-bootstrap-plan.md)):

| Dep | Pin | Notes |
|---|---|---|
| SDL3 | `release-3.4.10` | window + input; built static |
| bgfx (+ bx, bimg) | via `bgfx.cmake` wrapper | renderer; upstream is GENie/premake, wrapper gives CMake |
| Dear ImGui | `v1.92.8` | debug UI |
| Jolt Physics | `v5.6.0` | player-hull rigid body; MIT. See [Physics](#physics) |

The Dear ImGui → bgfx renderer is a small self-contained backend
(`src/imgui/imgui_bgfx.*`) — there's no official ImGui-bgfx backend, so it's
adapted from bgfx's own example backend (same imgui version) and decoupled
from bgfx's example framework. Uses bgfx's precompiled ImGui shaders (the two
vendored `*_ocornut_imgui.bin.h`), feeds ImGui input from SDL3 directly.

Next: ship + water rendering, then the full Build → Sail loop (see
[../vertical-slice.md](../vertical-slice.md)).

## Layout

- `src/ship_model.hpp` / `.cpp` — ship generation, weight-vs-buoyancy stats,
  validation, deterministic Gerstner sampling, serialize/deserialize. Ported
  faithfully from the Three.js spike so the numbers match.
- `src/physics_jolt.hpp` / `.cpp` — Jolt world, player hull body, island cylinder.
- `src/self_test.cpp` — model-spine checks; exit code 0 iff all pass.
- `src/physics_test.cpp` — headless Jolt adapter checks (no window).
- `CMakeLists.txt` — `sea_trial_selftest` (`model_spine`) and, when Jolt is
  present, `sea_trial_physics_test` (`physics_jolt`).

## Physics

Buoyancy is still the model spine. `sea::computeFloatPose` produces heave,
pitch, and heel from the Gerstner field and the float margin. The sail model
still produces heading and speed. `sea::PhysicsWorld` copies those into a
dynamic hull box each fixed substep (60 Hz) and lets Jolt integrate the body.
Gravity on the hull is off — the vertical axis chases the buoyancy target
instead of solving displaced volume. A static cylinder at the island shore
blocks the player box and replaces `keepOutsideCircle` for that hull only
(the box's bow grounds about half a hull-length outside the old point test).

Not simulated: character controller, soft sails and ropes, enemy rigid bodies,
sail thrust as a force. Wind heel is still a render offset on top of the
wave heel. Hull mass is sampled when the body is created; cargo and damage
still change ride height through the buoyancy target.

Init the submodule before configuring (the other extern deps are only needed
for the windowed app):

    git submodule update --init native/extern/JoltPhysics

Model tests alone, without Jolt:

    cmake -S . -B build -DSEA_TRIAL_BUILD_APP=OFF -DSEA_TRIAL_WITH_JOLT=OFF

## Build & run

From `native/`, after the Jolt submodule is initialized:

    cmake -S . -B build -DSEA_TRIAL_BUILD_APP=OFF
    cmake --build build
    ctest --test-dir build --output-on-failure

That runs `model_spine` and `physics_jolt`. CI does the same on Linux and
does not build the windowed app (no SDL/D3D11 device). Jolt itself is headless
and runs in that job. Drop `-DSEA_TRIAL_BUILD_APP=OFF` for the SDL/bgfx app;
that configure also needs the SDL, bgfx, and ImGui submodules
(`git submodule update --init --recursive`).

The model runner still compiles with nothing but a C++ compiler:

    g++ -std=c++17 -Wall -Wextra src/ship_model.cpp src/self_test.cpp -o selftest
    ./selftest

Verified 11/11 on **g++ (MSYS2 UCRT64)** and **MSVC 2022** (via CMake).

## The 11 checks

Base validates · cargo ↑ mass · remove-cargo restores mass · damage ↓ buoyancy ·
repair restores buoyancy · cargo ↓ float-margin · damage ↓ float-margin ·
no-helm fails validation · no-sail fails validation · water sampling
deterministic · serialize round-trips lossless.
