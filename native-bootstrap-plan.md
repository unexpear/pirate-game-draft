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

Milestone 0 should use pinned, reproducible dependencies.

Preferred:

- CMake project at repo root or `native/`
- pinned dependency versions
- no manual local paths required
- no global machine setup hidden from the repo

Candidate approaches:

- git submodules for bgfx / bx / bimg / imgui / SDL where needed
- vcpkg manifest if all required packages behave cleanly
- FetchContent only if build times and repo cleanliness stay acceptable

Decision still open:

- exact dependency strategy for SDL3 + bgfx + Dear ImGui

## After Milestone 0

Once the foundation boots cleanly, the slice plan ([vertical-slice.md](vertical-slice.md)) resumes: generated ship rendering, Gerstner water mesh, buoyancy on a real ship, then the full Build → Sail loop. Jolt and Steamworks come in after that.
