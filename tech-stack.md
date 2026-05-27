# Tech stack

## Platform — Steam first (decided)

**Steamworks SDK** handles all multiplayer infrastructure (see [multiplayer-infra.md](multiplayer-infra.md)):
- Steam Datagram Relay + ISteamNetworkingSockets, Steam Lobbies, Steam Voice, Steam Cloud, Steam Workshop
- Friends, invites, achievements built in
- Costs: $100 Steam Direct (single product, single library entry) + 30% revenue share
- No ongoing infrastructure costs

## Engine — engineless, custom C++ (decided)

The final game target is an **engineless native C++ game using selected libraries**. Not Unreal, not Unity, not Godot. Not HTML.

"Engineless" here means **custom engine assembled from libraries**, not write-every-subsystem-from-scratch. Writing physics, windowing, audio, rendering, asset loading, UI, Steam integration, and networking from nothing would kill the project.

### Locked stack

| Layer | Pick | Notes |
|---|---|---|
| Language | **C++** | Steamworks-native, Jolt-native, ecosystem matches |
| Window / input | **SDL3** | Cross-platform foundation |
| Rendering | **bgfx** | Backend-agnostic; Vulkan/DirectX later if needed |
| Physics | **Jolt** | Modern, strong for rigid bodies and ships |
| UI / tools | **Dear ImGui** | Debug overlays, editor panels |
| Audio | **miniaudio** | Lightweight; FMOD later if needed |
| Serialization | **JSON** (prototype) | FlatBuffers / Cap'n Proto later if needed |
| Build | **CMake** | Standard |
| Platform | Windows-first, Steam-first | Per [multiplayer-infra.md](multiplayer-infra.md) |
| Networking (later) | **Steamworks SDR / ISteamNetworkingSockets** | Per [multiplayer-infra.md](multiplayer-infra.md) |

### Why C++ over Rust

- Steamworks is native C++; Rust adds FFI cost
- Jolt is C++; Rust would be wrapping it
- Game-dev tools, asset libs, debug tooling all assume C++
- Hiring help is easier with C++
- Rust's memory-safety wins matter less when every meaningful dep is C++ anyway

### What the web spike was for

The Three.js browser spike at [prototype/](prototype/) proved the vertical-slice loop (build → ship data → validation → Sail → buoyancy → damage/cargo, plus 11/11 model-spine self-tests). **It is not the production path.** Keep it around as reference for the math, JSON schema, and test cases. Do not extend it as if it were the production app.

## Status

Engine + language locked. Bootstrap order in [next-steps.md](next-steps.md).
