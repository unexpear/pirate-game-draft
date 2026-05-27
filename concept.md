# Concept

A **single Steam app with two modes** inside. One library entry; main menu chooses **Sail** or **Build**. Complexity is gated by UI, not by purchase — players who only want to sail never have to engage with the build interface.

- **Sail mode** *(working title TBD)*
  Open-world pirate gameplay: sailing, combat, treasure hunting, dredging, swimming, sea creatures. The pirate fantasy.
- **Build mode** *(working title TBD)*
  Simulator-grade ship construction with board-bending, modular placement, and physics validation. The engineering layer.

Multiplayer throughout. Ships made in Build appear in Sail via a local user folder, optionally published to Steam Workshop. See [interop.md](interop.md).

## Audience

- **Sail-only** players: broader audience, gameplay loops, exploration, combat. Never need to touch Build.
- **Build players**: narrower, more dedicated audience. Enjoy systems-rich games like Stormworks, From the Depths, Kerbal Space Program.
- Same product, same price, no upgrade. The choice is which menu button to press.

## Why one product, two modes

- One Steam app = one library entry, one Workshop, one set of achievements, one $100 Direct fee
- Build being UI-optional achieves the previous split goals (Build players never resent action layer, Sail players never resent engineering wall) without shipping and marketing two products
- Workshop unifies — ships published from Build are sailed in Sail, no cross-product validation layer needed
- Precedents: Garry's Mod (Sandbox + game modes), Minecraft (Survival + Creative), Roblox

## Reversed from prior round

Prior direction was two separate Steam products (Game A: Pirate Sandbox; Game B: Shipbuilding Sim). That introduced two Direct fees, separate Workshops, hard cross-product references, and a marketing problem ("buy the other game to design your own ships"). Single product collapses all of that.
