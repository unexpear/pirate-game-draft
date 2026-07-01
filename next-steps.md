# Next steps

The Three.js browser spike at [spikes/threejs-sea-trial/](spikes/threejs-sea-trial/) proved the vertical-slice loop. **It is not the production path.** The final game target is an **engineless native C++ game using selected libraries** — see [tech-stack.md](tech-stack.md). The next build target is a custom C++ vertical slice.

## Immediate

**Milestone 0 is complete** ✅ — [native/](native/):

- [x] Native model self-tests pass — pure C++, verified on g++ (MSYS2 UCRT64) and MSVC 2022, and in CI
- [x] Builds from a clean checkout, exits cleanly, `ctest`-wired
- [x] SDL3 native window opens (release-3.4.10, static submodule)
- [x] bgfx clears the screen (Direct3D 11 auto-selected)
- [x] Dear ImGui debug panel renders (v1.92.8, self-contained bgfx renderer) — shows the 11 self-tests + Test Sloop stats, visually verified
- [x] Vendoring mechanic resolved: **pinned git submodules** under `extern/` (SDL, bgfx.cmake, imgui)

## Next (post-Milestone 0)

- [x] Ship rendering — the Test Sloop draws as 3D oriented boxes (one per hull piece) via bgfx debugdraw, orbit camera, colored by type/damage.
- [x] Buoyancy on the floating ship — `sea::computeFloatPose` derives heave + pitch + heel from the wave surface at the Tier-2 sample points, offset by float margin (heavier rides lower). The ship bobs and tilts on the real waves; live values in the panel. Covered by a self-test.
- [x] Shader pipeline — shaderc + `bgfx_compile_shaders` (D3D11 profile), app forces the D3D11 renderer. Reusable for all future shaders.
- [x] GPU water — solid lit ocean: a dense grid displaced in the vertex shader by the *same* wave sum as `sea::sampleWater` (params passed as uniforms, so it still matches buoyancy), with N·L shading, fresnel, and crest foam. Replaced the CPU wireframe.

## Next (post-water)

- [x] Ship on lit meshes — each hull piece is a lit box (per-face N·L + ambient) on the shader pipeline; debugdraw removed entirely (water + ship are both GPU now). `src/ship_mesh.*`, `shaders/{vs,fs}_mesh.sc`.
- [x] Interactive cargo/damage — the ship is live: a cargo slider + Damage/Repair/Reset buttons + keys (C/V/X/Z/R) feed straight into buoyancy. Overloading or breaking planks drops the float margin, the ship rides lower, then **founders and sinks beneath the waves** (progressive sink accumulation); lighten it or repair in time and it recovers. Panel shows live status (afloat / SINKING), cargo, damage, float margin.
- [x] Sailing (Black Flag control scheme) — W/S step sail tiers (anchored / half / full), A/D steer (more agile at low speed), chase camera behind the heading. Ship-centric: the hull stays at the origin and the ocean scrolls past (wave field sampled at the ship's virtual world position, so buoyancy stays consistent). See [references/black-flag.md](references/black-flag.md).
- [x] Wind + visible sail (Black Flag style) — a wind direction drifts over time; you make best speed running with it (downwind → 100% drive) and least beating into it (upwind → ~55%). Panel shows the wind bearing + tail/cross/head + drive %. A mast + canvas sail now render on the hull (data-driven on the ship's mast/sail counts) and the sail trims partway toward the wind.
- [ ] Mouse camera control (drag-orbit / scroll-zoom).
- [ ] The full Build → Sail loop per [vertical-slice.md](vertical-slice.md); Jolt + Steamworks after.

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
