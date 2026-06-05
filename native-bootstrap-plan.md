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

**Decided for Milestone 0: CMake FetchContent**, pinned dependencies, no manual local paths, no hidden global machine setup.

### Per-library plan

- **SDL3** — `FetchContent_Declare` pinned to a `release-3.x.x` tag. SDL3 ships native CMake and exposes the `SDL3::SDL3` target, so this is clean.
- **bgfx** — via the community **bgfx.cmake** wrapper (FetchContent, pinned commit). Upstream bgfx builds with GENie/premake, not CMake, and needs its `bx` + `bimg` siblings; bgfx.cmake bundles all three and gives them CMake targets (plus a `bgfx_compile_shaders` helper for later).
- **Dear ImGui** — `FetchContent_Declare` pinned to a tag, then a small hand-written CMake target that compiles ImGui core + the official `imgui_impl_sdl3.cpp` platform backend + a vendored bgfx renderer backend (see caveat).

### Caveats to resolve at scaffold time

- **Confirm the live canonical bgfx.cmake fork.** The lineage is messy (an archived original plus several active-looking forks). Verify which fork is current and healthy before pinning — the capability is standard, the exact repo URL is not settled here.
- **No official ImGui→bgfx renderer backend exists.** Vendor a small backend file into the repo (the community-standard "Richard Gale" implementation, ~200 lines) or write it. This is the one library source we vendor; there's no upstream to track. Independent of the FetchContent choice.

### Why FetchContent over the alternatives (for M0)

- **vs. vcpkg manifest** — SDL3 is excellent in vcpkg, but the bgfx port's freshness is the shakiest link, and vcpkg doesn't solve the genuinely hard part (the ImGui→bgfx backend is hand-wired either way). Adds a toolchain-bootstrap step. Overkill for three deps.
- **vs. git submodules** — equally reproducible, but more `.gitmodules` ceremony, and bgfx.cmake already manages its own submodules internally.

### What would flip this later

- Adding Jolt + asset importers + many transitive C++ deps → vcpkg manifest's resolution and binary cache start paying off; reconsider then.
- Needing to patch a dependency's source → submodules (or vcpkg overlay ports).
- CI re-download pain → add `FETCHCONTENT_BASE_DIR` caching, or move to submodules for offline determinism.

## After Milestone 0

Once the foundation boots cleanly, the slice plan ([vertical-slice.md](vertical-slice.md)) resumes: generated ship rendering, Gerstner water mesh, buoyancy on a real ship, then the full Build → Sail loop. Jolt and Steamworks come in after that.
