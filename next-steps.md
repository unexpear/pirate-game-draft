# Next steps

The vertical slice plan ([vertical-slice.md](vertical-slice.md)) supersedes most of what used to live here:

- Engine direction (for prototype): existing engine, lean Unreal
- Starting mode: both — the slice spans Sail and Build by design
- MVP scope: defined (Build → save → load in Sail → float → damage → sink)
- Project structure: outlined in slice §17
- Version control: done (this repo)

## Immediate

- [ ] Lock engine pick: Unreal vs. Unity (lean Unreal — see [vertical-slice.md §3](vertical-slice.md))
- [ ] Execute Phase 0 of the slice: project setup, menu, two empty scenes, debug overlay shell
- [ ] Begin Phase 1 (ship schema) once Phase 0 passes

## Later (after the slice)

- Final-game engine decision (engineless still open in [tech-stack.md](tech-stack.md))
- Open-world content (islands, quests, etc. — explicitly held back per slice §25)
- Real Steam Workshop integration (slice uses local `/user_ships/` as stand-in)
- Multiplayer beyond the optional Phase 8 mini-test
- Filename cleanup (`game-a-pirate-sandbox.md` → `sail-mode.md`, etc.)
