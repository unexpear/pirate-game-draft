# Next steps

The Three.js browser spike at [spikes/threejs-sea-trial/](spikes/threejs-sea-trial/) proved the vertical-slice loop. **It is not the production path.** The final game target is an **engineless native C++ game using selected libraries** — see [tech-stack.md](tech-stack.md). The next build target is a custom C++ vertical slice.

## Immediate

**Milestone 0 is complete** ✅ — [native/](native/):

- [x] 11 native self-tests pass — pure C++, verified on g++ (MSYS2 UCRT64) and MSVC 2022
- [x] Builds from a clean checkout, exits cleanly, `ctest`-wired
- [x] SDL3 native window opens (release-3.4.10, static submodule)
- [x] bgfx clears the screen (Direct3D 11 auto-selected)
- [x] Dear ImGui debug panel renders (v1.92.8, self-contained bgfx renderer) — shows the 11 self-tests + Test Sloop stats, visually verified
- [x] Vendoring mechanic resolved: **pinned git submodules** under `extern/` (SDL, bgfx.cmake, imgui)

## Next (post-Milestone 0)

- [x] Ship rendering — the Test Sloop draws as 3D oriented boxes (one per hull piece) via bgfx debugdraw, orbit camera, colored by type/damage.
- [x] Gerstner water — animated wireframe ocean grid, CPU-displaced by the model's own `sampleWater` (so it's deterministic and *exactly* matches the buoyancy math). Ship sits at the waterline.
- [ ] Buoyancy on the floating ship — drive the ship's ride height + pitch/heel from the wave surface at its Tier-2 sample points, matching the model's float margin. This is the payoff: the ship bobs on the real waves.
- [ ] Real shader pipeline (shaderc + `bgfx_compile_shaders`) — deferred; CPU water via debugdraw is enough for now. Needed when water goes GPU (solid lit surface / foam) or ship moves to lit meshes.
- [ ] Then the full Build → Sail loop per [vertical-slice.md](vertical-slice.md); Jolt + Steamworks after.

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
