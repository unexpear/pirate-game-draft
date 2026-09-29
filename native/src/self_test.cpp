// Sea Trial — native self-test runner (Milestone 0). Public domain (The Unlicense).
//
// Runs the model-spine checks ported from the Three.js spike, then the
// steam_platform stub checks. Exit code 0 iff all pass — so it doubles as a CI gate.
#include "ship_model.hpp"
#include "steam_platform.h"

#include <cstdio>
#include <filesystem>
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

    // pump/shutdown must be harmless before a session exists.
    steam_platform::pumpCallbacks();
    steam_platform::shutdown();

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

    check("cloud sync is unimplemented",
          steam_platform::cloudSyncUserShips() == steam_platform::HookResult::Unimplemented);
    check("workshop validate is unimplemented",
          steam_platform::workshopValidate("user_ships/test_sloop.ship.json")
              == steam_platform::HookResult::Unimplemented);

    steam_platform::shutdown();
    check("steam is offline after shutdown", !steam_platform::isSteam());
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
