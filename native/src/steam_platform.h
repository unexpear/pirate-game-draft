// Sea Trial — Steamworks façade. Public domain (The Unlicense).
//
// Steamworks is the one proprietary dependency. It stays an optional bolt-on:
// the default build compiles this header against local stubs and never sees
// the SDK. Define SEA_TRIAL_WITH_STEAM and point CMake at a local SDK to link
// the real Steam API (see native/steam.md). No Steam headers leak past here.
//
// All calls are main-thread, same as the Steamworks rule for SteamAPI_*.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace steam_platform {

// Result of a façade call.
enum class HookResult {
    Unimplemented, // ran as far as this build can (Workshop submit with no session)
    Ok,
    Failed,
};

// True when this binary was linked against the Steamworks SDK.
// The default (public) build returns false.
bool compiledWithSteamSdk();

// Stand the façade up. Stubs succeed and stay offline. With the SDK linked,
// a missing Steam client also leaves the process offline — init still
// succeeds so the game can run on the local user_ships folder.
// Does not talk to Steam Cloud; call cloudSyncUserShips for that.
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
// Always the local directory `<cwd>/user_ships`, including while a Steam
// session is up. Steam Cloud is not a directory the game opens: it is a
// mirror of the `.ship.json` files in this folder, updated by
// cloudSyncUserShips.
std::string userShipsRoot();

// Copy `user_ships/*.ship.json` to and from Steam Cloud (ISteamRemoteStorage).
//
// OFF, or ON with no live session: local-only success. Nothing is uploaded
// and the folder is not modified.
// ON with a live session: list, read, and write ship files under the cloud
// prefix `user_ships/`. See planCloudFile for which copy wins. Deletes are
// not propagated. Synchronous; ship files are small.
//
// Ok means the call finished and every ship file that needed a transfer
// was transferred (or there was nothing to do). Failed means at least one
// file could not be transferred; files that succeeded are already written.
HookResult cloudSyncUserShips();

// One line describing the last cloudSyncUserShips call. Empty until the
// first call. Safe to show in the debug panel.
std::string cloudSyncMessage();

// Which way one ship file should move. bytesEqual is consulted only when
// both copies exist: identical bytes stay put even if the timestamps differ.
// When the bytes differ, the newer Unix mtime wins, and a tie keeps the
// local file (the working copy on this machine) and uploads it.
enum class CloudFileAction {
    Unchanged,
    Upload,
    Download,
};

struct CloudFileFacts {
    bool localPresent = false;
    bool remotePresent = false;
    bool bytesEqual = false;
    std::int64_t localUnixSeconds = 0;
    std::int64_t remoteUnixSeconds = 0;
};

CloudFileAction planCloudFile(const CloudFileFacts& facts);

// A single path segment ending in `.ship.json`. Rejects separators, `..`,
// control characters, and names longer than 128 bytes so a cloud listing
// cannot write outside userShipsRoot.
bool isUserShipFileName(const std::string& basename);

// Cloud object name for a local ship file: `user_ships/<basename>`, with
// A–Z folded to lowercase. Steam Cloud stores names in lowercase.
// Empty when the basename is not a ship file.
std::string cloudObjectName(const std::string& basename);

// If `cloudName` is one ship object under `user_ships/`, write its basename
// and return true. Other cloud files are ignored.
bool cloudObjectBasename(const std::string& cloudName, std::string& basename);

// One file-level or structural problem from a Workshop check.
// `code` is stable (MISSING_HELM, FILE_TOO_LARGE, NOT_SUBMITTED, …).
// `message` is safe to show in the debug panel.
struct WorkshopIssue {
    std::string code;
    std::string message;
};

// Local half of "validated on upload and on download" (interop.md).
// Reads a `.ship.json`, then runs the ship validator: schema, pieces, helm,
// sails, mass, volume, float, piece cap, bounds, plus a 1 MiB file-size gate.
//
// Ok — the file passed. `code` is "OK". Warnings may still be set.
// Failed — `code` is the first error. `errors` lists every code.
// Does not publish, and does not return Unimplemented.
struct WorkshopValidateResult {
    HookResult result = HookResult::Failed;
    std::string code;
    std::string message;
    std::vector<WorkshopIssue> errors;
    std::vector<WorkshopIssue> warnings;
};

WorkshopValidateResult workshopValidate(const std::string& shipPath);

// Validate, then submit only when a Steam session is up.
//
// Failed — the local check failed, or a live submit failed. Nothing new is
// published when the check fails.
// Unimplemented — the check passed and this process has no Steam session.
// `code` is "NOT_SUBMITTED". Nothing was published.
// Ok — a live session created one new private Workshop item. `code` is
// "PUBLISHED". This slice does not remember the item id, so the next call
// creates another item.
WorkshopValidateResult workshopPublish(const std::string& shipPath);

// One line from the last workshopValidate or workshopPublish. Empty until
// the first call.
std::string workshopMessage();

// Result of that same call. Unimplemented until the first call.
HookResult lastWorkshopResult();

} // namespace steam_platform
