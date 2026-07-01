// Sea Trial — native ship model (Milestone 0). Public domain (The Unlicense).
#include "ship_model.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <sstream>

namespace sea {

namespace {

constexpr double PI = 3.14159265358979323846;

double clampd(double v, double lo, double hi) { return std::max(lo, std::min(hi, v)); }
double lerp(double a, double b, double t) { return a + (b - a) * t; }

// FNV-1a-ish hash matching the spike's seededHash (Math.imul + >>> 0 semantics
// map to uint32_t wraparound).
uint32_t seededHash(const std::string& seed) {
    uint32_t h = 2166136261u;
    for (char c : seed) {
        h ^= static_cast<uint32_t>(static_cast<unsigned char>(c));
        h = h * 16777619u;
    }
    return h;
}

// Canonical double formatting with full round-trip precision, so
// deserialize(serialize(x)) is bit-exact and re-serialization is idempotent.
std::string d2s(double v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.17g", v);
    return buf;
}

std::string pad3(int n) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%03d", n);
    return buf;
}

bool hasCode(const std::vector<Issue>& issues, const std::string& code) {
    for (const auto& i : issues)
        if (i.code == code) return true;
    return false;
}

} // namespace

double materialDensity(const std::string& key) {
    if (key == "pine") return 430.0;
    if (key == "teak") return 650.0;
    return 700.0; // oak / default
}

Ship makeShipFromConfig(const ShipConfig& cfg) {
    const double density = materialDensity(cfg.material);
    const double length = cfg.length;
    const double width = cfg.width;
    const double depth = cfg.depth;
    const int plankCount = std::max(10, static_cast<int>(std::round(length * 1.35)));
    const int ribCount = std::max(5, static_cast<int>(std::round(length / 1.4)));

    Ship ship;
    ship.schema_version = 1;
    ship.ship_id = "test_sloop_001";
    ship.display_name = cfg.name.empty() ? "Test Sloop" : cfg.name;
    ship.created_by = "local";
    ship.build_version = "native-m0";
    ship.bounds = { length, width, depth, depth + 5.4 };
    ship.systems.helm_count = cfg.hasHelm ? 1 : 0;
    ship.systems.sail_count = cfg.hasSail ? 1 : 0;
    ship.systems.mast_count = cfg.hasSail ? 1 : 0;
    ship.systems.cannon_count = cfg.cannonCount;
    ship.systems.cargo_mass = cfg.cargoMass;

    // Keel
    {
        Piece p;
        p.id = "keel_001";
        p.type = "keel";
        p.material = cfg.material;
        p.volume = length * 0.14 * 0.18;
        p.mass = p.volume * density;
        p.position = { 0, -depth * 0.55, 0 };
        p.bounds = { 0.18, 0.18, length };
        ship.pieces.push_back(p);
    }

    // Ribs
    for (int i = 0; i < ribCount; ++i) {
        const double z = lerp(-length / 2, length / 2, i / static_cast<double>(std::max(1, ribCount - 1)));
        const double curve = std::sin((i / static_cast<double>(std::max(1, ribCount - 1))) * PI);
        const double ribWidth = width * (0.55 + curve * 0.36);
        Piece p;
        p.id = "rib_" + pad3(i + 1);
        p.type = "rib";
        p.material = cfg.material;
        p.volume = ribWidth * depth * 0.014;
        p.mass = p.volume * density;
        p.position = { 0, -depth * 0.16, z };
        p.bounds = { ribWidth, depth * 0.75, 0.07 };
        ship.pieces.push_back(p);
    }

    // Planks (port then starboard)
    for (int side = -1; side <= 1; side += 2) {
        for (int i = 0; i < plankCount; ++i) {
            const double z = lerp(-length / 2, length / 2, i / static_cast<double>(std::max(1, plankCount - 1)));
            const double curve = std::sin((i / static_cast<double>(std::max(1, plankCount - 1))) * PI);
            const double x = side * (width * 0.32 + curve * width * 0.12);
            const double y = -depth * 0.25 + curve * depth * 0.18;
            Piece p;
            p.id = std::string("plank_") + (side > 0 ? "starboard_" : "port_") + pad3(i + 1);
            p.type = "plank";
            p.material = cfg.material;
            p.volume = (length / plankCount) * depth * 0.04;
            p.mass = p.volume * density;
            p.position = { x, y, z };
            p.rotation = { 0, side * 0.18, side * 0.16 };
            p.bounds = { 0.12, depth * 0.58, length / plankCount };
            ship.pieces.push_back(p);
        }
    }

    // Deck
    {
        const double deckVolume = length * width * 0.026;
        Piece p;
        p.id = "deck_001";
        p.type = "deck";
        p.material = cfg.material;
        p.volume = deckVolume;
        p.mass = deckVolume * density;
        p.position = { 0, depth * 0.03, 0 };
        p.bounds = { width * 0.78, 0.08, length * 0.82 };
        ship.pieces.push_back(p);
    }

    return ship;
}

Stats getShipStats(const Ship& ship) {
    Stats s;
    for (const auto& p : ship.pieces) {
        const double vol = std::max(0.0, p.volume);
        s.activeVolume += vol * (1.0 - clampd(p.damage, 0.0, 1.0));
        s.maxVolume += vol;
        s.hullMass += std::max(0.0, p.mass);
        if (p.damage < 1.0) s.healthyPieces++;
    }
    s.pieceCount = static_cast<int>(ship.pieces.size());
    s.cargoMass = ship.systems.cargo_mass;
    s.cannonMass = ship.systems.cannon_count * 360.0;
    s.rigMass = ship.systems.sail_count * 160.0 + ship.systems.mast_count * 220.0 + ship.systems.helm_count * 45.0;
    s.crewMass = 240.0;
    s.mass = s.hullMass + s.cargoMass + s.cannonMass + s.rigMass + s.crewMass;
    s.buoyancyScore = s.activeVolume * WATER_DENSITY;
    s.floatMargin = s.buoyancyScore - s.mass;
    s.damageRatio = s.maxVolume > 0 ? 1.0 - s.activeVolume / s.maxVolume : 1.0;
    s.submergedRatio = s.buoyancyScore > 0 ? clampd(s.mass / s.buoyancyScore, 0.0, 1.5) : 1.5;
    s.sinking = s.floatMargin < 0;
    return s;
}

ValidationResult validateShip(const Ship& ship) {
    ValidationResult r;
    r.stats = getShipStats(ship);
    const Stats& stats = r.stats;
    const Bounds& b = ship.bounds;

    auto err = [&](const char* c, const char* m) { r.errors.push_back({ c, m }); };
    auto warn = [&](const char* c, const char* m) { r.warnings.push_back({ c, m }); };

    if (ship.schema_version != 1) err("BAD_SCHEMA", "schema_version must be 1.");
    if (ship.pieces.empty()) err("NO_PIECES", "Ship must contain pieces.");
    if (ship.systems.helm_count < 1) err("MISSING_HELM", "Ship needs a helm.");
    if (ship.systems.sail_count < 1) err("MISSING_SAIL", "Ship needs a sail.");
    if (stats.mass <= 0) err("BAD_MASS", "Ship mass must be positive.");
    if (stats.maxVolume <= 0) err("BAD_VOLUME", "Ship volume must be positive.");
    if (stats.floatMargin <= 0) err("DOES_NOT_FLOAT", "Ship must float when loaded.");
    if (stats.pieceCount > 220) err("PIECE_CAP", "Prototype piece cap is 220.");
    if (b.length > 26 || b.width > 9 || b.height > 10) err("BOUNDS_CAP", "Prototype ship is too large.");

    for (const auto& p : ship.pieces) {
        if (p.id.empty()) err("BAD_ID", "Every piece needs an id.");
        if (!std::isfinite(p.volume) || p.volume <= 0) err("BAD_VOLUME", "Piece has invalid volume.");
        if (!std::isfinite(p.mass) || p.mass <= 0) err("BAD_MASS", "Piece has invalid mass.");
    }

    if (stats.floatMargin > 0 && stats.floatMargin < stats.mass * 0.18)
        warn("LOW_FLOAT_MARGIN", "Ship floats, but spare buoyancy is low.");
    if (b.width / std::max(1.0, b.length) < 0.24)
        warn("NARROW_HULL", "Narrow hull may be unstable.");
    if (stats.damageRatio > 0.3)
        warn("HIGH_DAMAGE", "Damage has removed major buoyancy.");

    r.valid = r.errors.empty();
    return r;
}

std::vector<Wave> makeWaveField(const std::string& seed) {
    const uint32_t base = seededHash(seed);
    auto rnd = [&](int n) {
        double x = std::sin(static_cast<double>(base) + n * 999.331) * 10000.0;
        return x - std::floor(x);
    };
    return {
        { 0.15 + rnd(1) * 0.5, 0.55, 15, 1.4, rnd(2) * PI * 2 },
        { 1.25 + rnd(3) * 0.6, 0.28, 8, 2.0, rnd(4) * PI * 2 },
        { 2.6 + rnd(5) * 0.5, 0.14, 5, 2.7, rnd(6) * PI * 2 },
    };
}

WaterSample sampleWater(const std::vector<Wave>& waves, double x, double z, double t) {
    double height = 0, dx = 0, dz = 0;
    for (const auto& w : waves) {
        const double dirX = std::cos(w.direction);
        const double dirZ = std::sin(w.direction);
        const double k = (PI * 2) / w.wavelength;
        const double f = k * (dirX * x + dirZ * z) + w.phase + t * w.speed;
        const double s = std::sin(f);
        const double c = std::cos(f);
        height += w.amplitude * s;
        dx += w.amplitude * k * dirX * c;
        dz += w.amplitude * k * dirZ * c;
    }
    return { height, dx, dz };
}

FloatPose computeFloatPose(const Ship& ship, const std::vector<Wave>& waves, double t,
                           double worldX, double worldZ, double heading) {
    // Tier-2 3x3 sample grid over the ship's footprint, placed at the ship's
    // world position and rotated by heading so it stays consistent with the
    // scrolling ocean as the ship sails.
    const double halfW = ship.bounds.width * 0.38;
    const double halfL = ship.bounds.length * 0.42;
    const double xs[3] = { -halfW, 0.0, halfW };
    const double zs[3] = { -halfL, 0.0, halfL };
    const double ch = std::cos(heading);
    const double sh = std::sin(heading);

    double sum = 0, front = 0, rear = 0, left = 0, right = 0;
    int fc = 0, rc = 0, lc = 0, rr = 0;
    for (double lx : xs) {
        for (double lz : zs) {
            const double wx = worldX + (lx * ch - lz * sh);
            const double wz = worldZ + (lx * sh + lz * ch);
            const double h = sampleWater(waves, wx, wz, t).height;
            sum += h;
            if (lz > 0) { front += h; ++fc; }
            if (lz < 0) { rear += h; ++rc; }
            if (lx < 0) { left += h; ++lc; }
            if (lx > 0) { right += h; ++rr; }
        }
    }
    const double avg = sum / 9.0;

    // Ride height: the hull origin sits above the mean surface by a freeboard
    // that scales with spare buoyancy (heavier / more damaged => rides lower).
    const Stats st = getShipStats(ship);
    const double buoyantLift = clampd(st.floatMargin / std::max(1.0, st.mass), -1.0, 1.0);
    const double freeboard = ship.bounds.depth * 0.25 * (1.0 + buoyantLift * 3.0);

    FloatPose p;
    p.heaveY = avg + freeboard;
    p.pitch = std::atan2(front / std::max(1, fc) - rear / std::max(1, rc), std::max(1.0, ship.bounds.length));
    p.heel = std::atan2(right / std::max(1, rr) - left / std::max(1, lc), std::max(1.0, ship.bounds.width));
    return p;
}

std::string serialize(const Ship& s) {
    std::string o;
    o += "schema_version " + std::to_string(s.schema_version) + "\n";
    o += "ship_id " + s.ship_id + "\n";
    o += "display_name " + s.display_name + "\n";
    o += "created_by " + s.created_by + "\n";
    o += "build_version " + s.build_version + "\n";
    o += "bounds " + d2s(s.bounds.length) + " " + d2s(s.bounds.width) + " " + d2s(s.bounds.depth) + " " + d2s(s.bounds.height) + "\n";
    o += "systems " + std::to_string(s.systems.helm_count) + " " + std::to_string(s.systems.sail_count) + " " +
         std::to_string(s.systems.mast_count) + " " + std::to_string(s.systems.cannon_count) + " " + d2s(s.systems.cargo_mass) + "\n";
    o += "pieces " + std::to_string(s.pieces.size()) + "\n";
    for (const auto& p : s.pieces) {
        o += "piece " + p.id + " " + p.type + " " + p.material + " " +
             d2s(p.volume) + " " + d2s(p.mass) + " " + d2s(p.damage) + " " +
             d2s(p.position.x) + " " + d2s(p.position.y) + " " + d2s(p.position.z) + " " +
             d2s(p.rotation.x) + " " + d2s(p.rotation.y) + " " + d2s(p.rotation.z) + " " +
             d2s(p.bounds.x) + " " + d2s(p.bounds.y) + " " + d2s(p.bounds.z) + "\n";
    }
    return o;
}

Ship deserialize(const std::string& text) {
    Ship s;
    std::istringstream iss(text);
    std::string line;
    while (std::getline(iss, line)) {
        std::istringstream ls(line);
        std::string key;
        ls >> key;
        if (key == "schema_version") ls >> s.schema_version;
        else if (key == "ship_id") ls >> s.ship_id;
        else if (key == "display_name") ls >> s.display_name;
        else if (key == "created_by") ls >> s.created_by;
        else if (key == "build_version") ls >> s.build_version;
        else if (key == "bounds") ls >> s.bounds.length >> s.bounds.width >> s.bounds.depth >> s.bounds.height;
        else if (key == "systems") ls >> s.systems.helm_count >> s.systems.sail_count >> s.systems.mast_count >> s.systems.cannon_count >> s.systems.cargo_mass;
        else if (key == "pieces") { /* count is implied by the piece lines */ }
        else if (key == "piece") {
            Piece p;
            ls >> p.id >> p.type >> p.material >> p.volume >> p.mass >> p.damage >>
                p.position.x >> p.position.y >> p.position.z >>
                p.rotation.x >> p.rotation.y >> p.rotation.z >>
                p.bounds.x >> p.bounds.y >> p.bounds.z;
            s.pieces.push_back(p);
        }
    }
    return s;
}

std::vector<TestResult> runSelfTest() {
    std::vector<TestResult> r;
    auto eq = [](double a, double b) { return std::fabs(a - b) < 1e-6; };
    auto push = [&](const std::string& name, bool pass, const std::string& details = "") {
        r.push_back({ name, pass, details });
    };
    auto num = [](double v) { char b[32]; std::snprintf(b, sizeof(b), "%.0f", v); return std::string(b); };

    ShipConfig baseCfg;
    baseCfg.name = "SelfTest";
    Ship base = makeShipFromConfig(baseCfg);
    ValidationResult baseReport = validateShip(base);
    push("Base ship validates", baseReport.valid,
         baseReport.valid ? "" : (baseReport.errors.empty() ? "" : baseReport.errors[0].code));
    const Stats baseStats = baseReport.stats;

    Ship cargoLoaded = base;
    cargoLoaded.systems.cargo_mass = 500;
    const Stats cargoStats = getShipStats(cargoLoaded);
    push("Cargo increases mass", cargoStats.mass > baseStats.mass, num(baseStats.mass) + " -> " + num(cargoStats.mass));

    Ship cargoRemoved = cargoLoaded;
    cargoRemoved.systems.cargo_mass = 0;
    const Stats restoredStats = getShipStats(cargoRemoved);
    push("Removing cargo restores mass", eq(restoredStats.mass, baseStats.mass), num(baseStats.mass) + " vs " + num(restoredStats.mass));

    Ship damaged = base;
    for (auto& p : damaged.pieces) {
        if (p.type == "plank") { p.damage = 0.5; break; }
    }
    const Stats damagedStats = getShipStats(damaged);
    push("Damage lowers buoyancy", damagedStats.buoyancyScore < baseStats.buoyancyScore,
         num(baseStats.buoyancyScore) + " -> " + num(damagedStats.buoyancyScore));

    Ship repaired = damaged;
    for (auto& p : repaired.pieces) p.damage = 0;
    const Stats repairedStats = getShipStats(repaired);
    push("Repair restores buoyancy", eq(repairedStats.buoyancyScore, baseStats.buoyancyScore),
         num(baseStats.buoyancyScore) + " vs " + num(repairedStats.buoyancyScore));

    push("Added cargo lowers float margin", cargoStats.floatMargin < baseStats.floatMargin,
         num(baseStats.floatMargin) + " -> " + num(cargoStats.floatMargin));
    push("Damage lowers float margin", damagedStats.floatMargin < baseStats.floatMargin,
         num(baseStats.floatMargin) + " -> " + num(damagedStats.floatMargin));

    ShipConfig noHelmCfg = baseCfg;
    noHelmCfg.hasHelm = false;
    ValidationResult noHelmReport = validateShip(makeShipFromConfig(noHelmCfg));
    const bool helmFlagged = !noHelmReport.valid && hasCode(noHelmReport.errors, "MISSING_HELM");
    push("No-helm ship fails validation", helmFlagged, helmFlagged ? "" : "MISSING_HELM not flagged");

    ShipConfig noSailCfg = baseCfg;
    noSailCfg.hasSail = false;
    ValidationResult noSailReport = validateShip(makeShipFromConfig(noSailCfg));
    const bool sailFlagged = !noSailReport.valid && hasCode(noSailReport.errors, "MISSING_SAIL");
    push("No-sail ship fails validation", sailFlagged, sailFlagged ? "" : "MISSING_SAIL not flagged");

    const auto waves = makeWaveField("self-test-deterministic");
    const WaterSample s1 = sampleWater(waves, 12.3, -4.5, 9.1);
    const WaterSample s2 = sampleWater(waves, 12.3, -4.5, 9.1);
    const bool det = eq(s1.height, s2.height) && eq(s1.slopeX, s2.slopeX) && eq(s1.slopeZ, s2.slopeZ);
    push("Water sampling is deterministic for same seed/time/position", det, det ? "" : "non-deterministic");

    const std::string ser = serialize(base);
    const Ship restored = deserialize(ser);
    const std::string roundTripped = serialize(restored);
    push("Ship round-trips through serialize/deserialize without data loss",
         ser == roundTripped, ser == roundTripped ? "" : "serialization differs after round-trip");

    // Buoyancy pose: a heavier ship rides lower on the same water.
    const auto poseWaves = makeWaveField("selftest-pose");
    const FloatPose basePose = computeFloatPose(base, poseWaves, 3.0);
    Ship overloaded = base;
    overloaded.systems.cargo_mass = 4000; // well past sinking
    const FloatPose loadedPose = computeFloatPose(overloaded, poseWaves, 3.0);
    {
        char d[64];
        std::snprintf(d, sizeof(d), "heave %.3f -> %.3f", basePose.heaveY, loadedPose.heaveY);
        push("Heavier ship rides lower (buoyancy pose)", loadedPose.heaveY < basePose.heaveY, d);
    }

    return r;
}

} // namespace sea
