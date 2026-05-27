# Decisions log

| Date | Decision | Rationale |
|------|----------|-----------|
| — | Split project into two games | Each can go deep on its strength; smaller scope per game; staged release possible |
| — | Multiplayer required for both | Core to the design vision |
| — | Player-built ships in Game B use modular free-placement | Like The Forest / survival games, not voxel |
| 2026-05-27 | Buoyancy: weight vs. score (not distributed physics) | Cheap to network, no exploding physics, legible to players. Standard pirate-sim approach (Sea of Thieves, Subnautica, ARK). |
| 2026-05-27 | Ship pieces are data, not per-frame physics samples | Game B hulls aggregate piece volumes/masses into the buoyancy score; wave sampling stays at tier points. Resolves the voxel-resolution question. |
| 2026-05-27 | Sample-point tier system, rectangular grid, Tier 2 default | Ships are anisotropic + port-starboard symmetric → rectangular maps cleanly. Tier 2 (3×3 = 9) captures pitch + heel. Tier auto-assigned by size/piece count. |
| 2026-05-27 | Performance target: 2016 mid-range PC (i5-6500 / GTX 970-class / 8GB / 1080p) | Reference bar: Sea of Thieves on Xbox One. Forces practical choices (Gerstner over FFT, cube-map over SSR). |
| 2026-05-27 | Wave generation: multi-frequency Gerstner (FFT cut for v1) | Fits 2016 perf target; analytic on CPU for cheap buoyancy sampling. Revisit FFT only if hero shots demand it. |
| 2026-05-27 | Platform: Steam first | Steamworks gives SDR + Lobbies + Cloud + Workshop + Voice. $200 + 30%, no infra costs. |
| 2026-05-27 | Multiplayer: P2P session-based via Steamworks | No persistent shared world. Replicate world feel via Sea of Thieves design tricks (voyage events, leaderboards, crossover encounters). |
| 2026-05-27 | Ship sharing: Steam Workshop, versioned + validated | Game B → Game A pipeline rides Workshop. File format must be Workshop-compatible. |
| 2026-05-27 | **REVERSED**: Single Steam app with Sail + Build modes (overrides "Split project into two games") | One library entry, one Workshop, one Direct fee. Build is UI-gated, not purchase-gated. Precedents: Garry's Mod, Minecraft, Roblox. Moots bundle pricing, A-only marketing, B-demo-in-A, and which-app-hosts-Workshop questions. |
| 2026-05-27 | NPC ship budget separate from player ship budget | Player ships at Tier 2 (15 cap), NPC vessels at Tier 0/1 (~30). AI doesn't need pitch+heel fidelity; budgets don't compete. |
| 2026-05-27 | Seamless host migration on host quit | Brief-pause UX (2–5s "Host migrating…" popup). Eventually-consistent client state, snapshot broadcasts, host election, atomic authority transfer. SDR survives the migration. |
| 2026-05-27 | Steam Workshop validation: on upload AND download | Max piece count, max bounding box, max file size, structural validity (must float, must have helm + sails). Failing ships don't publish; failing downloads don't load. Specific thresholds TBD. |
