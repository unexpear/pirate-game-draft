// Sea Trial — Steamworks façade. Public domain (The Unlicense).
//
// SEA_TRIAL_WITH_STEAM unset (the default): local stubs, no SDK headers.
// SEA_TRIAL_WITH_STEAM=1: call the real Steam API. The SDK is supplied at
// configure time and is not part of this repository.
//
// Steam Cloud, when a session is up, mirrors user_ships/*.ship.json through
// ISteamRemoteStorage. The game keeps reading and writing the local folder
// from userShipsRoot(); cloudSyncUserShips copies those files up and down.
#include "steam_platform.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#if defined(SEA_TRIAL_WITH_STEAM) && SEA_TRIAL_WITH_STEAM
#include "steam/steam_api.h"
#endif

namespace steam_platform {

namespace {

// One ship file. Above this, skip the file rather than allocate from a
// hostile or corrupt cloud object. Workshop's own size cap is still TBD
// (interop.md); this bound is only the cloud read/write guard.
constexpr std::uint64_t kMaxShipBytes = 1024ull * 1024ull;
constexpr std::string_view kShipSuffix = ".ship.json";
constexpr std::string_view kCloudPrefix = "user_ships/";
constexpr std::size_t kMaxBasename = 128;

std::string g_cloudMessage;

std::string asciiLower(std::string text) {
    for (char& c : text) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return text;
}

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

struct LocalShip {
    std::string basename;
    std::filesystem::path path;
    std::vector<unsigned char> bytes;
    std::int64_t unixSeconds = 0;
};

struct RemoteShip {
    std::string cloudName;
    std::string basename;
    std::vector<unsigned char> bytes;
    std::int64_t unixSeconds = 0;
};

bool readLocalShip(const std::filesystem::path& path, std::vector<unsigned char>& bytes,
                   std::int64_t& unixSeconds, std::string& error) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const auto sz = fs::file_size(path, ec);
    if (ec) {
        error = "cannot stat local ship";
        return false;
    }
    if (sz > kMaxShipBytes) {
        error = "local ship exceeds 1 MiB";
        return false;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "cannot read local ship";
        return false;
    }
    bytes.resize(static_cast<std::size_t>(sz));
    if (sz > 0) {
        in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(sz));
        if (!in) {
            error = "short read of local ship";
            return false;
        }
    }
    const auto stamp = fs::last_write_time(path, ec);
    if (ec) {
        error = "cannot read local ship mtime";
        return false;
    }
    const auto sys = std::chrono::file_clock::to_sys(stamp);
    unixSeconds = std::chrono::duration_cast<std::chrono::seconds>(sys.time_since_epoch()).count();
    return true;
}

// Write via a temp file in the same directory so a crash mid-write does not
// leave a half ship. POSIX rename replaces the destination; Windows will not,
// so fall back to removing the old file first.
bool writeLocalShip(const std::filesystem::path& dest, const std::vector<unsigned char>& bytes) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(dest.parent_path(), ec);
    if (ec) return false;
    const fs::path tmp = fs::path(dest.string() + ".tmp");
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        if (!bytes.empty()) {
            out.write(reinterpret_cast<const char*>(bytes.data()),
                      static_cast<std::streamsize>(bytes.size()));
        }
        out.close();
        if (!out) {
            fs::remove(tmp, ec);
            return false;
        }
    }
    fs::rename(tmp, dest, ec);
    if (ec) {
        std::error_code rm;
        fs::remove(dest, rm);
        ec.clear();
        fs::rename(tmp, dest, ec);
        if (ec) {
            fs::remove(tmp, rm);
            return false;
        }
    }
    return true;
}

bool uploadShip(ISteamRemoteStorage* cloud, const std::string& cloudName,
                const std::vector<unsigned char>& bytes) {
    if (bytes.size() > kMaxShipBytes) return false;
    uint64 total = 0;
    uint64 available = 0;
    // Overwriting an existing object frees its old bytes, so add that size
    // back before deciding the write will not fit.
    if (cloud->GetQuota(&total, &available)) {
        uint64 room = available;
        if (cloud->FileExists(cloudName.c_str())) {
            const int32 oldSize = cloud->GetFileSize(cloudName.c_str());
            if (oldSize > 0) room += static_cast<uint64>(oldSize);
        }
        if (room < static_cast<uint64>(bytes.size())) {
            std::fprintf(stderr, "steam_platform: cloud quota too small for %s\n", cloudName.c_str());
            return false;
        }
    }
    static const unsigned char kEmpty = 0;
    const void* data = bytes.empty() ? static_cast<const void*>(&kEmpty) : bytes.data();
    const int32 n = static_cast<int32>(bytes.size());
    if (!cloud->FileWrite(cloudName.c_str(), data, n)) {
        std::fprintf(stderr, "steam_platform: cloud upload failed: %s\n", cloudName.c_str());
        return false;
    }
    return true;
}

HookResult cloudSyncLive() {
    ISteamRemoteStorage* cloud = SteamRemoteStorage();
    if (!cloud) {
        g_cloudMessage = "Steam RemoteStorage is unavailable; ships stay local";
        return HookResult::Failed;
    }
    if (!cloud->IsCloudEnabledForAccount() || !cloud->IsCloudEnabledForApp()) {
        g_cloudMessage = "Steam Cloud is disabled; ships stay local";
        return HookResult::Ok;
    }

    namespace fs = std::filesystem;
    const fs::path root(localUserShipsRoot());
    std::error_code ec;
    if (fs::exists(root, ec)) {
        if (ec || !fs::is_directory(root, ec) || ec) {
            g_cloudMessage = "user_ships path is not a directory";
            return HookResult::Failed;
        }
    }

    std::map<std::string, LocalShip> localByKey;
    // Keys we will not upload or download: case collisions, unreadable local
    // files, and cloud objects that could not be read. Leaving them alone is
    // safer than overwriting either copy.
    std::set<std::string> skipKeys;
    bool listFailed = false;
    if (fs::is_directory(root, ec) && !ec) {
        fs::directory_iterator it(root, ec);
        if (ec) {
            g_cloudMessage = "cannot list user_ships";
            return HookResult::Failed;
        }
        while (it != fs::directory_iterator()) {
            std::error_code entryEc;
            const bool symlink = it->is_symlink(entryEc);
            const bool regular = !symlink && !entryEc && it->is_regular_file(entryEc);
            if (!entryEc && regular) {
                const std::string basename = it->path().filename().string();
                if (isUserShipFileName(basename)) {
                    const std::string key = asciiLower(basename);
                    if (localByKey.find(key) != localByKey.end() || skipKeys.count(key) != 0) {
                        std::fprintf(stderr, "steam_platform: two local ships fold to %s\n", key.c_str());
                        skipKeys.insert(key);
                        localByKey.erase(key);
                    } else {
                        LocalShip ship;
                        ship.basename = basename;
                        ship.path = it->path();
                        std::string error;
                        if (!readLocalShip(ship.path, ship.bytes, ship.unixSeconds, error)) {
                            std::fprintf(stderr, "steam_platform: %s (%s)\n", error.c_str(),
                                         basename.c_str());
                            skipKeys.insert(key);
                        } else {
                            localByKey.emplace(key, std::move(ship));
                        }
                    }
                }
            }
            it.increment(ec);
            if (ec) {
                std::fprintf(stderr, "steam_platform: listing user_ships stopped early\n");
                listFailed = true;
                break;
            }
        }
    }

    std::map<std::string, RemoteShip> remoteByKey;
    const int32 cloudCount = cloud->GetFileCount();
    if (cloudCount < 0) {
        g_cloudMessage = "Steam Cloud file list failed";
        return HookResult::Failed;
    }
    for (int32 i = 0; i < cloudCount; ++i) {
        int32 listedSize = 0;
        const char* rawName = cloud->GetFileNameAndSize(i, &listedSize);
        if (!rawName || !rawName[0]) continue;
        // GetFileNameAndSize's pointer is overwritten on the next call.
        const std::string cloudName = rawName;
        std::string basename;
        if (!cloudObjectBasename(cloudName, basename)) continue;
        const std::string key = asciiLower(basename);
        if (listedSize < 0 || static_cast<std::uint64_t>(listedSize) > kMaxShipBytes) {
            std::fprintf(stderr, "steam_platform: cloud ship rejected (size): %s\n", cloudName.c_str());
            skipKeys.insert(key);
            remoteByKey.erase(key);
            continue;
        }
        RemoteShip ship;
        ship.cloudName = cloudName;
        ship.basename = basename;
        ship.unixSeconds = cloud->GetFileTimestamp(cloudName.c_str());
        ship.bytes.resize(static_cast<std::size_t>(listedSize));
        if (listedSize > 0) {
            const int32 got = cloud->FileRead(cloudName.c_str(), ship.bytes.data(), listedSize);
            if (got != listedSize) {
                std::fprintf(stderr, "steam_platform: cloud read failed: %s\n", cloudName.c_str());
                skipKeys.insert(key);
                remoteByKey.erase(key);
                continue;
            }
        }
        if (remoteByKey.find(key) != remoteByKey.end()) {
            std::fprintf(stderr, "steam_platform: two cloud ships fold to %s\n", key.c_str());
            skipKeys.insert(key);
            remoteByKey.erase(key);
            continue;
        }
        remoteByKey.emplace(key, std::move(ship));
    }

    std::set<std::string> keys;
    for (const auto& entry : localByKey) keys.insert(entry.first);
    for (const auto& entry : remoteByKey) keys.insert(entry.first);
    for (const auto& key : skipKeys) keys.insert(key);

    int uploaded = 0;
    int downloaded = 0;
    int unchanged = 0;
    int failed = listFailed ? 1 : 0;

    for (const std::string& key : keys) {
        if (skipKeys.count(key) != 0) {
            std::fprintf(stderr, "steam_platform: skipped ship %s\n", key.c_str());
            ++failed;
            continue;
        }
        const auto localIt = localByKey.find(key);
        const auto remoteIt = remoteByKey.find(key);
        const bool hasLocal = localIt != localByKey.end();
        const bool hasRemote = remoteIt != remoteByKey.end();

        CloudFileFacts facts;
        facts.localPresent = hasLocal;
        facts.remotePresent = hasRemote;
        if (hasLocal) facts.localUnixSeconds = localIt->second.unixSeconds;
        if (hasRemote) facts.remoteUnixSeconds = remoteIt->second.unixSeconds;
        if (hasLocal && hasRemote) facts.bytesEqual = localIt->second.bytes == remoteIt->second.bytes;

        const CloudFileAction action = planCloudFile(facts);
        if (action == CloudFileAction::Unchanged) {
            ++unchanged;
            continue;
        }
        if (action == CloudFileAction::Upload) {
            const std::string object = cloudObjectName(localIt->second.basename);
            if (object.empty() || !uploadShip(cloud, object, localIt->second.bytes)) ++failed;
            else ++uploaded;
            continue;
        }
        const fs::path dest = hasLocal ? localIt->second.path : (root / remoteIt->second.basename);
        if (!writeLocalShip(dest, remoteIt->second.bytes)) {
            std::fprintf(stderr, "steam_platform: local write failed: %s\n", dest.string().c_str());
            ++failed;
        } else {
            ++downloaded;
        }
    }

    char summary[160];
    std::snprintf(summary, sizeof(summary), "synced %d up, %d down, %d unchanged, %d failed",
                  uploaded, downloaded, unchanged, failed);
    g_cloudMessage = summary;
    return failed == 0 ? HookResult::Ok : HookResult::Failed;
}
#endif

} // namespace

bool isUserShipFileName(const std::string& basename) {
    if (basename.size() <= kShipSuffix.size() || basename.size() > kMaxBasename) return false;
    if (basename.compare(basename.size() - kShipSuffix.size(), kShipSuffix.size(), kShipSuffix) != 0) {
        return false;
    }
    if (basename.find("..") != std::string::npos) return false;
    for (unsigned char c : basename) {
        if (c < 0x20 || c == 0x7f) return false;
        switch (c) {
        case '/':
        case '\\':
        case ':':
        case '*':
        case '?':
        case '"':
        case '<':
        case '>':
        case '|':
            return false;
        default:
            break;
        }
    }
    return true;
}

std::string cloudObjectName(const std::string& basename) {
    if (!isUserShipFileName(basename)) return {};
    return std::string(kCloudPrefix) + asciiLower(basename);
}

bool cloudObjectBasename(const std::string& cloudName, std::string& basename) {
    basename.clear();
    if (cloudName.size() <= kCloudPrefix.size()) return false;
    if (cloudName.compare(0, kCloudPrefix.size(), kCloudPrefix) != 0) return false;
    const std::string rest = cloudName.substr(kCloudPrefix.size());
    if (!isUserShipFileName(rest)) return false;
    basename = rest;
    return true;
}

CloudFileAction planCloudFile(const CloudFileFacts& facts) {
    if (facts.localPresent && facts.remotePresent) {
        if (facts.bytesEqual) return CloudFileAction::Unchanged;
        if (facts.localUnixSeconds >= facts.remoteUnixSeconds) return CloudFileAction::Upload;
        return CloudFileAction::Download;
    }
    if (facts.localPresent) return CloudFileAction::Upload;
    if (facts.remotePresent) return CloudFileAction::Download;
    return CloudFileAction::Unchanged;
}

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

std::string userShipsRoot() { return localUserShipsRoot(); }

std::string cloudSyncMessage() { return g_cloudMessage; }

HookResult cloudSyncUserShips() {
#if defined(SEA_TRIAL_WITH_STEAM) && SEA_TRIAL_WITH_STEAM
    if (g_active) return cloudSyncLive();
    g_cloudMessage = "local only (Steam offline)";
    return HookResult::Ok;
#else
    g_cloudMessage = "local only (Steam off)";
    return HookResult::Ok;
#endif
}

HookResult workshopValidate(const std::string& shipPath) {
    (void)shipPath;
    // TODO: upload and download validation per interop.md. No Workshop I/O yet.
    return HookResult::Unimplemented;
}

} // namespace steam_platform
