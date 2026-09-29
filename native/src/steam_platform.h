// Sea Trial — Steamworks façade. Public domain (The Unlicense).
//
// Steamworks is the one proprietary dependency. It stays an optional bolt-on:
// the default build compiles this header against local stubs and never sees
// the SDK. Define SEA_TRIAL_WITH_STEAM and point CMake at a local SDK to link
// the real Steam API (see native/steam.md). No Steam headers leak past here.
//
// All calls are main-thread, same as the Steamworks rule for SteamAPI_*.
#pragma once

#include <string>

namespace steam_platform {

// Result of a hook that is named now and implemented later.
enum class HookResult {
    Unimplemented, // no network in this slice
    Ok,
    Failed,
};

// True when this binary was linked against the Steamworks SDK.
// The default (public) build returns false.
bool compiledWithSteamSdk();

// Stand the façade up. Stubs succeed and stay offline. With the SDK linked,
// a missing Steam client also leaves the process offline — init still
// succeeds so the game can run on the local user_ships folder.
// Idempotent.
bool init();

// Release a live Steam session. Safe to call more than once, and safe if
// init never connected.
void shutdown();

// Dispatch pending Steam callbacks. Once per frame from the main loop.
// No-op when offline.
void pumpCallbacks();

// True only while a real Steam API session is up.
bool isSteam();

// Persona from ISteamFriends. Empty when offline.
std::string personaName();

// Where Build/Sail read and write player ships.
// This slice always returns the local directory `<cwd>/user_ships`, including
// when Steam is connected. A Steam Cloud-backed path comes later.
std::string userShipsRoot();

// TODO: sync user_ships/ through ISteamRemoteStorage (or Steam auto-cloud).
// No network in this slice — always Unimplemented.
HookResult cloudSyncUserShips();

// TODO: Workshop upload/download gate (interop.md: piece cap, bounds, file
// size, must float, helm + sails), run on upload and on download.
// No network in this slice — always Unimplemented. `shipPath` is the local
// ship file the future check will read.
HookResult workshopValidate(const std::string& shipPath);

} // namespace steam_platform
