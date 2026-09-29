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

bool hasWorkshopCode(const std::vector<steam_platform::WorkshopIssue>& issues, const char* code) {
    for (const auto& issue : issues) {
        if (issue.code == code) return true;
    }
    return false;
}

std::string shipPiecesJson(int count) {
    std::string json = R"({
  "schema_version": 1,
  "display_name": "Cap Test",
  "bounds": { "length": 12, "width": 4, "height": 5 },
  "systems": { "helm_count": 1, "sail_count": 1, "mast_count": 1, "cannon_count": 0, "cargo_mass": 0 },
  "pieces": [)";
    for (int i = 0; i < count; ++i) {
        if (i) json += ',';
        json += "{\"id\":\"p" + std::to_string(i) +
                "\",\"type\":\"plank\",\"material\":\"oak\",\"volume\":1,\"mass\":1}";
    }
    json += "]}";
    return json;
}

bool writeTextFile(const std::filesystem::path& path, const std::string& body) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << body;
    return static_cast<bool>(out);
}

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
    check("workshop message starts empty", steam_platform::workshopMessage().empty());
    check("workshop status starts unimplemented",
          steam_platform::lastWorkshopResult() == steam_platform::HookResult::Unimplemented);

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
    const fs::path workshopDir = fs::temp_directory_path() / "sea_trial_workshop_selftest";
    std::error_code workshopEc;
    fs::remove_all(workshopDir, workshopEc);
    fs::create_directories(workshopDir / "nested", workshopEc);

    const char* kValid = R"({
  "schema_version": 1,
  "ship_id": "workshop_min",
  "display_name": "Workshop Min",
  "created_by": "selftest",
  "build_version": "prototype",
  "bounds": { "length": 12.0, "width": 4.0, "depth": 2.8, "height": 5.0 },
  "runtime": {
    "buoyancy_tier": 2,
    "sample_grid": { "type": "rectangular", "rows": 3, "columns": 3 },
    "collision_asset": "generated",
    "visual_asset": "generated"
  },
  "systems": {
    "helm_count": 1,
    "sail_count": 1,
    "mast_count": 1,
    "cannon_count": 0,
    "cargo_mass": 0e0
  },
  "pieces": [
    {
      "id": "hull_001",
      "type": "plank",
      "material": "oak",
      "volume": 4e0,
      "mass": 800.0,
      "damage": 0,
      "position": [0, 0, 0],
      "rotation": [0, 0, 0],
      "bounds": [4, 2, 12]
    }
  ]
})";

    const fs::path validPath = workshopDir / "nested" / "workshop_min.ship.json";
    const fs::path noHelmPath = workshopDir / "no_helm.ship.json";
    const fs::path noSailPath = workshopDir / "no_sail.ship.json";
    const fs::path emptyPiecesPath = workshopDir / "empty_pieces.ship.json";
    const fs::path sinkPath = workshopDir / "sink.ship.json";
    const fs::path damagedPath = workshopDir / "damaged.ship.json";
    const fs::path capPath = workshopDir / "over_cap.ship.json";
    const fs::path underCapPath = workshopDir / "at_cap.ship.json";
    const fs::path bigBoundsPath = workshopDir / "big_bounds.ship.json";
    const fs::path edgeBoundsPath = workshopDir / "edge_bounds.ship.json";
    const fs::path schemaPath = workshopDir / "bad_schema.ship.json";
    const fs::path schemaTypePath = workshopDir / "schema_type.ship.json";
    const fs::path badIdPath = workshopDir / "bad_id.ship.json";
    const fs::path badVolumePath = workshopDir / "bad_volume.ship.json";
    const fs::path narrowPath = workshopDir / "narrow.ship.json";
    const fs::path junkPath = workshopDir / "junk.ship.json";
    const fs::path emptyFilePath = workshopDir / "empty.ship.json";
    const fs::path bigFilePath = workshopDir / "big.ship.json";
    const fs::path notesPath = workshopDir / "notes.json";
    const fs::path dirPath = workshopDir / "folder.ship.json";
    const fs::path samplePath = workshopDir / "sample_sloop.ship.json";

    auto withHelm = [&](const char* systems, const char* pieces, const char* bounds =
                            R"("bounds": { "length": 12, "width": 4, "height": 5 })") {
        return std::string("{\n  \"schema_version\": 1,\n  ") + bounds + ",\n  \"systems\": " + systems +
               ",\n  \"pieces\": " + pieces + "\n}\n";
    };
    const char* kHull =
        R"([{"id":"hull_001","type":"plank","material":"oak","volume":4,"mass":800,"damage":0}])";
    const char* kRigged =
        R"({ "helm_count": 1, "sail_count": 1, "mast_count": 1, "cannon_count": 0, "cargo_mass": 0 })";

    check("workshop fixture files were written",
          writeTextFile(validPath, kValid)
              && writeTextFile(noHelmPath, withHelm(
                     R"({ "helm_count": 0, "sail_count": 1, "mast_count": 1, "cannon_count": 0, "cargo_mass": 0 })",
                     kHull))
              && writeTextFile(noSailPath, withHelm(
                     R"({ "helm_count": 1, "sail_count": 0, "mast_count": 0, "cannon_count": 0, "cargo_mass": 0 })",
                     kHull))
              && writeTextFile(emptyPiecesPath, withHelm(kRigged, "[]"))
              && writeTextFile(sinkPath, withHelm(
                     R"({ "helm_count": 1, "sail_count": 1, "mast_count": 1, "cannon_count": 0, "cargo_mass": 100000 })",
                     kHull))
              && writeTextFile(damagedPath, withHelm(
                     kRigged,
                     R"([{"id":"hull_001","type":"plank","material":"oak","volume":1,"mass":1,"damage":1}])"))
              && writeTextFile(capPath, shipPiecesJson(221))
              && writeTextFile(underCapPath, shipPiecesJson(220))
              && writeTextFile(bigBoundsPath, withHelm(
                     kRigged, kHull, R"("bounds": { "length": 27, "width": 4, "height": 5 })"))
              && writeTextFile(edgeBoundsPath, withHelm(
                     kRigged, kHull, R"("bounds": { "length": 26, "width": 9, "height": 10 })"))
              && writeTextFile(schemaPath, std::string("{\n  \"schema_version\": 2,\n  ") +
                     R"("bounds": { "length": 12, "width": 4, "height": 5 },)" + "\n  \"systems\": " + kRigged +
                     ",\n  \"pieces\": " + kHull + "\n}\n")
              && writeTextFile(schemaTypePath, "{\n  \"schema_version\": \"1\"\n}\n")
              && writeTextFile(badIdPath, withHelm(
                     kRigged,
                     R"([{"id":"","type":"plank","material":"oak","volume":4,"mass":800}])"))
              && writeTextFile(badVolumePath, withHelm(
                     kRigged,
                     R"([{"id":"hull_001","type":"plank","material":"oak","volume":0,"mass":10}])"))
              && writeTextFile(narrowPath, withHelm(
                     kRigged, kHull, R"("bounds": { "length": 12, "width": 2, "height": 5 })"))
              && writeTextFile(junkPath, "{")
              && writeTextFile(emptyFilePath, "")
              && writeTextFile(notesPath, "{}\n"));

    {
        std::ofstream big(bigFilePath, std::ios::binary | std::ios::trunc);
        const std::string chunk(1024, 'x');
        for (int i = 0; i < 1024; ++i) big << chunk;
        big << 'x';
    }
    fs::create_directory(dirPath, workshopEc);

    sea::ShipConfig sampleCfg;
    sampleCfg.name = "Say \"ahoy\"";
    check("sample ship json was written",
          writeTextFile(samplePath, sea::toShipJson(sea::makeShipFromConfig(sampleCfg))));

    using steam_platform::HookResult;
    const auto missing = steam_platform::workshopValidate((workshopDir / "missing.ship.json").string());
    check("missing ship file cannot be read",
          missing.result == HookResult::Failed && missing.code == "CANNOT_READ");
    const auto emptyPath = steam_platform::workshopValidate("");
    check("empty workshop path is rejected",
          emptyPath.result == HookResult::Failed && emptyPath.code == "BAD_PATH");
    const auto notes = steam_platform::workshopValidate(notesPath.string());
    check("non-ship file is rejected",
          notes.result == HookResult::Failed && notes.code == "NOT_A_SHIP");
    const auto dotDot = steam_platform::workshopValidate((workshopDir / "..ship.json").string());
    check("parent-segment ship name is rejected",
          dotDot.result == HookResult::Failed && dotDot.code == "NOT_A_SHIP");
    const auto asDir = steam_platform::workshopValidate(dirPath.string());
    check("directory named like a ship is rejected",
          asDir.result == HookResult::Failed && asDir.code == "BAD_PATH");
    const auto emptyFile = steam_platform::workshopValidate(emptyFilePath.string());
    check("empty ship file is rejected",
          emptyFile.result == HookResult::Failed && emptyFile.code == "EMPTY_FILE");
    const auto junk = steam_platform::workshopValidate(junkPath.string());
    check("truncated JSON fails with BAD_JSON",
          junk.result == HookResult::Failed && junk.code == "BAD_JSON");
    const auto tooBig = steam_platform::workshopValidate(bigFilePath.string());
    check("ship over 1 MiB fails with FILE_TOO_LARGE",
          tooBig.result == HookResult::Failed && tooBig.code == "FILE_TOO_LARGE");
    const auto schemaType = steam_platform::workshopValidate(schemaTypePath.string());
    check("string schema_version is BAD_JSON",
          schemaType.result == HookResult::Failed && schemaType.code == "BAD_JSON");

    const auto valid = steam_platform::workshopValidate(validPath.string());
    check("synthetic ship passes Workshop checks",
          valid.result == HookResult::Ok && valid.code == "OK" && valid.errors.empty() && valid.warnings.empty(),
          valid.message);
    check("passing check records Ok",
          steam_platform::lastWorkshopResult() == HookResult::Ok);
    const auto validAgain = steam_platform::workshopValidate(validPath.string());
    check("workshop validate is idempotent",
          validAgain.result == HookResult::Ok && validAgain.code == "OK");

    const auto sample = steam_platform::workshopValidate(samplePath.string());
    check("generated sample ship passes Workshop checks",
          sample.result == HookResult::Ok && sample.code == "OK" && sample.errors.empty(),
          sample.message);

    const auto noHelm = steam_platform::workshopValidate(noHelmPath.string());
    check("ship without a helm fails MISSING_HELM",
          noHelm.result == HookResult::Failed && noHelm.code == "MISSING_HELM"
              && hasWorkshopCode(noHelm.errors, "MISSING_HELM"),
          noHelm.message);
    const auto noSail = steam_platform::workshopValidate(noSailPath.string());
    check("ship without a sail fails MISSING_SAIL",
          noSail.result == HookResult::Failed && noSail.code == "MISSING_SAIL"
              && hasWorkshopCode(noSail.errors, "MISSING_SAIL"),
          noSail.message);
    const auto noPieces = steam_platform::workshopValidate(emptyPiecesPath.string());
    check("ship with no pieces fails NO_PIECES",
          noPieces.result == HookResult::Failed && noPieces.code == "NO_PIECES"
              && hasWorkshopCode(noPieces.errors, "NO_PIECES")
              && hasWorkshopCode(noPieces.errors, "DOES_NOT_FLOAT")
              && noPieces.errors.size() > 1,
          noPieces.message);
    const auto sink = steam_platform::workshopValidate(sinkPath.string());
    check("overloaded ship fails DOES_NOT_FLOAT",
          sink.result == HookResult::Failed && sink.code == "DOES_NOT_FLOAT",
          sink.message);
    const auto damaged = steam_platform::workshopValidate(damagedPath.string());
    check("fully damaged hull fails DOES_NOT_FLOAT",
          damaged.result == HookResult::Failed && damaged.code == "DOES_NOT_FLOAT",
          damaged.message);
    const auto overCap = steam_platform::workshopValidate(capPath.string());
    check("221 pieces fail PIECE_CAP",
          overCap.result == HookResult::Failed && overCap.code == "PIECE_CAP",
          overCap.message);
    const auto atCap = steam_platform::workshopValidate(underCapPath.string());
    check("220 pieces pass the piece cap",
          atCap.result == HookResult::Ok && atCap.code == "OK",
          atCap.message);
    const auto bigBounds = steam_platform::workshopValidate(bigBoundsPath.string());
    check("oversized hull fails BOUNDS_CAP",
          bigBounds.result == HookResult::Failed && bigBounds.code == "BOUNDS_CAP",
          bigBounds.message);
    const auto edgeBounds = steam_platform::workshopValidate(edgeBoundsPath.string());
    check("bounds on the prototype cap still pass",
          edgeBounds.result == HookResult::Ok && edgeBounds.code == "OK",
          edgeBounds.message);
    const auto badSchema = steam_platform::workshopValidate(schemaPath.string());
    check("schema_version 2 fails BAD_SCHEMA",
          badSchema.result == HookResult::Failed && badSchema.code == "BAD_SCHEMA",
          badSchema.message);
    const auto badId = steam_platform::workshopValidate(badIdPath.string());
    check("piece without an id fails BAD_ID",
          badId.result == HookResult::Failed && badId.code == "BAD_ID",
          badId.message);
    const auto badVolume = steam_platform::workshopValidate(badVolumePath.string());
    check("piece with no volume fails BAD_VOLUME",
          badVolume.result == HookResult::Failed && badVolume.code == "BAD_VOLUME",
          badVolume.message);
    const auto narrow = steam_platform::workshopValidate(narrowPath.string());
    check("narrow hull passes with a warning",
          narrow.result == HookResult::Ok && narrow.code == "OK" && narrow.errors.empty()
              && hasWorkshopCode(narrow.warnings, "NARROW_HULL"),
          narrow.message);

    if (!online) {
        const auto published = steam_platform::workshopPublish(validPath.string());
        check("offline publish validates and does not submit",
              published.result == HookResult::Unimplemented && published.code == "NOT_SUBMITTED"
                  && published.errors.empty()
                  && published.message.find("Validated") != std::string::npos
                  && published.message.find("published") != std::string::npos,
              published.message);
        check("offline publish status is unimplemented",
              steam_platform::lastWorkshopResult() == HookResult::Unimplemented);
        const auto rejected = steam_platform::workshopPublish(noHelmPath.string());
        check("offline publish of an invalid ship fails the local check",
              rejected.result == HookResult::Failed && rejected.code == "MISSING_HELM"
                  && rejected.code != "NOT_SUBMITTED",
              rejected.message);
    }

    fs::remove_all(workshopDir, workshopEc);

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
