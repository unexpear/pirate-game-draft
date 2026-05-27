# Ship transfer & Workshop validation

Ships made in Build mode appear in Sail mode. Same product, same user, same install — no cross-product handoff. Three pathways:

1. **Local user folder** (default) — Build mode saves to a Steam Cloud–synced folder; Sail mode reads from it.
2. **Steam Workshop publish** (optional, from Build mode) — uploads the ship; other players subscribe; subscribed ships appear in their Sail mode (and Build mode for inspiration).
3. **Workshop download** (from Sail or Build) — one-click via Steam's Workshop UI.

A player who never touches Workshop loses nothing — their local Build-mode ships still work in Sail.

## File format requirements

- Stable, **versioned** schema with forward/backward compatibility across game updates
- Workshop-compatible (within Steam content size limits)
- Includes piece data (volume, mass, material, damage state) for runtime buoyancy + damage — overlaps with [water-tech.md](water-tech.md)
- Runtime conversion: build-time mesh → collision baked, LODs, buoyancy tier-grid baked

## Workshop validation

Run on upload AND on download. Failing ships don't publish; failing downloads don't load.

- Max piece count
- Max bounding box dimensions
- Max file size
- Structural validity — must actually float, must have helm + sails, etc.

Specific thresholds TBD — see [holes.md](holes.md).

## Notes

- Single Steam Workshop hosts all ships — see [multiplayer-infra.md](multiplayer-infra.md)
- Sketch the file format early — see [risks.md](risks.md). Simpler than the old two-product handoff, but still load-bearing once Build and Sail are both real.
