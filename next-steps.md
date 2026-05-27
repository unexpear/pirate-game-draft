# Next steps

A web-runnable prototype of the slice landed at [prototype/](prototype/) covering Phases 0–6 in browser form. The full slice plan ([vertical-slice.md](vertical-slice.md)) still calls for an engine-based prototype (lean Unreal) — that's the next major step.

## Immediate

- [ ] Play through the web prototype end-to-end and confirm the slice's pass criteria (slice §19) hold in the simplified 3D browser form
- [ ] Capture lessons / surprises from the web prototype before starting the engine version
- [ ] Lock engine pick: Unreal vs. Unity (lean Unreal)
- [ ] Begin Phase 0 of the engine prototype: project setup, menu, two empty scenes, debug overlay shell

## Done (initial pass)

- [x] Slice scope defined ([vertical-slice.md](vertical-slice.md))
- [x] Web prototype with Gerstner waves, Tier 2 sampling, ship JSON, validation, cargo/damage/sinking ([prototype/](prototype/))
- [x] Version control (this repo)
- [x] Project structure outlined (slice §17)

## Later (after the engine slice)

- Final-game engine decision (engineless still open in [tech-stack.md](tech-stack.md))
- Open-world content (islands, quests, etc. — explicitly held back per slice §25)
- Real Steam Workshop integration (slice uses local `/user_ships/` as stand-in)
- Multiplayer beyond the optional Phase 8 mini-test
- Filename cleanup (`game-a-pirate-sandbox.md` → `sail-mode.md`, etc.)
