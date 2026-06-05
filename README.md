# Pirate game

Working draft for a single Steam app with two modes inside: **Sail** (open-world pirate gameplay) and **Build** (simulator-grade shipbuilding). Players choose mode at the main menu. Ships made in Build appear in Sail.

**License:** public domain — [The Unlicense](LICENSE). Do whatever you want with it. Dependencies are kept permissive/no-strings; Steamworks is an isolated optional module (see [tech-stack.md](tech-stack.md)).

## Files

- [concept.md](concept.md) — project pitch and rationale for the single-product, two-mode shape
- [vertical-slice.md](vertical-slice.md) — **Sea Trial Prototype**: the first proof spine (Build → save → Sail → float → damage → sink)
- [native-bootstrap-plan.md](native-bootstrap-plan.md) — **Milestone 0** for the engineless C++ build: window + bgfx clear + ImGui panel + 11 self-tests, nothing else
- [spikes/threejs-sea-trial/](spikes/threejs-sea-trial/) — **disposable web spike** (React + Three.js) that proved the vertical-slice loop. Not the production path; the real game is engineless C++ per [tech-stack.md](tech-stack.md). Open `spikes/threejs-sea-trial/index.html` to play.
- [sail-mode.md](sail-mode.md) — Sail mode: vision, loop, world, systems
- [build-mode.md](build-mode.md) — Build mode: vision, loop, building systems, "sim but eased"
- [interop.md](interop.md) — Build ↔ Sail ship transfer, file format, Workshop validation
- [water-tech.md](water-tech.md) — shared water + buoyancy + physics subsystem (with high-level design)
- [multiplayer-infra.md](multiplayer-infra.md) — Steamworks-based multiplayer, host migration, scene budgets
- [tech-stack.md](tech-stack.md) — platform (Steam-first) and engine decisions
- [risks.md](risks.md) — what to prototype first, things that could invalidate the project
- [next-steps.md](next-steps.md) — immediate actions
- [decisions.md](decisions.md) — log of decisions made
- [holes.md](holes.md) — open questions master list
