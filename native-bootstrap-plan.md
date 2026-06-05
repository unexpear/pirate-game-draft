# Native Bootstrap Plan

## Decision

The production path is an **engineless native C++ game using selected libraries** — not Unreal, Unity, Godot, or the Three.js browser spike. See [tech-stack.md](tech-stack.md) for the locked stack.

## Milestone 0 — Window + Tests + Debug Shell

**Goal:** prove the native app foundation boots cleanly before adding ships, water rendering, gameplay physics, or Steamworks.

### What opens first?

A native SDL3 window.

### What renders first?

A bgfx clear screen, then a Dear ImGui debug panel.

### What data loads first?

Hardcoded / default ship-test data only. No asset loader yet. No external ship files yet.

### What self-tests run first?

Native equivalents of the 11 model-spine tests from the [Three.js browser spike](spikes/threejs-sea-trial/):

- base ship validates
- cargo increases mass
- removing cargo restores mass
- damage lowers buoyancy
- repair restores buoyancy
- added cargo lowers float margin
- damage lowers float margin
- no-helm ship fails validation
- no-sail ship fails validation
- water sampling is deterministic for same seed / time / position
- ship JSON round-trips without data loss

### Libraries included now

- C++
- CMake
- SDL3
- bgfx
- Dear ImGui

### Libraries deferred

- Jolt
- Steamworks
- miniaudio
- asset import pipeline
- networking
- Workshop
- real ship rendering
- real ocean mesh

### Pass criteria

- app builds from a clean checkout
- native window opens
- bgfx clears the screen
- Dear ImGui panel renders
- self-test panel reports 11 / 11 passing
- app exits cleanly
- no WebGL / browser dependency
- no engine dependency

### Fail criteria

- any self-test fails
- app cannot close cleanly
- render backend initialization is hidden behind placeholder code
- CMake requires manual local paths not documented in the repo

## Dependency acquisition policy

**Decided: no package manager.** No FetchContent, no vcpkg. The repo stays self-contained and buildable offline with no registry fetch — matching the public-domain ("do whatever you want") framing, so anyone can clone and build with zero external strings. See [decisions.md](decisions.md) and [LICENSE](LICENSE).

### Rules

- Depend only on **permissive, no-strings libraries** (zlib / BSD / MIT / public-domain). The locked stack already qualifies: SDL3 (zlib), bgfx / bx / bimg (BSD-2), Jolt (MIT), Dear ImGui (MIT), miniaudio (public domain).
- **Vendor** those libraries' source directly, pinned, with their license notices preserved in a `THIRD-PARTY-LICENSES` file. Nothing is pulled at configure time.
- **Build from scratch** anything that would attach license strings or cost (e.g. FMOD / Wwise → use miniaudio or roll your own).
- **Steamworks** is the one unavoidable proprietary piece. Isolate it as an optional module so the engine/framework builds and runs without it.

### bgfx note

Upstream bgfx builds with GENie/premake, not CMake, and needs its `bx` + `bimg` siblings. Vendoring means either copying a CMake-ified bgfx tree in-tree or writing the CMake for it. The community bgfx.cmake wrapper is the usual CMake face for bgfx; if used, vendor it in-tree rather than fetching it at configure time.

### Dear ImGui note

No official ImGui→bgfx renderer backend exists anywhere. Vendor a small backend file (community-standard implementation, ~200 lines) alongside the official `imgui_impl_sdl3.cpp` platform backend, or write it.

### Open sub-question

- Exact vendoring mechanic: **in-tree source copy** (fully self-contained, larger repo) vs **git submodules pinned to commits** (lean repo, but submodule remotes must stay alive). In-tree copy is the more self-contained / no-strings choice; submodules are leaner.

## After Milestone 0

Once the foundation boots cleanly, the slice plan ([vertical-slice.md](vertical-slice.md)) resumes: generated ship rendering, Gerstner water mesh, buoyancy on a real ship, then the full Build → Sail loop. Jolt and Steamworks come in after that.
