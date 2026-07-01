# Reference — Assassin's Creed IV: Black Flag (naval design)

The benchmark for modern pirate-sailing feel (2013, ~34M players). Its sequel-of-sorts **Skull and Bones** (2024) tried to make the naval layer into a standalone game and largely failed — the contrast is the most useful lesson here.

## How it worked (mechanics)

- **Naval was the core, not a side activity** — seamlessly integrated with on-foot stealth/parkour/melee. You could walk to the helm and sail off, or anchor and swim to shore, with **no loading screens** between ship and land.
- **Sailing:** multiple speeds — full sail (travel), half/combat sail (maneuver), anchored. Wind and positioning matter. Ship handling feels **weighty**.
- **Directional cannons** tied to where the camera/ship faces: broadside (sides), chain shot (front, to slow/immobilize), mortar (long lob), fire barrels (rear), heavy shot (close-range devastating), plus ram.
- **Boarding loop (the crown jewel):** damage an enemy ship to critical → pull alongside → hooks thrown → **melee combat on the deck** (kill quota / objectives) → reward: repair, crew, cargo. Real infantry combat, not a cutscene.
- **Fort assaults** and **legendary-ship** boss fights.
- **Exploration sandbox:** hidden coves, **underwater diving** for sunken wrecks/treasure, islands, hunting/whaling, collectibles.
- **Sea shanties:** the crew sings during travel — enormous "living aboard a ship" atmosphere (35 to collect).
- **Progression via plunder:** raid → sell loot → upgrade the ship → take on tougher targets.

## What players liked

- Naval combat with real **depth** — positioning, timing, crew, tactics over brute force.
- **Boarding + melee** — the standout, most-cited feature.
- **Seamless ship ↔ foot ↔ swim** integration ("best of both worlds").
- **Exploration & discovery** — coves, sunken treasure, legendary ships.
- **Sea shanties / audio / atmosphere.**
- **Freedom** — sail, fight, or explore whenever; open sandbox, not linear.
- Weighty, satisfying **ship handling**.

## What players disliked

- **Eavesdropping & tailing missions** — heavily criticized; *especially* bad when extended to **naval tailing** (following a ship undetected). Widely said they "should have been removed from the series."
- **Clunky, poorly-defined stealth controls.**
- Some **repetitive/tiring story missions**.
- **Modern-day segments** — mixed reception, felt like a distraction from the pirate fantasy.
- Common open-world gripes: combat can get easy; the map can feel like a collectathon; the plunder→upgrade loop can turn grindy late.

## The Skull and Bones lesson (the big one)

Ubisoft spent ~11 troubled years turning Black Flag's naval into a **sailing-only** standalone. To do it they **removed** boarding parties, sword fighting, crow's-nest leaps, all on-foot/land gameplay, real exploration, and lively ports. Boarding became a **short cutscene with no melee**.

Result: widely panned. The recurring verdict —
- *"Removed the best parts of Black Flag only to make the one system you kept even worse."*
- *"Failed at 10 things instead of succeeding at one."*
- Dead towns (buy commodities, craft a ship, nothing else), ubiquitous loading screens, grind, and even the sailing "doesn't feel as good as Black Flag."

**Core takeaway:** the pirate fantasy is the *combination* — sailing **plus** boarding-melee **plus** on-foot exploration **plus** lively ports **plus** discovery. Strip it to sailing-only and the magic dies. "It's about being a pirate, not just a pirate captain."

## Implications for this project

Directly bears on our decisions (see [../decisions.md](../decisions.md), [../concept.md](../concept.md), [../game-a-pirate-sandbox.md](../game-a-pirate-sandbox.md)):

1. **Keep the combination.** Sail mode already spans sailing + combat + boarding + diving + treasure + islands. Black Flag confirms that breadth *is* the fantasy; Skull and Bones confirms stripping it fails. → Reinforces **not** shipping a sailing-only product, and the single-app "modes gated by UI, not purchase" call over two thin products.
2. **Boarding melee is the crown jewel** — build the disable → board → **on-deck melee** loop for real; never reduce it to a cutscene. (We already list boarding as ship-to-ship infantry combat — make it central.)
3. **Seamless transitions** (ship ↔ swim ↔ land, no loading) are a big part of the magic. Design against loading-screen fragmentation. Our P2P session model must not chop the world into loads.
4. **Exploration & discovery drive the loop** — coves, sunken wrecks, diving. Our dredging/diving/treasure systems align; lean in.
5. **Audio/atmosphere punches above its weight** — shanties + crew chatter are cheap and hugely immersive. Worth an early, deliberate pass.
6. **Avoid the anti-patterns:** no tailing/eavesdropping filler, no clunky forced-stealth mini-games that fight the core loop.
7. **Watch the grind:** plunder→upgrade is proven, but keep targets and encounters varied so the late game doesn't flatten.
8. **Ship "weight"/handling makes sailing feel good** — this is exactly what our weighty **buoyancy + wave** work (already built in the native prototype) is for. On-theme; keep the sailing feel physical.

## Sailing controls (researched, matched in the native prototype)

Triangulated across the Ubisoft e-manual/pro-tips, Orcz/Fandom wikis, PC & Xbox control tables, and player forums/reviews. Black Flag's sailing is an **arcade "gearbox," not an analog throttle**:

- **Four discrete sail states**, slowest→fastest: **anchored** (no sail — drift/stealth) · **half sail** (the practical cruise + combat speed) · **full sail** (fast, wide turns) · **travel speed** (open-water overdrive above full sail).
- **W = raise sail one step, S = lower one step** (gamepad RT/LT). Reaching **travel speed** takes a **hold** of accelerate at full sail — the tap-vs-hold split is the authentic detail. **No reverse** — S only coasts the hull to a stop; to face about you steer through an arc or stop-and-pivot.
- **Turn radius is inversely tied to speed** — *the* convention players internalize. Half sail turns tight; full/travel turn wide; a **fully stopped ship pivots fastest** (the "stop-to-turn" combat trick). Speed *is* the handling control.
- **Weapon is chosen by camera facing**, not a weapon key: side = broadside, front = chain shot, rear = fire barrels, up = mortar. **Space = brace** (timed damage negation). *(For later, when combat lands.)*
- **Wind is essentially irrelevant** in the real game (a common realism gripe) — it's wind-neutral arcade handling.

**How our prototype maps it** (native `main.cpp` / `ship_mesh.cpp`):

- Spawns at **half sail** (the cruise baseline), not a dead stop.
- **W** taps up anchored→half→full; **holding W at full latches TRAVEL SPEED** (a distinct top gear, ~1.5× full). **S** taps down and coasts to a stop — **no reverse**.
- Turn rate `= 1.35·(1 − 0.62·speedFrac)` → ~77°/s pivot at a stop vs ~48°/s at travel (verified live). Stop-to-turn works.
- The **visible sail reefs with the state** — furled to a bare mast when anchored, half-height at half sail, full canvas at full/travel — which is literally what BF's "sail states" are.
- **Deliberate deviation:** we *keep* a wind mechanic (speed best downwind, worst upwind, with a HUD readout). Black Flag is wind-neutral, so this is an **intentional enhancement**, not fidelity — flagged so it stays a conscious choice. See [../holes.md](../holes.md).
- **Broadside cannons** fire (Space) in a real **two-sided duel**: the enemy is an AI warship that crowds sail to close, turns to present its broadside, and **fires back** — its shot floods *your* hull, so either ship can be sunk (win/lose). Cannonballs arc under gravity. The firing **side is auto-picked toward the enemy**; Black Flag's real **camera-direction** weapon selection (side = broadside, front = chain, rear = fire barrels, up = mortar) still waits on a free camera.
- Not yet mapped, in the order they matter: **boarding** (the crown jewel — disable → alongside → capture, with on-deck melee), **brace** (timed damage negation vs the enemy's return fire), then camera-direction weapon selection, spyglass, mortar, shanties.

## Sources

- [Black Flag — Wikipedia (reception)](https://en.wikipedia.org/wiki/Assassin's_Creed_IV:_Black_Flag)
- [Black Flag is still a more complete pirate game than Skull and Bones — The Nerd Stash](https://thenerdstash.com/11-years-later-black-flag-is-still-a-more-complete-pirate-game-than-skull-and-bones/)
- [AC Black Flag's potential was squandered for Skull and Bones — Screen Rant](https://screenrant.com/assassins-creed-black-flag-skull-bones-spinoff-bad/)
- [Skull and Bones review — Digital Trends](https://www.digitaltrends.com/gaming/skull-and-bones-pc-review/)
- [Black Flag deep-dive analysis — Howik](https://howik.com/assassins-creed-black-flag-analysis)
- [Naval battle tips — The Average Gamer](http://www.theaveragegamer.com/2013/12/17/naval-battle-tips-assassins-creed-iv-black-flag/)
