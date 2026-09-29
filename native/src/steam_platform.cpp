// Sea Trial — Steamworks façade. Public domain (The Unlicense).
//
// SEA_TRIAL_WITH_STEAM unset (the default): local stubs, no SDK headers.
// SEA_TRIAL_WITH_STEAM=1: call the real Steam API. The SDK is supplied at
// configure time and is not part of this repository.
//
// Steam Cloud, when a session is up, mirrors user_ships/*.ship.json through
// ISteamRemoteStorage. The game keeps reading and writing the local folder
// from userShipsRoot(); cloudSyncUserShips copies those files up and down.
//
// workshopValidate reads a schema-v1 .ship.json and runs sea::validateShip.
// workshopPublish submits a new private Workshop item only while a session
// is up (ISteamUGC). The final link must include ship_model.cpp.
#include "steam_platform.h"
#include "ship_model.hpp"

#include <cctype>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
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
HookResult g_workshopResult = HookResult::Unimplemented;
std::string g_workshopMessage;

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

void storeWorkshop(WorkshopValidateResult& report) {
    if (report.code.empty()) report.code = "FAILED";
    g_workshopResult = report.result;
    g_workshopMessage = report.message.empty() ? report.code : report.message;
}

WorkshopValidateResult workshopFail(const std::string& code, const std::string& message) {
    WorkshopValidateResult report;
    report.result = HookResult::Failed;
    report.code = code;
    report.message = code + ": " + message;
    report.errors.push_back({ code, message });
    storeWorkshop(report);
    return report;
}

enum class JsonKind { Null, Bool, Number, String, Array, Object };

struct Json {
    JsonKind kind = JsonKind::Null;
    bool boolean = false;
    double number = 0;
    std::string text;
    std::vector<Json> items;
    std::vector<std::pair<std::string, Json>> fields;
};

const Json* jsonField(const Json& obj, const char* key) {
    const Json* found = nullptr;
    if (obj.kind != JsonKind::Object) return nullptr;
    for (const auto& entry : obj.fields) {
        if (entry.first == key) found = &entry.second;
    }
    return found;
}

class JsonParser {
public:
    explicit JsonParser(std::string_view in) : text(in) {
        if (text.size() >= 3
            && static_cast<unsigned char>(text[0]) == 0xEF
            && static_cast<unsigned char>(text[1]) == 0xBB
            && static_cast<unsigned char>(text[2]) == 0xBF) {
            i = 3;
        }
    }

    bool parse(Json& out, std::string& error) {
        if (!parseValue(out, error)) return false;
        skipWs();
        if (i != text.size()) {
            error = "trailing data after JSON";
            return false;
        }
        return true;
    }

private:
    std::string_view text;
    std::size_t i = 0;
    int depth = 0;

    void skipWs() {
        while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
    }

    char peek() const { return i < text.size() ? text[i] : '\0'; }

    bool parseValue(Json& out, std::string& error) {
        skipWs();
        if (i >= text.size()) {
            error = "unexpected end of JSON";
            return false;
        }
        if (depth > 32) {
            error = "JSON is nested too deeply";
            return false;
        }
        const char c = text[i];
        if (c == '{') return parseObject(out, error);
        if (c == '[') return parseArray(out, error);
        if (c == '"') {
            out.kind = JsonKind::String;
            return parseString(out.text, error);
        }
        if (c == 't') return parseLiteral("true", JsonKind::Bool, true, out, error);
        if (c == 'f') return parseLiteral("false", JsonKind::Bool, false, out, error);
        if (c == 'n') return parseLiteral("null", JsonKind::Null, false, out, error);
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) return parseNumber(out, error);
        error = "unexpected character in JSON";
        return false;
    }

    bool parseLiteral(const char* word, JsonKind kind, bool boolean, Json& out, std::string& error) {
        for (const char* p = word; *p; ++p) {
            if (peek() != *p) {
                error = "invalid JSON literal";
                return false;
            }
            ++i;
        }
        out.kind = kind;
        out.boolean = boolean;
        return true;
    }

    bool parseString(std::string& out, std::string& error) {
        if (peek() != '"') {
            error = "expected a string";
            return false;
        }
        ++i;
        out.clear();
        while (i < text.size()) {
            const unsigned char c = static_cast<unsigned char>(text[i++]);
            if (c == '"') return true;
            if (c < 0x20) {
                error = "raw control character in string";
                return false;
            }
            if (c != '\\') {
                out.push_back(static_cast<char>(c));
                continue;
            }
            if (i >= text.size()) {
                error = "truncated string escape";
                return false;
            }
            const char e = text[i++];
            switch (e) {
            case '"':
            case '\\':
            case '/': out.push_back(e); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            case 'u': {
                if (i + 4 > text.size()) {
                    error = "truncated unicode escape";
                    return false;
                }
                int cp = 0;
                for (int k = 0; k < 4; ++k) {
                    const char h = text[i++];
                    cp <<= 4;
                    if (h >= '0' && h <= '9') cp += h - '0';
                    else if (h >= 'a' && h <= 'f') cp += h - 'a' + 10;
                    else if (h >= 'A' && h <= 'F') cp += h - 'A' + 10;
                    else {
                        error = "bad unicode escape";
                        return false;
                    }
                }
                if (cp >= 0xD800 && cp <= 0xDFFF) {
                    error = "lone surrogate in string";
                    return false;
                }
                if (cp < 0x80) {
                    out.push_back(static_cast<char>(cp));
                } else if (cp < 0x800) {
                    out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                } else {
                    out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                }
                break;
            }
            default:
                error = "unknown string escape";
                return false;
            }
        }
        error = "unterminated string";
        return false;
    }

    bool parseNumber(Json& out, std::string& error) {
        const std::size_t start = i;
        if (peek() == '-') ++i;
        if (!std::isdigit(static_cast<unsigned char>(peek()))) {
            error = "invalid number";
            return false;
        }
        if (peek() == '0') {
            ++i;
        } else {
            while (std::isdigit(static_cast<unsigned char>(peek()))) ++i;
        }
        if (peek() == '.') {
            ++i;
            if (!std::isdigit(static_cast<unsigned char>(peek()))) {
                error = "invalid number";
                return false;
            }
            while (std::isdigit(static_cast<unsigned char>(peek()))) ++i;
        }
        if (peek() == 'e' || peek() == 'E') {
            ++i;
            if (peek() == '+' || peek() == '-') ++i;
            if (!std::isdigit(static_cast<unsigned char>(peek()))) {
                error = "invalid number";
                return false;
            }
            while (std::isdigit(static_cast<unsigned char>(peek()))) ++i;
        }
        if (i - start > 64) {
            error = "number is too long";
            return false;
        }
        double value = 0;
        const char* first = text.data() + start;
        const char* last = text.data() + i;
        const std::from_chars_result res = std::from_chars(first, last, value);
        if (res.ec != std::errc() || res.ptr != last || !std::isfinite(value)) {
            error = "invalid number";
            return false;
        }
        out.kind = JsonKind::Number;
        out.number = value;
        return true;
    }

    bool parseObject(Json& out, std::string& error) {
        ++i;
        ++depth;
        out.kind = JsonKind::Object;
        skipWs();
        if (peek() == '}') {
            ++i;
            --depth;
            return true;
        }
        while (true) {
            skipWs();
            if (peek() != '"') {
                error = "expected a string key";
                return false;
            }
            std::string key;
            if (!parseString(key, error)) return false;
            skipWs();
            if (peek() != ':') {
                error = "expected ':' after key";
                return false;
            }
            ++i;
            Json child;
            if (!parseValue(child, error)) return false;
            out.fields.emplace_back(std::move(key), std::move(child));
            skipWs();
            if (peek() == ',') {
                ++i;
                continue;
            }
            if (peek() == '}') {
                ++i;
                --depth;
                return true;
            }
            error = "expected ',' or '}'";
            return false;
        }
    }

    bool parseArray(Json& out, std::string& error) {
        ++i;
        ++depth;
        out.kind = JsonKind::Array;
        skipWs();
        if (peek() == ']') {
            ++i;
            --depth;
            return true;
        }
        while (true) {
            Json child;
            if (!parseValue(child, error)) return false;
            out.items.push_back(std::move(child));
            skipWs();
            if (peek() == ',') {
                ++i;
                continue;
            }
            if (peek() == ']') {
                ++i;
                --depth;
                return true;
            }
            error = "expected ',' or ']'";
            return false;
        }
    }
};

bool jsonAsInt(double n, int& out) {
    if (!std::isfinite(n) || n > 1000000.0 || n < -1000000.0) return false;
    const double rounded = std::round(n);
    if (std::fabs(n - rounded) > 1e-6) return false;
    out = static_cast<int>(rounded);
    return true;
}

bool jsonStringField(const Json& obj, const char* key, std::string& dest, std::string& error) {
    const Json* node = jsonField(obj, key);
    if (!node) return true;
    if (node->kind != JsonKind::String) {
        error = std::string(key) + " must be a string";
        return false;
    }
    dest = node->text;
    return true;
}

bool jsonNumberField(const Json& obj, const char* key, double& dest, std::string& error) {
    const Json* node = jsonField(obj, key);
    if (!node) return true;
    if (node->kind != JsonKind::Number) {
        error = std::string(key) + " must be a number";
        return false;
    }
    dest = node->number;
    return true;
}

bool jsonIntField(const Json& obj, const char* key, int& dest, std::string& error) {
    const Json* node = jsonField(obj, key);
    if (!node) return true;
    if (node->kind != JsonKind::Number) {
        error = std::string(key) + " must be a number";
        return false;
    }
    int value = 0;
    if (!jsonAsInt(node->number, value)) {
        error = std::string(key) + " must be an integer";
        return false;
    }
    dest = value;
    return true;
}

bool jsonVec3(const Json* node, sea::Vec3& out, std::string& error, const char* what) {
    if (!node) return true;
    if (node->kind != JsonKind::Array || node->items.size() != 3) {
        error = std::string(what) + " must be an array of 3 numbers";
        return false;
    }
    double v[3];
    for (int n = 0; n < 3; ++n) {
        if (node->items[static_cast<std::size_t>(n)].kind != JsonKind::Number) {
            error = std::string(what) + " must be an array of 3 numbers";
            return false;
        }
        v[n] = node->items[static_cast<std::size_t>(n)].number;
    }
    out = { v[0], v[1], v[2] };
    return true;
}

bool shipFromJson(const Json& root, sea::Ship& ship, std::string& error) {
    if (root.kind != JsonKind::Object) {
        error = "ship file must be a JSON object";
        return false;
    }
    ship.schema_version = 0;
    if (const Json* version = jsonField(root, "schema_version")) {
        if (version->kind != JsonKind::Number) {
            error = "schema_version must be a number";
            return false;
        }
        if (!jsonAsInt(version->number, ship.schema_version)) {
            error = "schema_version must be an integer";
            return false;
        }
    }
    if (!jsonStringField(root, "ship_id", ship.ship_id, error)) return false;
    if (!jsonStringField(root, "display_name", ship.display_name, error)) return false;
    if (!jsonStringField(root, "created_by", ship.created_by, error)) return false;
    if (!jsonStringField(root, "build_version", ship.build_version, error)) return false;

    if (const Json* bounds = jsonField(root, "bounds")) {
        if (bounds->kind != JsonKind::Object) {
            error = "bounds must be an object";
            return false;
        }
        if (!jsonNumberField(*bounds, "length", ship.bounds.length, error)) return false;
        if (!jsonNumberField(*bounds, "width", ship.bounds.width, error)) return false;
        if (!jsonNumberField(*bounds, "depth", ship.bounds.depth, error)) return false;
        if (!jsonNumberField(*bounds, "height", ship.bounds.height, error)) return false;
    }

    if (const Json* systems = jsonField(root, "systems")) {
        if (systems->kind != JsonKind::Object) {
            error = "systems must be an object";
            return false;
        }
        if (!jsonIntField(*systems, "helm_count", ship.systems.helm_count, error)) return false;
        if (!jsonIntField(*systems, "sail_count", ship.systems.sail_count, error)) return false;
        if (!jsonIntField(*systems, "mast_count", ship.systems.mast_count, error)) return false;
        if (!jsonIntField(*systems, "cannon_count", ship.systems.cannon_count, error)) return false;
        if (!jsonNumberField(*systems, "cargo_mass", ship.systems.cargo_mass, error)) return false;
    }

    if (const Json* pieces = jsonField(root, "pieces")) {
        if (pieces->kind != JsonKind::Array) {
            error = "pieces must be an array";
            return false;
        }
        for (const Json& item : pieces->items) {
            if (item.kind != JsonKind::Object) {
                error = "each piece must be an object";
                return false;
            }
            sea::Piece piece;
            if (!jsonStringField(item, "id", piece.id, error)) return false;
            if (!jsonStringField(item, "type", piece.type, error)) return false;
            if (!jsonStringField(item, "material", piece.material, error)) return false;
            if (!jsonNumberField(item, "volume", piece.volume, error)) return false;
            if (!jsonNumberField(item, "mass", piece.mass, error)) return false;
            if (!jsonNumberField(item, "damage", piece.damage, error)) return false;
            if (!jsonVec3(jsonField(item, "position"), piece.position, error, "position")) return false;
            if (!jsonVec3(jsonField(item, "rotation"), piece.rotation, error, "rotation")) return false;
            if (!jsonVec3(jsonField(item, "bounds"), piece.bounds, error, "piece bounds")) return false;
            ship.pieces.push_back(std::move(piece));
        }
    }
    return true;
}

WorkshopValidateResult fromStructural(const sea::ValidationResult& structural) {
    WorkshopValidateResult report;
    for (const auto& warning : structural.warnings) {
        report.warnings.push_back({ warning.code, warning.message });
    }
    if (structural.errors.empty()) {
        report.result = HookResult::Ok;
        report.code = "OK";
        report.message = "Ship passed Workshop checks.";
        if (!report.warnings.empty()) {
            report.message += " Warning " + report.warnings.front().code + ": " + report.warnings.front().message;
        }
        storeWorkshop(report);
        return report;
    }
    report.result = HookResult::Failed;
    for (const auto& issue : structural.errors) {
        report.errors.push_back({ issue.code, issue.message });
    }
    report.code = report.errors.front().code;
    report.message = report.code + ": " + report.errors.front().message;
    if (report.errors.size() > 1) {
        report.message += " (+" + std::to_string(report.errors.size() - 1) + " more)";
    }
    storeWorkshop(report);
    return report;
}

struct LoadedShip {
    WorkshopValidateResult report;
    sea::Ship ship;
    std::filesystem::path path;
    std::string bytes;
};

LoadedShip loadShipForWorkshop(const std::string& shipPath) {
    LoadedShip loaded;
    namespace fs = std::filesystem;
    if (shipPath.empty()) {
        loaded.report = workshopFail("BAD_PATH", "Ship path is empty.");
        return loaded;
    }
    const fs::path path(shipPath);
    loaded.path = path;
    const std::string filename = path.filename().string();
    if (filename.empty() || filename == "." || filename == "..") {
        loaded.report = workshopFail("BAD_PATH", "Ship path has no file name.");
        return loaded;
    }
    if (!isUserShipFileName(filename)) {
        loaded.report = workshopFail("NOT_A_SHIP", "Workshop checks read a single .ship.json file.");
        return loaded;
    }

    std::error_code ec;
    const fs::file_status status = fs::status(path, ec);
    if (ec || !fs::exists(status)) {
        loaded.report = workshopFail("CANNOT_READ", "Ship file could not be read.");
        return loaded;
    }
    if (!fs::is_regular_file(status)) {
        loaded.report = workshopFail("BAD_PATH", "Ship path is not a file.");
        return loaded;
    }
    const auto sz = fs::file_size(path, ec);
    if (ec) {
        loaded.report = workshopFail("CANNOT_READ", "Ship file could not be read.");
        return loaded;
    }
    if (sz > kMaxShipBytes) {
        loaded.report = workshopFail("FILE_TOO_LARGE", "Ship file exceeds the 1 MiB Workshop cap.");
        return loaded;
    }
    if (sz == 0) {
        loaded.report = workshopFail("EMPTY_FILE", "Ship file is empty.");
        return loaded;
    }

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        loaded.report = workshopFail("CANNOT_READ", "Ship file could not be read.");
        return loaded;
    }
    loaded.bytes.resize(static_cast<std::size_t>(sz));
    in.read(loaded.bytes.data(), static_cast<std::streamsize>(sz));
    if (!in) {
        loaded.bytes.clear();
        loaded.report = workshopFail("CANNOT_READ", "Ship file could not be read.");
        return loaded;
    }

    Json root;
    std::string parseError;
    JsonParser parser(loaded.bytes);
    if (!parser.parse(root, parseError)) {
        loaded.report = workshopFail("BAD_JSON", "Ship JSON could not be parsed (" + parseError + ").");
        return loaded;
    }
    std::string mapError;
    if (!shipFromJson(root, loaded.ship, mapError)) {
        loaded.report = workshopFail("BAD_JSON", "Ship JSON could not be parsed (" + mapError + ").");
        return loaded;
    }
    loaded.report = fromStructural(sea::validateShip(loaded.ship));
    return loaded;
}

#if defined(SEA_TRIAL_WITH_STEAM) && SEA_TRIAL_WITH_STEAM
// Block until a Steam API call finishes. Ship files are small; this is the
// same synchronous style as cloud sync. CI never compiles this function.
template <typename T>
bool waitForSteamCall(SteamAPICall_t call, T& out, const char* what, std::string& error) {
    if (call == k_uAPICallInvalid) {
        error = std::string(what) + " was not started";
        return false;
    }
    ISteamUtils* utils = SteamUtils();
    if (!utils) {
        error = "SteamUtils is unavailable";
        return false;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (true) {
        SteamAPI_RunCallbacks();
        bool failed = false;
        if (utils->IsAPICallCompleted(call, &failed)) {
            if (failed) {
                error = std::string(what) + " failed";
                return false;
            }
            bool getFailed = false;
            if (!utils->GetAPICallResult(call, &out, static_cast<int>(sizeof(out)), T::k_iCallback, &getFailed)
                || getFailed) {
                error = std::string(what) + " result was unavailable";
                return false;
            }
            return true;
        }
        if (std::chrono::steady_clock::now() > deadline) {
            error = std::string(what) + " timed out";
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

WorkshopValidateResult submitValidatedShip(const std::filesystem::path& shipFile,
                                           const std::string& bytes,
                                           const sea::Ship& ship) {
    auto fail = [](const std::string& code, const std::string& message) {
        return workshopFail(code, message);
    };
    ISteamUGC* ugc = SteamUGC();
    ISteamUtils* utils = SteamUtils();
    if (!ugc || !utils) return fail("WORKSHOP_UNAVAILABLE", "Steam Workshop is unavailable.");
    const AppId_t appId = utils->GetAppID();
    if (appId == 0) return fail("SUBMIT_FAILED", "Steam did not report an AppID.");

    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path folder = fs::absolute(fs::path(localUserShipsRoot()) / ".workshop_stage" / shipFile.filename(), ec);
    if (ec || folder.empty()) return fail("SUBMIT_FAILED", "Could not stage the ship for Workshop.");
    fs::create_directories(folder, ec);
    if (ec) return fail("SUBMIT_FAILED", "Could not stage the ship for Workshop.");
    struct StageCleanup {
        std::filesystem::path folder;
        ~StageCleanup() {
            if (folder.empty()) return;
            std::error_code rm;
            std::filesystem::remove_all(folder, rm);
        }
    } cleanup{ folder };
    {
        std::ofstream out(folder / shipFile.filename(), std::ios::binary | std::ios::trunc);
        if (!out) return fail("SUBMIT_FAILED", "Could not stage the ship for Workshop.");
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        out.close();
        if (!out) return fail("SUBMIT_FAILED", "Could not stage the ship for Workshop.");
    }

    std::string title = ship.display_name;
    if (title.empty()) {
        title = shipFile.filename().string();
        constexpr std::string_view suffix = ".ship.json";
        if (title.size() > suffix.size()
            && title.compare(title.size() - suffix.size(), suffix.size(), suffix) == 0) {
            title.resize(title.size() - suffix.size());
        }
    }
    if (title.empty()) title = "Ship";
    if (title.size() > 128) title.resize(128);

    std::string callError;
    CreateItemResult_t created{};
    const SteamAPICall_t createCall = ugc->CreateItem(appId, k_EWorkshopFileTypeCommunity);
    if (!waitForSteamCall(createCall, created, "CreateItem", callError)) {
        std::fprintf(stderr, "steam_platform: %s\n", callError.c_str());
        return fail("SUBMIT_FAILED", callError);
    }
    if (created.m_eResult != k_EResultOK || created.m_nPublishedFileId == 0) {
        if (created.m_bUserNeedsToAcceptWorkshopLegalAgreement) {
            return fail("WORKSHOP_LEGAL", "Accept the Steam Workshop legal agreement, then publish again.");
        }
        char buf[96];
        std::snprintf(buf, sizeof(buf), "Workshop item was not created (EResult %d).",
                      static_cast<int>(created.m_eResult));
        std::fprintf(stderr, "steam_platform: %s\n", buf);
        return fail("SUBMIT_FAILED", buf);
    }

    const UGCUpdateHandle_t handle = ugc->StartItemUpdate(appId, created.m_nPublishedFileId);
    if (handle == k_UGCUpdateHandleInvalid) {
        return fail("SUBMIT_FAILED", "Workshop update could not be started.");
    }
    const std::string content = folder.string();
    if (!ugc->SetItemTitle(handle, title.c_str())
        || !ugc->SetItemDescription(handle, "Player ship from Sea Trial.")
        || !ugc->SetItemVisibility(handle, k_ERemoteStoragePublishedFileVisibilityPrivate)
        || !ugc->SetItemContent(handle, content.c_str())) {
        return fail("SUBMIT_FAILED", "Workshop item fields could not be set.");
    }

    SubmitItemUpdateResult_t submitted{};
    const SteamAPICall_t submitCall = ugc->SubmitItemUpdate(handle, "Initial publish.");
    if (!waitForSteamCall(submitCall, submitted, "SubmitItemUpdate", callError)) {
        std::fprintf(stderr, "steam_platform: %s\n", callError.c_str());
        return fail("SUBMIT_FAILED", callError);
    }
    if (submitted.m_eResult != k_EResultOK) {
        if (submitted.m_bUserNeedsToAcceptWorkshopLegalAgreement) {
            return fail("WORKSHOP_LEGAL", "Accept the Steam Workshop legal agreement, then publish again.");
        }
        char buf[96];
        std::snprintf(buf, sizeof(buf), "Workshop submit failed (EResult %d).",
                      static_cast<int>(submitted.m_eResult));
        std::fprintf(stderr, "steam_platform: %s\n", buf);
        return fail("SUBMIT_FAILED", buf);
    }

    const PublishedFileId_t publishedId = submitted.m_nPublishedFileId != 0
        ? submitted.m_nPublishedFileId
        : created.m_nPublishedFileId;
    char buf[192];
    std::snprintf(buf, sizeof(buf),
                  "Published a new private Workshop item %llu.",
                  static_cast<unsigned long long>(publishedId));
    WorkshopValidateResult report;
    report.result = HookResult::Ok;
    report.code = "PUBLISHED";
    report.message = buf;
    storeWorkshop(report);
    return report;
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

WorkshopValidateResult workshopValidate(const std::string& shipPath) {
    return loadShipForWorkshop(shipPath).report;
}

WorkshopValidateResult workshopPublish(const std::string& shipPath) {
    LoadedShip loaded = loadShipForWorkshop(shipPath);
    if (loaded.report.result != HookResult::Ok) return loaded.report;
#if defined(SEA_TRIAL_WITH_STEAM) && SEA_TRIAL_WITH_STEAM
    if (g_active) return submitValidatedShip(loaded.path, loaded.bytes, loaded.ship);
#endif
    loaded.report.result = HookResult::Unimplemented;
    loaded.report.code = "NOT_SUBMITTED";
    loaded.report.message =
        "Validated locally. Nothing was published: Workshop submit needs a Steam session.";
    if (!loaded.report.warnings.empty()) {
        loaded.report.message += " Warning " + loaded.report.warnings.front().code + ".";
    }
    storeWorkshop(loaded.report);
    return loaded.report;
}

std::string workshopMessage() { return g_workshopMessage; }

HookResult lastWorkshopResult() { return g_workshopResult; }

} // namespace steam_platform
