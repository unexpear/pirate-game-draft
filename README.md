# Pirate game

Working draft for a single Steam app with two modes inside: **Sail** (open-world pirate gameplay) and **Build** (simulator-grade shipbuilding). Players choose mode at the main menu. Ships made in Build appear in Sail.

## Files

- [concept.md](concept.md) — project pitch and rationale for the single-product, two-mode shape
- [vertical-slice.md](vertical-slice.md) — **Sea Trial Prototype**: the first proof spine (Build → save → Sail → float → damage → sink)
- [game-a-pirate-sandbox.md](game-a-pirate-sandbox.md) — Sail mode: vision, loop, world, systems
- [game-b-shipbuilding-sim.md](game-b-shipbuilding-sim.md) — Build mode: vision, loop, building systems, "sim but eased"
- [interop.md](interop.md) — Build ↔ Sail ship transfer, file format, Workshop validation
- [water-tech.md](water-tech.md) — shared water + buoyancy + physics subsystem (with high-level design)
- [multiplayer-infra.md](multiplayer-infra.md) — Steamworks-based multiplayer, host migration, scene budgets
- [tech-stack.md](tech-stack.md) — platform (Steam-first) and engine decisions
- [risks.md](risks.md) — what to prototype first, things that could invalidate the project
- [next-steps.md](next-steps.md) — immediate actions
- [decisions.md](decisions.md) — log of decisions made
- [holes.md](holes.md) — open questions master list

> Note: file names `game-a-pirate-sandbox.md` and `game-b-shipbuilding-sim.md` predate the single-product reframe. Content reflects Sail/Build modes; renames pending.
