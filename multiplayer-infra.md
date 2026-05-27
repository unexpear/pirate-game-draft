# Multiplayer infrastructure

Steam first — Steamworks SDK handles everything. See [tech-stack.md](tech-stack.md) for the platform decision.

## Architecture

**P2P session-based** via Steam Datagram Relay + Steam Lobbies. No persistent shared world.

Sea of Thieves operates under the same constraint and replicates world feel through design: voyage events, leaderboards, crossover encounters. Lean on these techniques rather than trying to fake a persistent server.

## Steamworks components

| Component                       | Use                                                   |
|---------------------------------|-------------------------------------------------------|
| Steam Datagram Relay (SDR)      | Free global P2P relay; NAT traversal, IP hiding       |
| ISteamNetworkingSockets         | Networking API on top of SDR                          |
| Steam Lobbies                   | Matchmaking, crew assembly                            |
| Steam Voice                     | In-game voice chat                                    |
| Steam Cloud                     | Player profile + Build-mode ship folder sync          |
| Steam Workshop                  | Ship sharing; validated on upload + download          |
| Friends, invites, achievements  | Built in                                              |

## Host migration

Sessions don't end when the host leaves — **seamless handoff** to another player, brief-pause UX (2–5 seconds, "Host migrating…" popup). Sea of Thieves bar, not invisible-AAA bar.

Architecture requirements:
- **Eventually-consistent client state** — every client carries enough recent state to step into the host role
- **State snapshots broadcast during play** — frequent enough that mid-action state (sails mid-trim, cannons mid-fire, creature mid-attack) survives the handoff
- **Host election** — remaining clients agree on the new host (best ping + stable connection; Raft or simple consensus voting both fit)
- **Authority transfer** — every networked entity (ships, NPCs, projectiles, treasure chests) re-points to the new host atomically

SDR makes the transport survive — the relay is independent of any specific peer.

Costs: more networking complexity than session-ends-on-quit, slightly higher bandwidth (non-host clients receive more state than strictly necessary), and edge cases (new host drops fast after migration, no remaining client connects well enough, intentional-quit vs. crash).

## Scene budgets

Player ships and NPC vessels run on **independent** physics/sampling budgets:
- ~15 player ships at Tier 2 (3×3 sampling) — full pitch + heel fidelity
- ~30 NPC vessels at Tier 0/1 — simpler buoyancy, baked behavior

Single scene carries both without competing. See [water-tech.md](water-tech.md) for the tier system.

## Implications

- Steamworks integration designed in from day one, not bolted on later
- Save data format assumes Steam Cloud as storage layer (small, sync-friendly)
- Ship file format must be Workshop-compatible — see [interop.md](interop.md)
- No persistent shared world — design must accommodate session-based play

## Risk

Physics sync across P2P clients still matters, and host migration adds complexity. Mitigated by:

- Weight-vs-buoyancy model (no exploding rigid-body state) — see [water-tech.md](water-tech.md)
- Deterministic Gerstner waves (clients agree on wave field by computation, not replication)
- Eventually-consistent client state for migration readiness

Worth a two-player sailing prototype early, plus a migration test once basic networking is up.

## Open questions

- Which Sea of Thieves design tricks to adopt for world-feel (voyage events, leaderboards, crossover encounters)
- Collaborative-building (Build mode) inside a P2P session
- Crossplay if non-Steam platforms come later
- Host-migration edge cases: cascading host drops, no eligible new host, distinguishing intentional-quit from crash
