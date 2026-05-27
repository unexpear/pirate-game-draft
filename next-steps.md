# Next steps

The Three.js browser spike at [spikes/threejs-sea-trial/](spikes/threejs-sea-trial/) proved the vertical-slice loop. **It is not the production path.** The final game target is an **engineless native C++ game using selected libraries** — see [tech-stack.md](tech-stack.md). The next build target is a custom C++ vertical slice.

## Immediate

- [ ] Bootstrap a native C++ project (CMake + SDL3 + bgfx + Jolt + Dear ImGui + miniaudio) — compiles + runs an empty window, no game logic yet
- [ ] Hit the first native milestone (per [vertical-slice.md §3](vertical-slice.md)):
  - Open native window
  - Render 3D grid
  - Render generated ship mesh
  - Render Gerstner water mesh
  - Run the same 11 self-tests in C++
  - Show ImGui debug panel
- [ ] Resume the full Build → Sail loop in native code after the milestone passes

## Done

- [x] Slice scope defined ([vertical-slice.md](vertical-slice.md))
- [x] Web spike (React + Three.js) with Gerstner waves, Tier 2 sampling, ship JSON, validation, cargo/damage/sinking ([spikes/threejs-sea-trial/](spikes/threejs-sea-trial/))
- [x] 11/11 self-tests passing in the web spike — model-spine regression gate
- [x] Engine direction locked: engineless C++ stack ([tech-stack.md](tech-stack.md))
- [x] Version control (this repo)

## Later

- Open-world content (islands, quests, etc. — explicitly held back per slice §25)
- Real Steam Workshop integration (web spike used local `/user_ships/` as stand-in)
- Multiplayer beyond the slice's optional Phase 8 mini-test
- Filename cleanup (`game-a-pirate-sandbox.md` → `sail-mode.md`, etc.)
