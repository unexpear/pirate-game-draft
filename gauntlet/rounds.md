# gauntlet rounds

## Round 0 — baseline critique (13 blind critics, 5 pieces)

Every piece scored **0 / critics say work_passes**. Convergent findings:

| piece | mostly-failing criteria | biggest gap (consensus) |
|---|---|---|
| water | crests tiled/undirected, foam blobs, hard horizon, tiled pattern | repeating tiled wave grid + scattered foam blobs |
| ship | box-raft hull, grey sail slab, hovers on water | grey placeholder sail (brightest element) + box-raft hull |
| composition | flat/muddy lighting, dead sky band | dead dark-navy sky + no directional sun (flattens everything) |
| island | flat green pad, no relief, tabletop scale | zero vertical relief — reads as a flat prop |
| build | near-black underexposure, no material read, poles block hull | severe underexposure (hull is a silhouette) |

**Cross-cutting root causes** (one fix helps many): (1) dead sky + no sun →
flat light [composition, build, ship]; (2) hard water/sky seam [water,
composition]; (3) grey sail [ship, composition]; (4) build underexposure.

### Round 1 plan — the atmosphere/lighting/sail cluster

Contained changes hitting 4 of 5 pieces:
- **Gradient sky** (warm pale horizon → deep navy zenith) replacing the flat clear colour.
- **Horizon fog** on the water — fade the sea into the sky colour at distance (kills the hard seam).
- **Warm hemispheric lighting** in the mesh + terrain shaders (warm sun diffuse + cool sky ambient, lifted floor) — gives form and lifts the near-black build scene.
- **Sail canvas** — grey slab → warm off-white.
- **Buoys** — retint the alarming red markers.

Deferred to Round 2 (geometry-heavy): water foam de-blobbing + wave de-tiling,
island vertical relief, hull bow/sheer reshape.

### Round 1 changes applied

- **Gradient sky** — new `sky_gpu` module + `{vs,fs}_sky.sc`: fullscreen warm-haze
  horizon → deep navy zenith, drawn behind everything (far-depth, order-independent).
- **Horizon fog** on the water (`fs_water` `u_fog`) — the sea dissolves into the
  sky colour at distance; sky + fog share the horizon colour so there's no seam.
- **Lower warm sun** — light direction `(0.4,0.85,0.35)` (85% up, left vertical
  faces dark) → `(0.5,0.6,0.62)` (~36° elevation) so vertical faces catch light.
- **Warm hemispheric lighting** in `fs_mesh` + `fs_terrain`: warm sun key + cool
  sky fill, lifted floor.
- **Canvas sail** — new `u_mat==3` path: bright warm cloth that stays creamy in
  shade; sail colour grey → warm cream.
- **Tuning after eyeballing the first r1 shot**: killed a specular blowout
  (pow 60·0.6 → 150·0.14), placed the warm sky band at the horizon line, warmed
  the horizon colour, lifted the near-black build scene (brighter structural
  woods/metal + higher floor).

First r1 shots confirmed live: sail reads as canvas, the sea fogs into a warm
horizon (no hard seam), the hull takes warm form, the build scene is readable.

### Round 1 result (fresh panel on r1 shots)

Objective before→after (same 13-critic panel, fresh context):
- **water**: horizon-seam criterion now **passes** (was 3 fails → 2 passes). Still
  fails the tiled-wave + blobby-foam criteria (geometry).
- **ship**: sail now warm, but still fails "canvas" on the *missing billow/curve*;
  the box-raft hull is the dominant remaining gap (geometry).
- **composition**: critics still want stronger directional contrast + cast shadows
  and read the focal hull as dark; framing shows little sky.
- **island**: still flat — the relief was too gentle to read.
- **build**: needs real shipyard geometry + reframe; lighting lifted but the
  hull/poles still read dark from the orbit angle.

Every surviving failure is **geometry/relief** — exactly the Round 2 backlog.

### Round 2a changes (contained — no model-spine/self-test risk)

- **Island relief** — beach shelf 3.2→4.5, hills 12/6 → **30/16/15** (three summits)
  + stronger noise; colour bands raised (rocky crown above ~22). Result: a real
  hill with a green slope and rock crown rising **above** the buildings — mass,
  silhouette and scale, no longer a flat pad.
- **Crest-steepness foam** — foam now keys on surface slope (`length(N.xz)`) × a
  little height, so it streaks the sharp crest faces instead of scattering as blobs.
- **More light contrast** — mesh key `sun·ndl` 0.72→0.90 with a lower fill floor,
  for stronger lit/shadow facet contrast (the composition "flat light" note).

Live-confirmed: the island now reads as a real landmass; foam clings to crests.

### Round 2 backlog (big geometry — each its own focused round)

- **Gerstner wave de-tiling** — sum 4–6 non-harmonic Gerstner trains (model spine
  `sampleWater` + `vs_water` + wider uniform arrays; keep the self-tests green).
  This is THE fix for the "tiled grid" water reads (criteria 1 & 5).
- **Hull bow/sheer reshape** — taper the fore/aft plank rows to a stem, raise the
  sheer, add a V-keel so the hull reads as a boat, not a box-raft (model spine
  `makeShipFromConfig` + re-verify the `HullProfile` bake self-tests).
- **Sail billow** — curve the sail quad so it reads as filled canvas.
- **Build scene** — visible stocks/ways/scaffolding, reframe off the foreground poles.
- **Cast/contact shadows** — the composition depth cue the critics keep asking for.
