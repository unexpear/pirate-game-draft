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

### Round 2 changes (the geometry backlog — all of it)

- **Gerstner waves** — `makeWaveField` now returns **6 non-harmonic trains** (spread
  directions, a long dominant swell + cross-chop) and `vs_water` sums them as
  **Gerstner** waves (horizontal crest-pinch + analytic normal), so crests sharpen
  and roll directionally instead of a rounded sine lattice. `sampleWater` (buoyancy)
  keeps its vertical sum over all 6 — **62/62 self-tests stay green**. This is THE
  fix for the "tiled grid" reads; live-confirmed the lattice is gone.
- **Hull bow/sheer reshape** — `makeShipFromConfig` planks + ribs now follow a skewed
  beam profile `sin(pi * u^0.82)` that tapers the strakes to a **stem point** at the
  bow and a fine stern (max beam slightly aft), with a **sheer line** rising to the
  ends and the strakes toed inward. `bakeHullProfile`/`getShipStats` read bounds +
  volumes (not positions), so the reshape is purely visual and the bake self-tests
  are untouched. Live-confirmed: a boat, not a box-raft.
- **Sail billow** — the sail is drawn as 7 vertical strips bellied to leeward in a
  parabola (`u_mat==3` canvas), so it reads as a filled, curved sail with panel seams.
- **Shipyard geometry + reframe** — gantry cranes set wide (±15 → ±23) so they frame
  rather than block; the stocks now read as a cradle (keel blocks + 5 shoring posts a
  side + angled shores + fore-and-aft scaffold rails + ground ways running to the
  water). `--head` drives the build-shot orbit for a 3/4 framing.
- **Contact shadows** — `ship_mesh::renderShadow` draws a soft alpha-blended dark quad
  under each hull (player, enemy, ship-on-stocks) to ground it; `fs_mesh` now honours
  per-draw alpha (all opaque draws pass 1.0).

### Round 2 result (fresh panel on r2-final shots)

Objective before→after across the whole run:
- **water: 0/3 → 3/3 PASS (all 5 criteria)** — the Gerstner rewrite fixed the "tiled
  grid" that failed every prior round. The only critic notes were `[work_passes]`
  polish (add sun-glint, sharpen foam).
- **ship**: the "box-raft hull" complaint **dropped out of the biggest-gaps entirely**
  (the reshape landed) — the remaining fail is the sail now being too small vs the mast.
- **island: 0 → 2 pass** — relief/silhouette now read; remaining fail is a too-thin
  beach / hard shoreline.
- **build: 0 → 1 pass** — improved but still underexposed and the stocks read weakly.
- **composition**: still fails lighting + sky — the critics revealed the cause: **fog
  was only on the water, so distant land never receded** (no aerial perspective).

### Round 2b polish (contained — targeting the exact surviving notes)

- **Aerial-perspective fog on land** — `fs_mesh` + `fs_terrain` now fog toward the
  horizon colour with distance (using the `u_camPos`/`u_fog` water already sets), so
  the island + buildings recede into haze instead of reading at full saturation up
  close. This is the composition depth cue the critics kept flagging.
- **Sail scale-up** — the sail now fills ~2/3 of the mast height and is broader
  (`mastH*0.66` tall, `wid*1.7` wide) — the hero silhouette, not a postage stamp.
- **Wider island beach** — gentler near-shore ramp + a broader wet-sand/dry-sand
  colour band, so the shore reads as a beach rooted in the sea, not a hard green edge.
- **Planked dry-dock deck** — the build berth's dark slab → a lit planked-timber deck.
- **Water sun-glint** — a brighter, slightly wider specular track toward the light.

Deferred (the one genuinely big item): full **cast/shadow-mapped shadows** (contact
shadows are in; projected shadows are a larger feature).

### Round 3 result + the root-cause fix (daylight coherence)

The r3 panel held **water at 3/3 pass** and moved **ship to 3/5**, but composition,
island and build kept failing on the SAME words every round: *underlit, murky, dark
dusk sky over bright day water, flat.* The root cause was finally clear — the whole
palette was a **dusk look** (dark navy sky + low warm sun + moderate ambient), which
read as incoherent against the bright water and buried land/ships/dock in shadow.

Round 3 fix — a coherent **daylight** shift:
- **Bright day sky** — navy zenith → day-blue `(0.28,0.45,0.68)`, warm pale daylight
  horizon; water fog + sky share the pale horizon so it all reads as one sunny moment.
- **Lifted exposure** — mesh/terrain ambient floors raised and the sun brightened, so
  nothing sits near-black while a strong key keeps form.
- **Fog pushed back** — starts at 0.5·far (was 0.35), so the island keeps its relief
  contrast instead of hazing into a flat pad.
- **Pitch/heel clamp** — `computeFloatPose` clamps trim to ~±8°/±13°, so the hull rides
  a steep Gerstner swell instead of spearing the bow under (the "diving ship" note).

Live-confirmed: a bright, coherent daylight pirate sea — island fully legible with
hill + beach + buildings, the build berth a lit planked dry-dock, the hull lit warm.
62/62 self-tests throughout. Capstone panel re-run on r4-final.

## Arc summary (Round 0 → final)

| piece | R0 | after |
|---|---|---|
| water | 0/3 | **3/3 pass, 5/5 criteria** (Gerstner) |
| ship | 0/3 | box-raft gone; boat hull + big canvas sail (remaining: waterline foam/wake) |
| island | 0/2 | real hill + rock crown + wide beach (relief reads; some critic taste variance) |
| composition | 0/3 | coherent daylight, aerial haze, atmosphere (remaining: cast shadows) |
| build | 0/2 | lit planked dry-dock, visible cradle, reframed off the poles |

The one big feature the critics still want and that remains genuinely deferred:
**shadow-mapped cast shadows** (and hull-intersection foam/wake) — larger systems,
each its own focused build.

## Round 4 — the two deferred big features

Built both blockers the panels isolated, then re-ran the panel.

- **Waterline foam + bow wave + wake** (`fs_water` `u_ship`/`u_shipDyn`): the hull
  sits at the scene origin oriented by heading, so the water shader draws a foam
  ring hugging the hull ellipse at the waterline, a bow wave narrowing to the
  stem, and a spreading V-wake that fades astern (scaled by speed). The ship now
  displaces water instead of clipping/hovering. Plumbed heading + hull footprint
  + speed through `water_gpu::render` and `ship_view::render`.
- **Shadow-mapped cast shadows** (new `shadow_gpu` module + `{vs,fs}_shadow.sc`):
  a two-pass directional shadow map focused on the ship — the sun renders the
  hull + mast + rigging + sail depth into a 1024² R32F target from an orthographic
  view (view 0, before the scene), and `fs_water`/`fs_mesh`/`fs_terrain` compare
  each fragment's light-space depth (3×3 PCF) to darken what the ship occludes.
  Real projected shadow on the sea + self-shadow on the hull for form. Replaced
  the old contact-shadow blobs. Ship-focused coverage (radius 30 around the hull);
  the distant island is outside the map (a known scope limit — a cascade or wider
  map would shadow the whole scene).

62/62 self-tests. Live-confirmed: the ship casts a real hull+sail shadow on the
water, sits in a foam ring with a bow wave and V-wake. Panel re-run on r5-final.
