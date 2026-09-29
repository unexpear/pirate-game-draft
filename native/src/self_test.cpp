// Sea Trial — native self-test runner (Milestone 0). Public domain (The Unlicense).
//
// Runs the model-spine checks ported from the Three.js spike, then the
// steam_platform stub checks. Exit code 0 iff all pass — so it doubles as a CI gate.
#include "ship_model.hpp"
#include "steam_platform.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace {

bool runSteamPlatformChecks() {
    int passing = 0;
    int total = 0;
    auto check = [&](const char* name, bool pass, const std::string& details = {}) {
        ++total;
        if (pass) {
            std::printf("PASS: %s\n", name);
            ++passing;
        } else {
            std::printf("FAIL: %s%s%s\n", name,
                        details.empty() ? "" : " — ", details.c_str());
        }
    };

    check("cloud sync message starts empty", steam_platform::cloudSyncMessage().empty());

    check("test_sloop.ship.json is a ship file",
          steam_platform::isUserShipFileName("test_sloop.ship.json"));
    check("spaced ship name is a ship file",
          steam_platform::isUserShipFileName("Test Sloop.ship.json"));
    check("suffix alone is not a ship file",
          !steam_platform::isUserShipFileName(".ship.json"));
    check("other json is not a ship file",
          !steam_platform::isUserShipFileName("notes.json"));
    check("slash in a ship name is rejected",
          !steam_platform::isUserShipFileName("nested/a.ship.json"));
    check("parent segment in a ship name is rejected",
          !steam_platform::isUserShipFileName("..ship.json"));
    check("cloud object name is the lowercased user_ships prefix",
          steam_platform::cloudObjectName("Test_Sloop.ship.json") == "user_ships/test_sloop.ship.json");
    check("bad basename has no cloud object name",
          steam_platform::cloudObjectName("notes.txt").empty());

    std::string parsed;
    check("cloud object basename round-trips",
          steam_platform::cloudObjectBasename("user_ships/test_sloop.ship.json", parsed)
              && parsed == "test_sloop.ship.json");
    check("cloud listing ignores other files",
          !steam_platform::cloudObjectBasename("screenshots/port.png", parsed));
    check("cloud listing ignores a nested ship path",
          !steam_platform::cloudObjectBasename("user_ships/../secret.ship.json", parsed));
    check("cloud listing ignores a leading slash",
          !steam_platform::cloudObjectBasename("/user_ships/a.ship.json", parsed));

    using steam_platform::CloudFileAction;
    using steam_platform::CloudFileFacts;
    auto facts = [](bool local, bool remote, bool same, std::int64_t localTs, std::int64_t remoteTs) {
        CloudFileFacts f;
        f.localPresent = local;
        f.remotePresent = remote;
        f.bytesEqual = same;
        f.localUnixSeconds = localTs;
        f.remoteUnixSeconds = remoteTs;
        return f;
    };
    check("local-only ship uploads",
          steam_platform::planCloudFile(facts(true, false, false, 10, 0)) == CloudFileAction::Upload);
    check("remote-only ship downloads",
          steam_platform::planCloudFile(facts(false, true, false, 0, 10)) == CloudFileAction::Download);
    check("identical bytes stay put",
          steam_platform::planCloudFile(facts(true, true, true, 1, 50)) == CloudFileAction::Unchanged);
    check("newer local bytes upload",
          steam_platform::planCloudFile(facts(true, true, false, 20, 10)) == CloudFileAction::Upload);
    check("newer remote bytes download",
          steam_platform::planCloudFile(facts(true, true, false, 10, 20)) == CloudFileAction::Download);
    check("equal timestamps keep the local ship",
          steam_platform::planCloudFile(facts(true, true, false, 10, 10)) == CloudFileAction::Upload);
    check("neither copy is unchanged",
          steam_platform::planCloudFile(facts(false, false, false, 0, 0)) == CloudFileAction::Unchanged);

    // pump/shutdown must be harmless before a session exists.
    steam_platform::pumpCallbacks();
    steam_platform::shutdown();
    check("cloud sync before init is local-only",
          steam_platform::cloudSyncUserShips() == steam_platform::HookResult::Ok
              && steam_platform::cloudSyncMessage().find("local") != std::string::npos);

    check("steam init succeeds", steam_platform::init());
    check("steam init is idempotent", steam_platform::init());
    steam_platform::pumpCallbacks();

    const bool online = steam_platform::isSteam();
    if (!steam_platform::compiledWithSteamSdk()) {
        check("default build has no Steam session", !online);
        check("stub persona is empty", steam_platform::personaName().empty());
    } else {
        check("persona is empty while offline", online || steam_platform::personaName().empty());
    }

    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path expected = (fs::current_path(ec) / "user_ships").lexically_normal();
    const fs::path root(steam_platform::userShipsRoot());
    const bool local = !ec && root.is_absolute() && root.filename() == "user_ships" && root == expected;
    check("userShipsRoot is the local user_ships directory", local, root.string());

    if (!online) {
        const fs::path ship = expected / "selftest_cloud_keep.ship.json";
        const fs::path note = expected / "selftest_cloud_notes.txt";
        const bool dirExisted = fs::is_directory(expected, ec);
        fs::create_directories(expected, ec);
        const std::string body = "{\"name\":\"selftest-cloud\"}\n";
        {
            std::ofstream out(ship, std::ios::binary | std::ios::trunc);
            out << body;
            std::ofstream side(note, std::ios::binary | std::ios::trunc);
            side << "leave me\n";
        }
        const auto synced = steam_platform::cloudSyncUserShips();
        std::string kept;
        {
            std::ifstream in(ship, std::ios::binary);
            kept.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }
        std::string sideKept;
        {
            std::ifstream in(note, std::ios::binary);
            sideKept.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }
        check("offline cloud sync is a local-only success",
              synced == steam_platform::HookResult::Ok);
        check("offline cloud sync message says local",
              steam_platform::cloudSyncMessage().find("local") != std::string::npos);
        check("offline cloud sync leaves the ship file untouched", kept == body, kept);
        check("offline cloud sync leaves non-ship files untouched", sideKept == "leave me\n", sideKept);
        const auto syncedAgain = steam_platform::cloudSyncUserShips();
        check("offline cloud sync is idempotent", syncedAgain == steam_platform::HookResult::Ok);

        std::error_code rm;
        fs::remove(ship, rm);
        fs::remove(note, rm);
        if (!dirExisted) {
            bool empty = true;
            if (fs::is_directory(expected, rm)) {
                const fs::directory_iterator end;
                for (fs::directory_iterator it(expected, rm); !rm && it != end; it.increment(rm)) {
                    empty = false;
                    break;
                }
            }
            if (empty) fs::remove(expected, rm);
        }
    } else {
        // Do not upload the self-test fixture into a live cloud. The folder
        // the game reads is still the local one; sync runs from the app.
        check("connected session keeps the local user_ships folder", root == expected, root.string());
    }
    check("workshop validate is unimplemented",
          steam_platform::workshopValidate("user_ships/test_sloop.ship.json")
              == steam_platform::HookResult::Unimplemented);

    steam_platform::shutdown();
    check("steam is offline after shutdown", !steam_platform::isSteam());
    check("cloud sync after shutdown is local-only",
          steam_platform::cloudSyncUserShips() == steam_platform::HookResult::Ok
              && steam_platform::cloudSyncMessage().find("local") != std::string::npos);
    steam_platform::shutdown();
    steam_platform::pumpCallbacks();
    check("steam shutdown is idempotent", !steam_platform::isSteam());

    check("steam re-init succeeds", steam_platform::init());
    const fs::path rootAgain(steam_platform::userShipsRoot());
    check("userShipsRoot stays local across re-init", rootAgain == expected, rootAgain.string());
    steam_platform::shutdown();

    std::printf("\nsteam platform: %d / %d passing\n", passing, total);
    return passing == total;
}

} // namespace

int main() {
    const auto results = sea::runSelfTest();

    int passing = 0;
    for (const auto& r : results) {
        if (r.pass) {
            std::printf("PASS: %s\n", r.name.c_str());
            ++passing;
        } else {
            std::printf("FAIL: %s%s%s\n", r.name.c_str(),
                        r.details.empty() ? "" : " — ", r.details.c_str());
        }
    }

    const int total = static_cast<int>(results.size());
    std::printf("\n%d / %d passing\n", passing, total);

    const bool steamOk = runSteamPlatformChecks();
    return (passing == total && steamOk) ? 0 : 1;
}
