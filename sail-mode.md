# Sail mode (pirate sandbox)

## Vision

A multiplayer open-ocean pirate game where players sail, fight, dive, dredge, and hunt treasure across an explorable world of islands, caves, and shipwrecks. Sailing feels weighty and real, but the focus is on adventure, not engineering.

## Core gameplay loop

1. Set sail from a port or anchored position
2. Travel the open ocean — wind, weather, navigation
3. Encounter content — islands, wrecks, caves, sea creatures, other players
4. Engage — combat, exploration, diving, dredging, treasure recovery
5. Return to port — sell loot, repair, restock, upgrade
6. Repeat with progression unlocking new zones, ships, gear

## World content

- **Open ocean** — the connective tissue. Weather, wind, currents, day/night, fog.
- **Islands** — tropical, rocky, frozen, jungle variants. Some inhabited (NPC ports, quest hubs), some wild (resources, hidden caves).
- **Sunken ships** — explorable wrecks. Combine underwater rendering + interior exploration + treasure + sometimes creature lairs.
- **Caves** — both surface sea caves and fully submerged cave systems. Air management, claustrophobia, bioluminescent lighting.
- **Open-water encounters** — ghost ships, merchant convoys, naval patrols, storm zones.

## Core systems

### Sailing & ship physics
- Wave-based buoyancy on pre-made hulls (baked buoyancy volumes per ship class)
- Wind direction, sail trim, hull drag
- Anchor mechanics, docking
- Damage states affecting handling

### Naval combat
- Cannon ballistics — gravity, range, reload cycles
- Crew positions (helm, cannons, sails, lookout)
- Damage model — hull breaches, mast loss, sail damage, flooding
- Boarding mechanics for ship-to-ship infantry combat

### Swimming & diving
- Stamina/breath management
- Vertical movement, pressure depth limits
- Underwater visibility curve
- Loadout limitations underwater (no firearms below surface, etc.)

### Underwater rendering
- Fog falloff, light attenuation
- Caustics on surfaces near the surface
- Bioluminescence in caves and deep zones
- Post-process tint and refraction at the surface

See also [water-tech.md](water-tech.md) — the rendering pipeline is shared with Build mode's sea trials.

### Dredging
- Seafloor terrain deformation
- Sediment plumes, visibility reduction
- Tool variants (hand dredge, ship-mounted, mechanical)
- Loot scattered/buried in seabed, treasure caches

### Treasure hunting
- Treasure maps, dig sites, sunken caches
- Underwater retrieval mechanics
- Trade-in / appraisal system at ports

### Sea creatures
- Ambient — fish schools, sea turtles, jellyfish (atmospheric)
- Hostile — sharks, eels, hostile squid
- Set-piece — kraken, megalodon, leviathan-class encounters
- AI: flocking for ambient, behavior trees for hostile, scripted phases for bosses

## Ships

- **Pre-made ships** — designed in-house, balanced for combat and handling. Multiple classes (sloop, brigantine, galleon, etc.).
- **Build-mode ships** — any ship made in Build mode appears here automatically via the local user folder. Workshop subscriptions also show up. See [interop.md](interop.md).

## Progression

- TBD. Likely some combination of: ship upgrades, gear/loadout, navigation chart unlocks, faction reputation.
- Open question: do players have persistent characters? Persistent ships? Persistent worlds?

## Multiplayer

**P2P session-based** via Steamworks — no persistent shared world. See [multiplayer-infra.md](multiplayer-infra.md).

Persistent world feel comes from design (Sea of Thieves model): voyage events, leaderboards, crossover encounters.

**Seamless host migration** on host quit — 2–5 second pause with "Host migrating…" popup, then play resumes.

**Scene budgets**: ~15 player ships at Tier 2 + ~30 NPC vessels at Tier 0/1 in the same scene, run on independent budgets.

Other axes:
- Crew mechanics — multiple players per ship via Steam Lobbies, role-based gameplay
- PvP vs PvE — still open

## Open questions

- World seed: handcrafted, procedural, or hybrid?
- Within-session persistence (Steam Cloud covers player profile; world/run state TBD)
- PvP framing
- Monetization model
- Which Sea of Thieves design tricks to adopt for world-feel

Consolidated in [holes.md](holes.md).
