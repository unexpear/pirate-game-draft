# Tech stack

## Platform — Steam first (decided)

**Steamworks SDK** handles all multiplayer infrastructure (see [multiplayer-infra.md](multiplayer-infra.md)):
- Steam Datagram Relay + ISteamNetworkingSockets, Steam Lobbies, Steam Voice, Steam Cloud, Steam Workshop
- Friends, invites, achievements built in
- Costs: $100 Steam Direct (single product, single library entry) + 30% revenue share
- No ongoing infrastructure costs

Engine pick should weigh Steamworks integration quality.

## Engine vs. engineless

Open: deciding between custom engine, pure no-engine, or pivoting to an existing engine.

## If proceeding engineless

- Graphics API: OpenGL / Vulkan / DirectX / wgpu — picked once and committed
- Language: C++, Rust, or Zig as plausible choices
- Physics library: Jolt, Bullet, or Rapier (writing physics from scratch is not advised given scope)
- Audio: FMOD, Wwise, or a lighter-weight alternative
- Asset pipeline: needs to exist; this is normally an engine job

## If reconsidering an engine

Unreal and Unity remain the strongest options for this kind of project.

## Status

This is the first decision gate — it blocks everything else. See [next-steps.md](next-steps.md).
