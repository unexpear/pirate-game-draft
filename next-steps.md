# Next steps

The Three.js browser spike at [spikes/threejs-sea-trial/](spikes/threejs-sea-trial/) proved the vertical-slice loop. **It is not the production path.** The final game target is an **engineless native C++ game using selected libraries** — see [tech-stack.md](tech-stack.md). The next build target is a custom C++ vertical slice.

## Immediate

Milestone 0 is underway in [native/](native/):

- [x] 11 native self-tests pass — pure C++17, no deps; verified on g++ (MSYS2 UCRT64) and MSVC 2022
- [x] Builds from a clean checkout, exits cleanly, `ctest`-wired
- [x] SDL3 native window opens (release-3.4.10, static submodule) — opens, runs the self-tests, clean exit
- [ ] bgfx clears the screen
- [ ] Dear ImGui debug panel renders
- [ ] (still no ship/water rendering, no Jolt, no Steamworks — per [native-bootstrap-plan.md](native-bootstrap-plan.md))

- [ ] Resolve the vendoring mechanic (in-tree source copy vs pinned submodules) before pulling SDL3/bgfx/ImGui in
- [ ] After Milestone 0: add ship rendering, Gerstner water mesh, Jolt, then resume the full Build → Sail loop per [vertical-slice.md](vertical-slice.md)

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
