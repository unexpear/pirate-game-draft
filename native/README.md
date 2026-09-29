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
`sea_trial` app adds SDL3, bgfx, Dear ImGui, and miniaudio. SDL3, bgfx, and
Dear ImGui are **pinned git submodules** under `extern/`. miniaudio is a
**pinned in-tree header** (one public-domain file — a submodule would only add
a remote). No package manager — see
[../native-bootstrap-plan.md](../native-bootstrap-plan.md) and
[../THIRD-PARTY-LICENSES](../THIRD-PARTY-LICENSES):

| Dep | Pin | Notes |
|---|---|---|
| SDL3 | `release-3.4.10` | window + input; built static |
| bgfx (+ bx, bimg) | via `bgfx.cmake` wrapper | renderer; upstream is GENie/premake, wrapper gives CMake |
| Dear ImGui | `v1.92.8` | debug UI |
| miniaudio | `0.11.25` | playback; `extern/miniaudio/miniaudio.h` |

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
- `src/self_test.cpp` — runs the 11 model-spine checks; exit code 0 iff all
  pass, so it doubles as a CI gate.
- `src/audio.h` / `audio.cpp` — miniaudio init / update / one-shot / looping bed.
  Sounds are synthesized at startup (cannon crack + wind/ocean loop). No asset
  files.
- `src/audio_test.cpp` — headless smoke test (`audio_headless`). No sound card.
- `CMakeLists.txt` — builds `sea_trial_selftest` and `sea_trial_audio_test`;
  `ctest` registered. The windowed app links `sea_audio` when
  `SEA_TRIAL_BUILD_APP` is ON.

## Build & run

CMake (any generator):

    cmake -S . -B build
    cmake --build build
    ctest --test-dir build --output-on-failure

Or a one-shot compile:

    g++ -std=c++17 -Wall -Wextra src/ship_model.cpp src/self_test.cpp -o selftest
    ./selftest

Verified 11/11 on **g++ (MSYS2 UCRT64)** and **MSVC 2022** (via CMake).

`ctest` also runs `audio_headless` (procedural clips + a no-device mix). That
target does not need SDL, bgfx, or a sound card. CI builds it next to
`model_spine` with `SEA_TRIAL_BUILD_APP=OFF`.

## Hearing audio

Build and run the windowed app (`sea_trial`) on a machine with a playback device:

- **Space** fires your broadside and plays a short procedural cannon crack.
  Enemy broadsides use the same one-shot. Space does nothing inside the harbour
  safe zone or in Build mode.
- Debug panel **Audio → Beep** plays that crack anyway.
- A soft generated wind/ocean bed loops from startup. Uncheck **Wind / ocean bed**
  to stop it. **Mute** silences the bed and one-shots together; unmuting brings
  the bed back.

If no playback device can be opened — including when miniaudio's own null
backend is the only one that starts — stderr shows a line ending in
`no-device fallback (silent)` and the panel says **No playback device - silent
fallback**. The game keeps running; one-shots and the bed are mixed into
nowhere. A headless probe may also print ALSA or JACK errors before that line.
Init failure of even the no-device engine logs `audio: engine failed to start`
and playback calls do nothing.

## The 11 checks

Base validates · cargo ↑ mass · remove-cargo restores mass · damage ↓ buoyancy ·
repair restores buoyancy · cargo ↓ float-margin · damage ↓ float-margin ·
no-helm fails validation · no-sail fails validation · water sampling
deterministic · serialize round-trips lossless.
