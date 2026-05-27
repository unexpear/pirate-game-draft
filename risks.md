# Risks & validation order

Things to prototype early because they could invalidate the project if they don't work.

1. **Buoyancy on modular ships** (Build mode) — the hardest physics problem. Score-based model (see [water-tech.md](water-tech.md)) tames most of it, but build a stress test before anything else in Build.
2. **Network sync of physics ships + host migration** — physics ships are hard to sync; seamless host handoff (see [multiplayer-infra.md](multiplayer-infra.md)) adds complexity. Prove a two-player sailing demo early, then add a migration test.
3. **Board-bending mesh generation** (Build mode) — prove the core building feel before designing UX around it.
4. **Underwater rendering at scale** (Sail mode) — prove visibility, lighting, and performance work for the diving experience.
5. **Ship file format** — versioned schema with Workshop validation and forward/backward compatibility across game updates. Simpler now (single product, no cross-product handoff), but still load-bearing.
