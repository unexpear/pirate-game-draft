# pirate visual gauntlet

Applies the [gauntlet-loop](../../gauntlet-loop/README.md) method to the pirate
game's rendered look: fresh **blind critics** grade a clean screenshot of each
piece against a concrete **pass/fail answer key**, name the single biggest gap,
and a fix goes back to the builder. Repeat until the criteria pass.

Why an answer key instead of a fetched reference image: the game is a stylized
box-geometry prototype, so a photoreal AAA screenshot is the wrong bar (it would
"win" every round and teach nothing). This is the method's documented
**no-benchmark variant** — a decision map / pass-fail answer key stands in for
the reference (see [gauntlet-loop/concept.md](../../gauntlet-loop/concept.md#the-known-flaw-and-the-fix)).

## The enabler: `--shot`

`sea_trial` can now render a clean frame headlessly (no UI) to PNG, so a critic
can *run and see* the work rather than imagine it:

```
sea_trial.exe --shot <path.png> [--scene sail|build] [--pos X Z] [--head rad] [--walk] [--shot-frames N]
```

- `--shot <path>` — capture a PNG and exit (hides the ImGui panel + wind vane).
- `--scene build` — the shipyard berth (full hull on the stocks); default `sail`.
- `--pos X Z` / `--head rad` — pose the ship for framing.
- `--shot-frames N` — frames to render before capture (default 90; lets waves settle).

Implemented in [native/src/screenshot.cpp](../native/src/screenshot.cpp) (a bgfx
`CallbackI` that encodes the BGRA backbuffer to PNG via bimg) + arg handling in
[native/src/main.cpp](../native/src/main.cpp). PNGs are near-uncompressed, so a
1280x720 frame is ~3.6 MB.

### The shots this run graded

| piece | shot | framing |
|---|---|---|
| water | `shots/piece_water.png` | open ocean, `--pos 300 320 --head 1.2` |
| ship | `shots/piece_water.png` | ship prominent (sail up) |
| composition | `shots/piece_overall.png` | water + island + ship, `--pos -55 20 --head 0.35` |
| island | `shots/piece_island.png` | island frontal, `--pos 0 42 --head 0` |
| build | `shots/piece_build.png` | `--scene build` |

## The answer keys (decision map)

Each piece is judged on 5 binary criteria; a criterion passes only if the frame
clearly satisfies it. Full text lives in the workflow script; the pieces are:

- **water** — directional lit swells; crest foam (not blobs); depth-based colour;
  hazy horizon (no hard seam); living surface (not tiled).
- **ship** — plausible hull form (not a box raft); canvas-looking sail (not a grey
  slab); legible mast/rigging; material richness; sits *in* the water.
- **composition** — clear focal subject + depth; consistent form-giving light;
  cohesive pirate palette; atmospheric sky; reads as a game, not an engine test.
- **island** — irregular coastline; beach→veg→height transition; silhouette
  interest; clean land/water meeting; sense of place & scale.
- **build** — adequately lit (not near-black); reads as a working shipyard;
  distinct materials; hull well framed (not blocked by poles); place & scale.

## The loop

1. `--shot` each piece → `shots/`.
2. Run the critic panel: `Workflow` `pirate-visual-gauntlet` — N fresh blind
   critics per piece (diverse lenses: form / colour+light / atmosphere), each
   returns per-criterion pass/fail + the single biggest gap + one concrete fix.
   Fresh context per agent = the "blind, never-saw-the-build" property the
   gauntlet needs; the builder never grades its own work.
3. Aggregate → the mostly-failing criteria and most-cited gaps are the worklist.
4. Builder applies the top fix, rebuilds, re-captures.
5. Re-run critics (fresh again). Stop when the criteria pass (or budget).

Results of each round are logged in [rounds.md](rounds.md).
