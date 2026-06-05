# Next steps

The Three.js browser spike at [spikes/threejs-sea-trial/](spikes/threejs-sea-trial/) proved the vertical-slice loop. **It is not the production path.** The final game target is an **engineless native C++ game using selected libraries** — see [tech-stack.md](tech-stack.md). The next build target is a custom C++ vertical slice.

## Immediate

- [ ] Hit Milestone 0 per [native-bootstrap-plan.md](native-bootstrap-plan.md):
  - SDL3 native window opens
  - bgfx clears the screen
  - Dear ImGui debug panel renders
  - 11 native self-tests pass
  - App builds from a clean checkout and exits cleanly
  - No ship rendering, no water rendering, no Jolt, no Steamworks
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
