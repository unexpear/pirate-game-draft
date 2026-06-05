# Water + buoyancy + physics

Shared subsystem. Sail mode's combat/diving and Build mode's sea trials both sit on top of it. Originally the project's biggest technical risk — see [risks.md](risks.md). The weight-vs-score buoyancy decision and the 2016 perf target tame most of it.

---

## Requirements

### Functional
- Animated, visually convincing ocean surface
- Sample wave height, slope, and velocity at arbitrary `(x, z, t)` for buoyancy
- Compute buoyancy + hydrodynamic forces on all floating entities (ships, players, debris)
- Underwater rendering (caustics, fog, color attenuation, bioluminescence)
- Surface effects (foam, wake, splashes)
- Unified buoyancy model: weight vs. buoyancy score
- Tiered wave sampling: same API at all tiers, count + arrangement varies

### Non-functional
- 60 fps on 2016 mid-range PC: i5-6500-class CPU (4-core), GTX 970 / GTX 1060 / RX 480 GPU, 8GB DDR4, 1080p
- Bar: "Gorgeous on a mid-range gaming PC, playable on integrated graphics, scalable to high-end" — reference Sea of Thieves on Xbox One
- Deterministic across multiplayer clients (P2P session model; clients MUST agree on wave state)
- Scene budget: ~15 player ships at Tier 2 (≈135 wave samples) + ~30 NPC vessels at Tier 0/1 (≈30–90 samples). Independent budgets — player and NPC ships don't compete for the same cap.

### Constraints
- Shared codebase across both games
- Steam first — see [tech-stack.md](tech-stack.md). P2P session via Steamworks.
- Physics library will likely be Jolt / Bullet / Rapier rather than custom

---

## Buoyancy model — weight vs. score

Every floating entity has:
- **Buoyancy score** = sum of submerged volume × water density
- **Weight** = body mass + carried/attached stuff (cargo, crew, gear, ammo)
- Weight < buoyancy → floats; gap sets ride height
- Weight > buoyancy → sinks; gap sets sink rate

For **ships**: buoyancy = sum of hull pieces' displacement; weight = hull mass + cargo + cannons + crew. Pieces are **data** (volume + mass + damage state), NOT per-frame physics samples. Destroyed pieces drop out of the sum.

For **players**: buoyancy ≈ constant (body volume); weight = body + inventory + armor + weapons. Creates real gameplay tension — drop loot to swim free, choose light gear for diving, plate armor + treasure chest sinks you.

Standard pirate-sim approach (Sea of Thieves, Subnautica, ARK all roughly this). Simple, easy to network, no exploding physics, legible to players. Distributed-physics upgrade still possible later if rocking/heeling fidelity needs to go up.

---

## Sample-point fidelity — tier system

Same wave-sampling API at all tiers. Choose how many points to sample and how to combine them. **Rectangular grid** (NOT triangular): ships are anisotropic (length ≠ width) and port-starboard symmetric, which rectangular maps to naturally. Triangular biases toward bow or stern and complicates indexing.

| Tier | Points | Captures        | Use for                                          |
|------|--------|-----------------|--------------------------------------------------|
| 0    | 1      | heave           | players, skiffs, debris, dropped objects         |
| 1    | 3      | + pitch         | small craft                                      |
| 2    | 9      | + heel (3×3)    | **default for normal ships**                     |
| 3    | many   | distributed     | monster builds only; probably clusters to T2     |

Tier auto-assigned by ship size / piece count.

---

## High-level design

```
            ┌───────────────────────────────────────────────┐
            │   Ocean Surface Generator                      │
            │   Multi-frequency Gerstner → wave field W      │
            └─────────────┬───────────────────┬─────────────┘
                          │                   │
                          ▼                   ▼
              ┌────────────────────┐  ┌────────────────────┐
              │ Surface Renderer   │  │ Sampling API       │
              │ + foam, glint,     │  │ height/slope/vel   │
              │ caustics, fog      │  │                    │
              └────────────────────┘  └─────────┬──────────┘
                                                │
                                                ▼
                                   ┌──────────────────────────┐
                                   │ Per-entity Tier Sampler  │
                                   │ (Tier 0/1/2/3)           │
                                   └─────────┬────────────────┘
                                             │
                                             ▼
                                   ┌──────────────────────────┐
                                   │ Buoyancy Aggregator      │
                                   │ weight vs. score → force │
                                   │ + heel/pitch orientation │
                                   └─────────┬────────────────┘
                                             │
                                             ▼
                                   ┌──────────────────────────┐
                                   │ Physics Engine           │
                                   │ (Jolt / Rapier)          │
                                   └──────────────────────────┘
```

Per frame:
1. Surface generator advances `t`; Gerstner sum produces wave field analytically.
2. Sampling API serves CPU queries (physics) and GPU queries (rendering).
3. Each floating entity's tier sampler samples its N points from the wave field.
4. Aggregator computes submerged volume per point → total buoyancy score; compares to weight; derives net vertical force + tilt orientation; submits to physics engine.

---

## Key trade-offs

### Wave generation — Gerstner (chosen) vs. FFT
- **Multi-frequency Gerstner** chosen. Cheap, analytic, trivially sampled on CPU, fits the 2016 perf target.
- FFT cut: GPU-only, readback latency for physics, and Gerstner is sufficient for the Sea of Thieves reference bar.
- Revisit only if hero shots demand more.

### Buoyancy — aggregate score (chosen) vs. distributed physics
- **Score-based** chosen. Sum pieces' displacements vs. summed mass; sample at tier points only.
- Distributed physics (force per piece) would give better rocking/heeling but explodes networking cost and risks instability.
- Standard pirate-sim approach; easy to network; legible failure modes.

### Sampling — CPU with analytic Gerstner
- CPU sampling drives physics directly; cost scales as `tier_points × ships`, comfortably within budget at the 15-ship scene cap.
- GPU sampling unnecessary given Gerstner is analytic.

### Network sync of waves
- Deterministic Gerstner with seeded parameters + synced session time → every client computes identical `W(x,z,t)`.
- P2P session (see [multiplayer-infra.md](multiplayer-infra.md)) makes this non-optional: no authoritative server to fix divergence.
- Cheap to get right; impossible to retrofit.

### Physics engine
- **Jolt**: modern, vehicle/ship-friendly, C++.
- **Rapier**: Rust-native, growing, maps well to a Rust codebase.
- Bullet showing age.
- Blocked on language pick in [tech-stack.md](tech-stack.md). Default: Jolt if C++, Rapier if Rust.

---

## Performance budget — what's in, what's out

Target: 2016 mid-range PC (i5-6500, GTX 970/1060, 8GB, 1080p). Reference: Sea of Thieves on Xbox One (2013 hardware).

**In:** multi-frequency Gerstner waves, crest-based whitecaps, wind streaks, sun glint, depth-based color, wake foam, texture-based caustics (animated projection from sun), basic underwater fog, cube-map reflections.

**Cut / downshifted:**
- FFT ocean simulation → Gerstner instead
- Screen-space reflections → cube-map only
- Real-time computed caustics → baked or faked projection
- Heavy shore-foam particle systems → sprite-based and capped if used at all
- Tier 3 distributed buoyancy on monster builds → probably clusters down to Tier 2

---

## Revisit later

- FFT ocean for cinematic moments (if perf headroom appears)
- Distributed-physics buoyancy upgrade (if rocking/heeling needs more fidelity)
- Storm-state wave field switching with continuous blend
- Wave-floor interaction (shallows breaking waves) — piggyback on v1 foam infrastructure when added
- High-fidelity hull-water interaction: planing, slamming, broaching

---

## Open questions

- Piece-data schema (volume, mass, material, damage state) — drives both buoyancy and damage modeling
- Tier sampler grid placement on irregular Build-mode hulls (bounding box / principal axes / designer hint)
- Diving entity tier — players at Tier 0 underwater, or upgrade for swimmer pitch/roll?
- Where swimmer-scale and ship-scale buoyancy code cleanly diverge (shared model + API done; abstractions TBD)

Consolidated in [holes.md](holes.md).
