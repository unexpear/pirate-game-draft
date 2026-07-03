# Build mode (shipbuilding sim)

## Vision

A simulator-grade shipbuilding game where players construct real, functional vessels. Board-bending, structural integrity, hydrodynamics. "Sim but eased" — the boring/repetitive engineering work is automated or abstracted, but the meaningful design decisions remain in the player's hands.

## Core gameplay loop

1. Lay out hull form (keel, ribs, spine)
2. Plank the hull (board-bending, fastening)
3. Add internal structure (decks, bulkheads, framing)
4. Add rigging (masts, spars, sails)
5. Add functional systems (cannons, helm, rudder, capstan, etc.)
6. Test — float trial, sail trial, stress test
7. Iterate
8. Share / export

## Building systems

### Board-bending
- Procedural plank meshes that flex along curves
- Material constraints (different woods bend differently before splintering)
- Steaming/clamping workflow or its eased equivalent
- Real-time collision and buoyancy updates as the hull mesh changes

### Modular placement
- Free placement of pieces (planks, beams, decks, masts) with snapping where helpful
- No strict voxel grid — pieces fit where they geometrically make sense
- Fasteners and joinery as gameplay elements or as abstraction

### Materials & physics
- Wood species with different properties (oak vs. pine vs. teak — weight, strength, rot resistance)
- Iron, brass, canvas, cordage
- Optional: aging, weathering, repair states

### Validation / sea trials
- Float test — does it float, what's the waterline, what's the heel angle?
- Sail test — does it sail upwind, how does it handle?
- Stress test — what loads can it take before failure?
- Failure modes — visible structural failure, not just "you lose"

Sea trials piggyback on the shared [water-tech.md](water-tech.md) pipeline.

## "Sim but eased"

Open list — needs deeper discussion.

**Candidates for abstraction:**
- Bulk fastener placement (don't make players place every nail)
- Auto-rigging given mast and sail positions
- Templates / starter hulls to bend from rather than fully blank canvas
- Symmetry tools (mirror across keel)
- Numeric input for repetitive geometry

**Candidates to keep demanding:**
- Hull form decisions (this is the core design satisfaction)
- Structural decisions (where the ribs go, plank layout)
- Mast positioning and rigging design
- Material selection

## Multiplayer & sharing

- Collaborative building (multiple players on one ship project) — over P2P session
- Steam Workshop publish from Build mode; subscribers see the ship in their Sail and Build modes. See [interop.md](interop.md) for validation rules.
- Possibly competitive challenges (best handling, fastest hull, etc.)
- Sea trial spectating

Shared networking concerns in [multiplayer-infra.md](multiplayer-infra.md).

## Implementation direction (native prototype)

The clarified loop we're building toward, and how it maps to the native C++ prototype:

**Shape it → submit for launch → bake a profile → sail it differently.**
1. **Build** on the stocks at the shipyard: lay the **backbone** (keel → stem → sternpost → ribs — already real pieces), then **place wood** (planks) onto the frames. The player **makes the ribs / shapes the hull** freely — long & narrow, short & beamy, deep or shallow — the shape is theirs.
2. **Submit for launch** — a short **"calculating…" load**.
3. **Bake** the built geometry into a **`HullProfile`**: displacement, block coefficient `Cb`, waterplane, draft, **stability (GM ~ B²/T)**, and handling factors (**top speed ~ √L·fineness**, **turn ~ 1/L·√B**, drag, heel). *This is the "lots of math so the boats don't all act the same."*
4. **Sail** — Sail mode reads the profile: fine hull = fast, wide-turning, tender; beamy hull = slow, tight-turning, stiff/stable; deeper draft bites more. Ties into the existing weight-vs-buoyancy + founder model.

**Beyond the profile (big, separate increments):**
- **Freeform placement** — click-to-place each plank/rib to shape the hull; per-tradition validation (backbone before planking; shell- vs frame-first; clinker vs carvel). Then the bake reads the *actual placed geometry*, not just L/B/T sliders.
- **Walkable human character** — a third-person avatar you control on foot: walk the yard, walk the deck, climb aboard (Black Flag's seamless ship↔foot). New: character controller + camera + animation. Big.
- **Pulleys & ropes** — block-and-tackle: yard **cranes/gantries** hoist timbers & masts with visible rope + sheaves; the ship's **rigging** (halyards/sheets). Rope as a simple constraint/catenary; pulley as a hoist ratio. Big.
- **Per-section buoyancy sample points** baked from the hull → `computeFloatPose` rides/pitches per the real shape.

**Status (native prototype):** the backbone pieces (keel/stem/sternpost/ribs) are real; the shipyard build berth with a stand + orbit camera is in. **Next up: the `bakeHullProfile` + launch flow + hull-shape sliders**, then freeform placement, then the character, then rigging. See [next-steps.md](next-steps.md).

## Open questions

- Physics fidelity beyond v1 score-based model (distributed physics if rocking/heeling needs more)
- Freeform placement: full plank freedom vs snap-to-frame-station (probably snap-to-station).
- Launch gate: does the hull have to be **watertight/sealed** to pass the bake? A "seaworthiness" fail state is natural.
- Character scope: full traversal + climbing, or deck-walking first?
- Rope fidelity: cosmetic vs load-bearing (a snapped halyard drops the sail).
- Mod support · tutorial/onboarding (steep curve) · collaborative-building UX in P2P.

Consolidated in [holes.md](holes.md).
