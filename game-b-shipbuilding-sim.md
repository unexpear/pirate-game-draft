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

## Open questions

- Physics fidelity beyond v1 score-based model (distributed physics if rocking/heeling needs more)
- Mod support
- Tutorial path — this is a steep learning curve and onboarding matters
- Collaborative-building UX in a P2P session model

Consolidated in [holes.md](holes.md).
