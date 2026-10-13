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
- `src/session.hpp` / `.cpp` — Phase 8 host/join session: snapshot and damage
  codecs, interpolation, in-process peers, UDP sockets.
- `src/self_test.cpp` — model-spine and session checks; exit code 0 iff all
  pass, so it doubles as a CI gate.
- `CMakeLists.txt` — builds `sea_trial_selftest`; `ctest` registered.

## Build & run

CMake (any generator):

    cmake -S . -B build
    cmake --build build
    ctest --test-dir build --output-on-failure

Or a one-shot compile:

    g++ -std=c++17 -Wall -Wextra src/ship_model.cpp src/session.cpp src/self_test.cpp -o selftest
    ./selftest

Verified on **g++ (MSYS2 UCRT64)** and **MSVC 2022** (via CMake). The same binary also runs the Phase 8 session checks (serialize/apply, interpolation, in-process two-peer loopback, UDP loopback).

## Two-player sailing (Phase 8)

A local UDP host and one joining peer. Each player sails their own ship; the host owns the wave seed, the wind, and the session clock. The other hull is drawn in the chase view. The windowed app needs a display (the self-tests do not). From `native/build` after a full app build:

```text
# terminal 1 — host, waits for one peer
./sea_trial --host 27777

# terminal 2 — join (IPv4; localhost is accepted)
./sea_trial --join 127.0.0.1 27777
```

Port defaults to 27777 (`--host` alone, or `--join 127.0.0.1`). The joiner spawns ahead and to starboard of the host so both hulls start in view. Sail, add cargo (C/V), damage a plank (X), or repair (Z); the peer's panel shows their position and a pose-match line (recomputed heave / pitch / heel against the snapshot).

The AI patrol and gunnery are paused for the session. There is no host migration and no second client. Same Test Sloop on both sides — a custom build is not transferred. Snapshots are unauthenticated text datagrams for loopback and LAN, not the internet.

## The 11 checks

Base validates · cargo ↑ mass · remove-cargo restores mass · damage ↓ buoyancy ·
repair restores buoyancy · cargo ↓ float-margin · damage ↓ float-margin ·
no-helm fails validation · no-sail fails validation · water sampling
deterministic · serialize round-trips lossless.
