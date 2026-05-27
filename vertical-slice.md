# Vertical Slice Plan — Sea Trial Prototype

## Purpose

This vertical slice proves the shared spine of the pirate game before building the full game.

It does **not** try to prove the whole pirate fantasy. It proves the riskiest chain:

**Build a simple ship → validate it → save it → load it in Sail mode → float it on deterministic waves → sail it → damage/overload it → sink or survive.**

That chain directly tests the core idea of one Steam app with two modes — see [concept.md](concept.md).

---

## 1. Slice name

**Sea Trial Prototype.** Working internal name only.

The player does this:

1. Opens the app.
2. Chooses **Build Test**.
3. Creates or edits a tiny test ship.
4. Runs validation.
5. Saves the ship.
6. Returns to menu.
7. Chooses **Sail Test**.
8. Loads that same ship.
9. Sails around a small ocean test arena.
10. Adds cargo, takes damage, or loses hull pieces.
11. Sees the ship's buoyancy change.
12. Either survives, capsizes, floods, or sinks.

That's the slice.

---

## 2. What this proves

### Core proof

The vertical slice proves the two-mode architecture is viable:

- Build mode creates a ship.
- Ship data survives as a file.
- Sail mode reads that file.
- The water/buoyancy system understands it.
- The ship behaves visibly differently based on weight, volume, damage, and layout.

The ship file format is already flagged as load-bearing in [interop.md](interop.md): it must support local transfer, Workshop, validation, buoyancy, damage, compatibility, and future updates.

### Technical proof

This slice proves:

- deterministic wave sampling
- weight-vs-buoyancy scoring
- tiered buoyancy sample points
- basic ship validation
- ship save/load
- cross-mode handoff
- basic damage affecting float behavior
- 60 fps budget direction

The water design already chose multi-frequency Gerstner waves, score-based buoyancy, and tiered sampling — see [water-tech.md](water-tech.md) and [decisions.md](decisions.md).

---

## 3. Engine decision for the slice

### Decision

Use an existing engine for the vertical slice. Do **not** go engineless for the first prototype.

### Reason

The current docs correctly say the engine decision gates everything else — see [tech-stack.md](tech-stack.md).

For this slice, the goal is not to prove that a custom engine can exist. The goal is to prove the game concept works.

The slice needs water rendering, rigid body physics, input, UI, save/load, debug overlays, asset loading, and (later) Steamworks + multiplayer integration. Building those from scratch first burns time before the game has proven itself.

### Practical choice

Use **Unreal or Unity** for the prototype — both already noted as the strongest options in [tech-stack.md](tech-stack.md).

For this specific game, lean **Unreal for the prototype** because the project wants ocean visuals, ships, physics, multiplayer, Steam release, and large-world feel.

The architecture below stays engine-neutral enough to port later. Engineless is still open for the final game.

---

## 4. Slice scope

### Included

- main menu with two buttons: **Build Test**, **Sail Test**
- small dry dock Build scene
- small ocean Sail scene
- one simple ship type
- simple modular hull editing
- ship piece data
- ship validation
- versioned ship save file
- deterministic Gerstner ocean
- Tier 2 buoyancy sampling for the test ship
- weight-vs-buoyancy behavior
- damage removing buoyancy contribution
- cargo overload test
- basic helm/sail control
- debug overlays
- test logs

### Not included

- full open world
- islands, ports, quests, factions, progression
- real Workshop upload, real Steam Cloud
- sea creatures
- boarding combat, full cannon combat
- real board-bending workflow
- full underwater cave rendering
- ship interiors
- AI ships
- host migration
- collaborative building
- cosmetics, economy, character persistence

These are real game features, not proof features.

---

## 5. Player-facing flow

### Main Menu

```text
Pirate Game Prototype

[Build Test]
[Sail Test]
[Quit]
```

### Build Test

A small dry dock with:

- one test hull frame
- editable hull dimensions
- simple plank/piece placement
- cargo/cannon weight controls
- validation button
- save button
- launch-to-sail button

Not a full shipbuilder — a controlled test bench.

### Sail Test

A small ocean arena with:

- the saved ship
- ocean waves
- wind direction marker
- sail control, rudder control
- cargo/damage test controls
- buoyancy debug panel

The player can sail forward, turn, add/remove cargo, damage/repair hull sections, reload ship file, reset scene.

---

## 6. Build mode slice

### Goal

Prove only: **can player-made ship data become runtime ship behavior?** Not full creativity yet.

### Build scene

Dry dock with a fixed camera and one editable ship. The ship starts as a small sloop-like hull. The player can adjust:

- hull length, width, depth
- number of ribs
- plank density
- material preset
- mast / sail / helm present-absent
- cargo mass
- cannon count

### Editing model

Use simplified modular editing. No real board bending yet. Represent planks as generated pieces attached to a hull curve.

Each piece stores data:

```json
{
  "id": "piece_001",
  "type": "plank",
  "material": "oak",
  "volume": 0.18,
  "mass": 95.0,
  "damage": 0.0,
  "position": [0.0, 1.2, -3.4],
  "rotation": [0.0, 0.0, 0.0],
  "bounds": [0.3, 0.08, 2.0]
}
```

Matches the decision in [water-tech.md](water-tech.md): ship pieces are data, not per-frame physics samples. Destroyed pieces drop out of buoyancy scoring.

### Minimum ship components

A valid prototype ship must have:

- hull volume
- total mass
- at least one helm, sail, mast
- at least one buoyancy sample grid
- collision bounds
- visual mesh
- piece list
- version number

### Validation rules

A ship passes validation if:

1. It has a helm.
2. It has a sail.
3. It has positive buoyancy score.
4. It has positive mass.
5. It has collision bounds.
6. It fits prototype size limits.
7. It has no invalid piece values.
8. It can float unloaded.
9. It does not exceed piece count cap.
10. It can serialize and deserialize without data loss.

Mirrors the Workshop validation direction in [interop.md](interop.md): max piece count, max bounding box, max file size, structural validity (must float, must have helm + sails).

---

## 7. Sail mode slice

### Goal

Prove only: **can a Build-mode ship become a controllable ship on water?**

### Sail scene

Small ocean box. No land required. Add:

- ocean plane
- sky, sun
- wind arrow
- spawn point
- one loaded ship
- one floating test buoy
- reset trigger
- debug text

### Sail controls

```text
W / S = sail trim or throttle abstraction
A / D = rudder
R     = reset ship
L     = reload saved ship
C     = add cargo
V     = remove cargo
X     = damage random hull piece
Z     = repair all
F1    = toggle debug overlay
```

### Sailing model

Fake-but-useful sailing:

- wind has a fixed world direction
- sail trim produces forward thrust based on angle to wind
- rudder turns ship when moving
- drag slows ship
- waves affect vertical position, pitch, and heel

No advanced sail simulation yet. The slice is about buoyancy and handoff, not real seamanship.

---

## 8. Water system

### Ocean

Use deterministic multi-frequency Gerstner waves. The water system exposes one API:

```text
SampleWater(x, z, time) -> WaterSample
```

Where:

```text
WaterSample:
- height
- normal
- slope
- velocity
```

Matches [water-tech.md](water-tech.md): Gerstner chosen because it's cheap, analytic, and easy to sample on CPU for physics.

### Determinism

The wave field is defined by:

```text
seed
session_start_time
wave_components[]
```

Each wave component has:

```text
direction
amplitude
wavelength
speed
phase
steepness
```

Do not replicate the whole water surface. Replicate or store only the seed and parameters. [water-tech.md](water-tech.md) requires deterministic Gerstner with synced session time so P2P clients agree on wave state.

---

## 9. Buoyancy model

### Chosen model

Weight vs. buoyancy score.

```text
buoyancy_score = sum(active_piece_volume * water_density)
weight         = hull_mass + cargo_mass + cannon_mass + crew_mass
```

Behavior:

```text
if weight < buoyancy_score:
    ship floats

if weight > buoyancy_score:
    ship sinks
```

The gap controls ride height and sink rate. Already the chosen decision — see [decisions.md](decisions.md).

### Damage

Each hull piece has damage `0.0` to `1.0`.

```text
effective_volume = volume * (1.0 - damage)
```

Destroyed pieces (`damage >= 1.0`) contribute zero volume. Damage directly affects whether the ship floats.

### Cargo overload

Cargo adds mass but no buoyancy. Lets the prototype prove:

- empty ship floats high
- loaded ship rides lower
- overloaded ship sinks
- damaged ship may sink even if cargo is unchanged

---

## 10. Buoyancy sampling tier

Use Tier 2 for the prototype ship. Tier 2 is a 3×3 rectangular grid, capturing heave + pitch + heel. [water-tech.md](water-tech.md) sets Tier 2 as the default for normal ships.

### Prototype grid

9 sample points:

```text
front-left      front-center      front-right
mid-left        mid-center        mid-right
rear-left       rear-center       rear-right
```

Each frame:

1. Convert sample points into world space.
2. Sample water height at each point.
3. Compare sample point height to water height.
4. Estimate submerged amount.
5. Aggregate into vertical force.
6. Derive target pitch and heel.
7. Apply smoothing.
8. Push result into physics body.

### Debug overlay

Show:

```text
Ship mass
Buoyancy score
Float margin
Cargo mass
Damaged pieces
Waterline
Pitch
Heel
Sample tier
Wave seed
FPS
```

---

## 11. Ship file format

### File extension

```text
.ship.json
```

Example: `test_sloop.ship.json`

### Save location

Prototype local path: `/user_ships/`. Later this maps to Steam Cloud and Workshop — see [interop.md](interop.md).

### Minimum schema

```json
{
  "schema_version": 1,
  "ship_id": "test_sloop_001",
  "display_name": "Test Sloop",
  "created_by": "local",
  "build_version": "prototype",
  "bounds": {
    "length": 12.0,
    "width": 4.0,
    "height": 5.0
  },
  "runtime": {
    "buoyancy_tier": 2,
    "sample_grid": {
      "type": "rectangular",
      "rows": 3,
      "columns": 3
    },
    "collision_asset": "generated",
    "visual_asset": "generated"
  },
  "systems": {
    "helm_count": 1,
    "sail_count": 1,
    "mast_count": 1,
    "cannon_count": 2,
    "cargo_mass": 0.0
  },
  "pieces": [
    {
      "id": "piece_001",
      "type": "plank",
      "material": "oak",
      "volume": 0.18,
      "mass": 95.0,
      "damage": 0.0,
      "position": [0.0, 1.2, -3.4],
      "rotation": [0.0, 0.0, 0.0],
      "bounds": [0.3, 0.08, 2.0]
    }
  ]
}
```

### Compatibility rule

Never save anonymous data blobs. Everything must be named, versioned, and convertible.

Bad:

```json
{ "data": "..." }
```

Good:

```json
{ "schema_version": 1, "pieces": [] }
```

Schema must be stable, versioned, and forward/backward compatible across updates — per [interop.md](interop.md).

---

## 12. Validation system

### Validator output

```json
{
  "valid": false,
  "errors": [
    { "code": "MISSING_HELM", "message": "Ship must have at least one helm." }
  ],
  "warnings": [
    { "code": "LOW_FLOAT_MARGIN", "message": "Ship floats, but has little spare buoyancy." }
  ],
  "stats": {
    "mass": 2400.0,
    "buoyancy_score": 3200.0,
    "float_margin": 800.0,
    "piece_count": 84
  }
}
```

### Hard fail

- missing helm
- missing sail
- no pieces
- no buoyancy
- invalid mass or volume
- too many pieces
- bounds too large
- file too large
- impossible sample grid
- cannot load after save

### Soft warning

- low float margin
- very heavy cargo
- very narrow hull
- likely unstable
- high damage
- high piece count near cap

---

## 13. Damage test

### Simple damage model

In Sail Test, pressing `X` damages a random hull piece:

```text
piece.damage += 0.25
```

At `damage = 1.0`, the piece stops contributing buoyancy. The debug overlay updates immediately.

### Visual feedback

Damaged pieces visibly change. Simple options: darker material, cracked material, missing plank, red debug highlight. No full destruction yet.

### Required behavior

The player must be able to create this chain:

```text
Ship floats normally
Add cargo            → ship rides lower
Damage hull          → float margin drops
Damage more hull     → ship sinks
Remove cargo/repair  → ship recovers if still valid
```

That is the core "this system is real" moment.

---

## 14. Networking slice

### Do not start with full multiplayer

The first vertical slice is single-player. But it must be designed so multiplayer is not impossible later. [multiplayer-infra.md](multiplayer-infra.md) and [risks.md](risks.md) flag P2P physics sync and host migration as serious risks and recommend a two-player sailing prototype after the basics work.

### Multiplayer-ready constraints now

Even in single-player, enforce:

- deterministic wave seed
- deterministic wave parameters
- clean ship state snapshot
- no hidden local-only buoyancy state
- no random damage without stored RNG seed/event
- serializable ship runtime state

### Later two-player mini-test

After the single-player slice works:

- host creates session
- client joins
- both load same ship
- host controls ship
- client sees same waterline, pitch, heel, position
- host damages ship → client sees buoyancy change
- no host migration yet

### Host migration

Cut from vertical slice. Defined in [multiplayer-infra.md](multiplayer-infra.md) as a chosen direction, but not needed to prove the first ship loop.

---

## 15. Performance target

Hit the 2016 mid-range PC target already set in [water-tech.md](water-tech.md):

```text
CPU: i5-6500 class
GPU: GTX 970 / GTX 1060 / RX 480 class
RAM: 8 GB
Resolution: 1080p
Target: 60 fps
```

### Slice budget

```text
1 player ship
Tier 2 buoyancy
9 water samples per frame
No NPC ships
No AI
No combat simulation
No big world
No streaming terrain
```

This should be cheap. If this runs badly, the architecture is wrong.

---

## 16. Debug tools

Add debug tools immediately.

### Ship overlay

```text
Mass:
Buoyancy:
Float margin:
Cargo:
Damage:
Velocity:
Pitch:
Heel:
Waterline:
```

### Water overlay

```text
Wave seed:
Wave count:
Sample tier:
Sample points:
Time:
```

### Validation overlay

```text
Valid:
Errors:
Warnings:
Piece count:
File size:
Bounds:
```

### Required debug visuals

- draw buoyancy sample points
- draw waterline
- draw ship bounds
- draw center of mass
- draw center of buoyancy
- color damaged pieces
- color invalid pieces

No hidden magic.

---

## 17. Suggested repo/project structure

Engine-specific layout can vary, but conceptually:

```text
/src
  /app
    MainMenu
    ModeRouter

  /modes
    /build
      BuildTestScene
      HullEditor
      PieceEditor
      ValidationPanel

    /sail
      SailTestScene
      ShipController
      SailInput
      DamageTester

  /shared
    /ships
      ShipData
      ShipPieceData
      ShipRuntimeState
      ShipSerializer
      ShipValidator
      ShipStats

    /water
      GerstnerWaveField
      WaterSampler
      BuoyancySampler
      BuoyancyAggregator

    /physics
      ShipPhysicsAdapter
      FloatingBody

    /debug
      DebugOverlay
      DebugDraw

/user_ships
  test_sloop.ship.json

/docs
  VERTICAL_SLICE_PLAN.md
```

---

## 18. Implementation order

### Phase 0 — Project setup

Deliver: engine project, repo structure, main menu, Build Test scene, Sail Test scene, basic logging, debug overlay shell.

**Pass:** app opens, menu works, both scenes load, logs write.

### Phase 1 — Ship schema

Deliver: `ShipData`, `ShipPieceData`, `ShipSerializer`, load/save JSON, test ship file, schema version field, basic validator.

**Pass:** a test ship saves to `/user_ships/`, reloads with no data loss, invalid files fail gracefully.

### Phase 2 — Build Test scene

Deliver: dry dock scene, simple hull generator, editable dimensions, piece list generation, cargo/cannon controls, validation button, save button.

**Pass:** player can generate a valid simple ship; player can make it invalid; validator catches the invalid state; valid ship saves.

### Phase 3 — Water system

Deliver: Gerstner wave field, CPU water sampler, water surface rendering, wave seed, debug sample marker.

**Pass:** given the same seed and time, water samples are repeatable; ship scene can query water height at arbitrary x/z.

### Phase 4 — Buoyancy

Deliver: Tier 2 sample grid, buoyancy score calculation, mass calculation, vertical force, pitch/heel approximation, waterline debug view.

**Pass:** ship floats; ship rides lower when mass increases; ship sinks when overloaded; ship tilts with wave slope.

### Phase 5 — Sail Test scene

Deliver: load saved ship, spawn on ocean, rudder input, sail/throttle input, camera, reset/reload controls.

**Pass:** a ship created in Build Test appears in Sail Test; it floats; it moves; it turns; it can be reset and reloaded.

### Phase 6 — Damage and recovery

Deliver: damage random hull piece, repair all, damage affects buoyancy, damaged visual state, updated validation stats.

**Pass:** damaging pieces lowers buoyancy; enough damage sinks the ship; repairing restores buoyancy.

### Phase 7 — Prototype hardening

Deliver: file size cap, piece count cap, bounds cap, validation report UI, malformed file tests, debug snapshots, basic perf counter.

**Pass:** bad files do not crash the app; bad ships do not enter Sail Test; performance stays stable with the test ship.

### Phase 8 — Optional two-player test

Only after single-player works.

Deliver: host session, join session, deterministic wave seed shared, host-controlled ship replicated to client, damage event replicated, client debug overlay matches host within tolerance.

**Pass:** two players see the same ship behavior; damage affects both clients; no full host migration yet.

---

## 19. Pass/fail criteria for the whole slice

### Passes if

1. Build Test can create a ship.
2. Validator can reject broken ships.
3. Valid ship saves to disk.
4. Sail Test loads the saved ship.
5. Ship floats on deterministic waves.
6. Ship pitch/heel visibly reacts to wave sampling.
7. Cargo weight changes waterline.
8. Damage changes buoyancy.
9. Overload or damage can sink the ship.
10. Repair or cargo removal can recover the ship if still structurally valid.
11. Debug overlay exposes the real numbers.
12. No invalid ship crashes the runtime.
13. The file format has `schema_version`.
14. The slice keeps the door open for Steam Cloud/Workshop later.
15. The slice does not require a full open world.

### Fails if

- Build and Sail require separate incompatible ship representations.
- Buoyancy relies on hidden engine magic that cannot be serialized.
- Ship files are not versioned.
- Damage is visual only and does not affect buoyancy.
- Water sampling is not deterministic.
- The ship can only work as a hand-authored prefab.
- The prototype needs a full world before proving float/sail/sink.

---

## 20. What to fake

Fake these deliberately:

- **Board bending** — generated planks along simple curves. Real board bending later.
- **Sailing** — simplified thrust and rudder. Real sail aerodynamics later.
- **Workshop** — local `/user_ships/`. Steam Workshop later.
- **Steam Cloud** — local files. Cloud sync later.
- **Structural simulation** — simple validation stats. Real stress/failure later.
- **Visual damage** — material changes or hidden pieces. Real destruction later.

---

## 21. What not to fake

- **Ship data** — must really be structured data.
- **Buoyancy** — weight, volume, damage, cargo must actually change float behavior.
- **Save/load** — Sail mode must load the ship from file, not from memory.
- **Validation** — broken ships must fail for real reasons.
- **Water sampling** — the ship must query the water system, not ride a hand-authored animation.

---

## 22. First prototype ship

```text
Name:            Test Sloop
Length:          12 m
Width:           4 m
Height:          5 m
Mass unloaded:   ~2400 kg
Buoyancy score:  ~3200 kg-equivalent
Float margin:    ~800 kg
Sails:           1
Helm:            1
Cannons:         2 optional
Cargo:           0–1500 kg test range
Buoyancy tier:   2
Sample grid:     3×3
```

Numbers can be tuned. They only need internal consistency.

---

## 23. First acceptance demo

```text
1.  Launch app.
2.  Open Build Test.
3.  Generate Test Sloop.
4.  Click Validate.
5.  Show valid report.
6.  Save ship.
7.  Return to menu.
8.  Open Sail Test.
9.  Load Test Sloop.
10. Sail forward.
11. Turn left/right.
12. Toggle debug overlay.
13. Add cargo until waterline drops.
14. Add too much cargo until ship sinks.
15. Reset.
16. Damage hull pieces until float margin drops.
17. Damage more until ship sinks.
18. Repair.
19. Reload saved ship.
20. Confirm original file still works.
```

That is the first real proof of the project.

---

## 24. Why this is the correct first slice

[risks.md](risks.md) lists the biggest risks as: modular ship buoyancy, network sync of physics ships, board-bending mesh generation, underwater rendering, ship file format.

This slice hits three of the five immediately:

- modular ship buoyancy
- ship file format
- future multiplayer determinism

It lightly touches board generation without committing to full board bending. It avoids underwater rendering — that can be tested later without blocking the Build → Sail loop.

---

## 25. Hard rule

Do not add open-world content until this works.

No islands. No quests. No kraken. No economy. No progression. No cosmetics. No multiplayer-first detour.

The first job is:

```text
Build ship.
Save ship.
Load ship.
Float ship.
Damage ship.
Sink ship.
```

Everything else waits.
