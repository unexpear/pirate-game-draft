# Open questions

Master list. Per-mode subsets live inline in [game-a-pirate-sandbox.md](game-a-pirate-sandbox.md) (Sail) and [game-b-shipbuilding-sim.md](game-b-shipbuilding-sim.md) (Build). Resolved items move to [decisions.md](decisions.md).

## Tech foundation
- Native Milestone 0 pass criteria — exact build/run/test bar for the first C++ scaffold before ship rendering, water rendering, Jolt, or Steamworks (see [native-bootstrap-plan.md](native-bootstrap-plan.md) for the baseline; details still firming up)
- Dependency acquisition strategy — git submodules vs vcpkg manifest vs FetchContent for SDL3, bgfx, Dear ImGui (see [native-bootstrap-plan.md](native-bootstrap-plan.md))
- Rendering backend details under bgfx — DirectX 11/12 vs Vulkan backend priority, shader pipeline, material format
- SDL3 platform layer boundaries — windowing, input, controller support, file paths
- Jolt integration boundaries — rigid bodies vs score-based buoyancy, where ship physics hands off to Jolt
- Dear ImGui debug/tooling scope — runtime panels, validators, self-tests, ship inspection
- Asset pipeline — source formats, cooked formats, hot reload, mesh/material import
- Single executable with internal mode switching, or two executables under one Steam app ID — affects mode-switching feel and patching workflow

## Sail mode
- Within-session persistence — Steam Cloud covers player profile; world/run state TBD
- World generation (procedural / handcrafted / hybrid)
- Progression design
- PvP framing
- Which Sea of Thieves design tricks to adopt for world-feel (voyage events, leaderboards, crossover encounters)

## Build mode
- Physics fidelity beyond v1 — current model is weight-vs-buoyancy scoring + Tier 2 sample points for waterline/pitch/heel; upgrade to distributed buoyancy or Jolt-assisted rigid-body interaction if that ever feels fake
- Mod support
- Tutorial / onboarding path — steep learning curve
- Collaborative-building UX inside a P2P session

## Ship transfer & Workshop
- Versioned ship file format with forward/backward compatibility across game updates — see [interop.md](interop.md)
- Workshop validation thresholds (max piece count, max bounding box, max file size) — exact numbers TBD
- Crossplay if non-Steam platforms come later

## Water / buoyancy / physics
- Piece-data schema (volume, mass, material, damage state) — drives both buoyancy and damage
- Tier sampler grid placement on irregular Build-mode hulls (bounding box / principal axes / designer hint)
- Diving entity tier — players at Tier 0 underwater, or upgrade for swimmer pitch/roll?
- Where swimmer-scale and ship-scale buoyancy code cleanly diverge (shared model + API done)

## Multiplayer & host migration
- Cascading host drops — new host disconnects fast after migration, then what?
- No eligible new host — graceful end-session?
- Distinguishing intentional quit from crash for migration UX

## Business
- Monetization model (single product baseline; DLC / cosmetics still open)
- Working titles for the product and the two modes
- Team size and structure

---

## Recently resolved (see [decisions.md](decisions.md))

- **Single Steam app with Sail + Build modes** (reverses prior two-product split — also moots bundle pricing, A-only marketing, B-demo-in-A, which-app-hosts-Workshop)
- NPC ship cap — separate budget from player ships; Tier 0/1
- Host migration behavior — seamless with brief-pause UX
- Steam Workshop confirmed in v1 with upload + download validation
- Reverse interop (Game A → Game B export) — moot now (same product)
- Buoyancy model — weight vs. score, not distributed physics
- Boards are data, not per-frame physics samples
- Sample-point tiers (Tier 2 default, rectangular grid)
- Wave-floor / breaking waves in v1 — punted by perf budget
- Performance target — 2016 mid-range PC
- Wave gen — multi-frequency Gerstner (FFT cut)
- Platform — Steam first
- Multiplayer architecture — P2P session via Steamworks
