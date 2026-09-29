// Sea Trial — Steamworks façade. Public domain (The Unlicense).
//
// SEA_TRIAL_WITH_STEAM unset (the default): local stubs, no SDK headers.
// SEA_TRIAL_WITH_STEAM=1: call the real Steam API. The SDK is supplied at
// configure time and is not part of this repository.
#include "steam_platform.h"

#include <filesystem>

#if defined(SEA_TRIAL_WITH_STEAM) && SEA_TRIAL_WITH_STEAM
#include <cstdio>
#include "steam/steam_api.h"
#endif

namespace steam_platform {

namespace {

// Local stand-in for the slice's /user_ships/ folder. Absolute, under the
// process working directory, so saves land next to where the app was launched
// and tests can name the path without touching Steam Cloud.
std::string localUserShipsRoot() {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path base = fs::current_path(ec);
    const fs::path dir = (ec ? fs::path("user_ships") : (base / "user_ships")).lexically_normal();
    return dir.string();
}

#if defined(SEA_TRIAL_WITH_STEAM) && SEA_TRIAL_WITH_STEAM
bool g_inited = false;
bool g_active = false;
#endif

} // namespace

#if defined(SEA_TRIAL_WITH_STEAM) && SEA_TRIAL_WITH_STEAM

bool compiledWithSteamSdk() { return true; }

bool init() {
    if (g_inited) return true;
    g_inited = true;
    // SteamAPI_RestartAppIfNecessary is intentionally not called: it exits and
    // relaunches through the Steam client, which is the wrong default for a
    // local offline run of this prototype.
    if (!SteamAPI_Init()) {
        g_active = false;
        std::fprintf(stderr,
                     "steam_platform: SteamAPI_Init failed; continuing offline "
                     "(local user_ships).\n");
        return true;
    }
    g_active = true;
    return true;
}

void shutdown() {
    if (!g_inited) return;
    if (g_active) SteamAPI_Shutdown();
    g_active = false;
    g_inited = false;
}

void pumpCallbacks() {
    if (g_active) SteamAPI_RunCallbacks();
}

bool isSteam() { return g_active; }

std::string personaName() {
    if (!g_active) return {};
    ISteamFriends* friends = SteamFriends();
    if (!friends) return {};
    const char* name = friends->GetPersonaName();
    return name ? std::string(name) : std::string();
}

#else

bool compiledWithSteamSdk() { return false; }

bool init() { return true; }

void shutdown() {}

void pumpCallbacks() {}

bool isSteam() { return false; }

std::string personaName() { return {}; }

#endif

std::string userShipsRoot() {
    // Cloud-aware root is a later slice. Ships stay in the local folder
    // even after a real SteamAPI_Init succeeds.
    return localUserShipsRoot();
}

HookResult cloudSyncUserShips() {
    // TODO: ISteamRemoteStorage file sync of user_ships/*.ship.json.
    return HookResult::Unimplemented;
}

HookResult workshopValidate(const std::string& shipPath) {
    (void)shipPath;
    // TODO: upload and download validation per interop.md. No Workshop I/O yet.
    return HookResult::Unimplemented;
}

} // namespace steam_platform
