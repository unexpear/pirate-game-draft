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

    // Backbone: keel (the spine), then the stem (bow post) and sternpost (stern
    // post) rising from its ends. Everything else is framed/planked onto these.
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
    {
        // Stem: rises from the fore end of the keel and rakes forward.
        Piece p;
        p.id = "stem_001";
        p.type = "stem";
        p.material = cfg.material;
        p.volume = 0.22 * 0.30 * (depth * 1.4);
        p.mass = p.volume * density;
        p.position = { 0, -depth * 0.1, length * 0.5 };
        p.rotation = { -0.42, 0, 0 }; // rake the head forward
        p.bounds = { 0.22, depth * 1.4, 0.30 };
        ship.pieces.push_back(p);
    }
    {
        // Sternpost: rises near-vertically from the aft end of the keel.
        Piece p;
        p.id = "sternpost_001";
        p.type = "sternpost";
        p.material = cfg.material;
        p.volume = 0.24 * 0.32 * (depth * 1.3);
        p.mass = p.volume * density;
        p.position = { 0, -depth * 0.12, -length * 0.5 };
        p.rotation = { 0.20, 0, 0 }; // slight aft rake
        p.bounds = { 0.24, depth * 1.3, 0.32 };
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

std::vector<Projectile> fireBroadside(const Ship& ship, int side,
                                      double sx, double sy, double sz,
                                      double heading, double muzzleSpeed) {
    const int n = std::max(1, ship.systems.cannon_count);
    const double ch = std::cos(heading), sh = std::sin(heading);
    // Ship axes (in x,z): forward = (sin,cos), starboard = (cos,-sin).
    const double rx = ch, rz = -sh;   // starboard unit
    const double fx = sh, fz = ch;    // forward unit
    const double halfW = ship.bounds.width * 0.5;
    const double halfL = ship.bounds.length * 0.5;
    const int s = side >= 0 ? 1 : -1;
    std::vector<Projectile> shots;
    shots.reserve(n);
    for (int i = 0; i < n; ++i) {
        // Space the guns along the hull; vary elevation a touch for scatter.
        const double along = (n == 1) ? 0.0 : lerp(-halfL * 0.55, halfL * 0.55, i / double(n - 1));
        const double elev = 0.14 + 0.02 * std::sin(double(i) * 1.7); // ~8 deg
        const double ce = std::cos(elev), se = std::sin(elev);
        Projectile p;
        p.x = sx + rx * s * (halfW + 0.3) + fx * along;
        p.y = sy + 1.2;
        p.z = sz + rz * s * (halfW + 0.3) + fz * along;
        p.vx = rx * s * muzzleSpeed * ce;
        p.vz = rz * s * muzzleSpeed * ce;
        p.vy = muzzleSpeed * se;
        p.life = 4.0;
        p.alive = true;
        shots.push_back(p);
    }
    return shots;
}

void stepProjectiles(std::vector<Projectile>& shots, double dt) {
    for (auto& p : shots) {
        if (!p.alive) continue;
        p.x += p.vx * dt;
        p.y += p.vy * dt;
        p.z += p.vz * dt;
        p.vy -= GRAVITY * dt;
        p.life -= dt;
        if (p.life <= 0.0 || p.y < -2.5) p.alive = false;
    }
}

bool pointInHull(const Ship& target, double tx, double ty, double tz, double heading,
                 double px, double py, double pz) {
    const double dx = px - tx, dz = pz - tz;
    const double ch = std::cos(heading), sh = std::sin(heading);
    const double lx = dx * ch + dz * sh;   // inverse yaw R(-heading)
    const double lz = -dx * sh + dz * ch;
    const double vert = std::max(2.0, target.bounds.depth);
    return std::fabs(lx) <= target.bounds.width * 0.5
        && std::fabs(lz) <= target.bounds.length * 0.5
        && std::fabs(py - ty) <= vert;
}

void damageHull(Ship& ship, double amount) {
    // Flood the least-damaged intact plank (fallback: rib), so sustained fire
    // progressively drops buoyancy and founders the hull.
    Piece* best = nullptr;
    for (auto& p : ship.pieces) {
        if (p.damage >= 1.0) continue;
        if (p.type != "plank" && p.type != "rib") continue;
        if (!best || p.damage < best->damage) best = &p;
    }
    if (best) best->damage = clampd(best->damage + amount, 0.0, 1.0);
}

int resolveHits(std::vector<Projectile>& shots, Ship& target,
                double tx, double ty, double tz, double heading) {
    int hits = 0;
    for (auto& p : shots) {
        if (!p.alive) continue;
        if (pointInHull(target, tx, ty, tz, heading, p.x, p.y, p.z)) {
            p.alive = false;
            damageHull(target, 0.34);
            ++hits;
        }
    }
    return hits;
}

AiOrders aiCaptain(double ex, double ez, double eHeading,
                   double ox, double oz, double engageRange, bool reloadReady) {
    AiOrders o;
    const double dx = ox - ex, dz = oz - ez;
    const double range = std::sqrt(dx * dx + dz * dz);
    // Bearing to the foe relative to our heading (forward = (sin,cos)).
    const double ch = std::cos(eHeading), sh = std::sin(eHeading);
    const double fwd = dx * sh + dz * ch;   // ahead(+) / astern(-)
    const double stb = dx * ch - dz * sh;   // starboard(+) / port(-)
    const double bearing = std::atan2(stb, fwd); // 0 = dead ahead
    const double absB = std::fabs(bearing);
    const double beam = PI * 0.5;

    // Sail: crowd on to close, ease to half sail once in the fight.
    o.sailTier = range > engageRange * 1.6 ? 2 : 1;

    if (range > engageRange) {
        // Pursue: point the bow at the foe.
        o.steer = bearing > 0.05 ? 1 : (bearing < -0.05 ? -1 : 0);
    } else {
        // In range: swing the foe onto the beam to present a broadside.
        if (absB < beam - 0.15) o.steer = bearing > 0 ? -1 : 1;
        else if (absB > beam + 0.15) o.steer = bearing > 0 ? 1 : -1;
        else o.steer = 0;
        // Fire the bearing side when roughly abeam, in range, and reloaded.
        if (reloadReady && absB > beam - 0.5 && range < engageRange * 1.2)
            o.fireSide = bearing > 0 ? 1 : -1;
    }
    return o;
}

double sailPower(double windAngleOffBowDeg) {
    double a = clampd(std::fabs(windAngleOffBowDeg), 0.0, 180.0);
    // Polar of boat-speed fraction vs wind angle off the bow. No-go zone near the
    // wind, peak on a reach (~110 deg), tapering to a slower dead run.
    struct Pt { double a, p; };
    static const Pt tbl[] = {
        { 0, 0.00 }, { 30, 0.00 }, { 43, 0.06 }, { 50, 0.42 }, { 60, 0.66 },
        { 75, 0.85 }, { 90, 0.95 }, { 110, 1.00 }, { 130, 0.94 },
        { 150, 0.80 }, { 170, 0.66 }, { 180, 0.62 },
    };
    const int n = int(sizeof(tbl) / sizeof(tbl[0]));
    if (a <= tbl[0].a) return tbl[0].p;
    for (int i = 1; i < n; ++i) {
        if (a <= tbl[i].a) {
            const double f = (a - tbl[i - 1].a) / (tbl[i].a - tbl[i - 1].a);
            return lerp(tbl[i - 1].p, tbl[i].p, f);
        }
    }
    return tbl[n - 1].p;
}

const char* traditionName(BuildTradition t) {
    switch (t) {
        case BuildTradition::Roman:  return "Roman (shell-first, carvel M&T)";
        case BuildTradition::Viking: return "Viking (shell-first, clinker rivets)";
        case BuildTradition::English: return "English Age-of-Sail (frame-first carvel)";
    }
    return "?";
}

bool isFrameFirst(BuildTradition t) { return t == BuildTradition::English; }

std::vector<int> buildOrder(const Ship& ship, BuildTradition t) {
    const bool frameFirst = isFrameFirst(t);
    auto stage = [&](const std::string& type) -> int {
        if (type == "keel") return 0;                         // laid first
        if (type == "stem" || type == "sternpost") return 1;  // rest of the backbone
        if (type == "deck") return 4;                         // deck always last
        const bool frame = (type == "rib");
        // shell-first: planks(2) then frames(3). frame-first: frames(2) then planks(3).
        if (frameFirst) return frame ? 2 : 3;
        return frame ? 3 : 2;
    };
    std::vector<int> idx(ship.pieces.size());
    for (int i = 0; i < static_cast<int>(ship.pieces.size()); ++i) idx[i] = i;
    std::stable_sort(idx.begin(), idx.end(), [&](int a, int b) {
        return stage(ship.pieces[a].type) < stage(ship.pieces[b].type);
    });
    return idx;
}

std::vector<std::string> buildSequence(BuildTradition t) {
    switch (t) {
        case BuildTradition::Roman: return {
            "Lay the keel, then the stem & sternpost",
            "Fasten the garboard strake to the keel",
            "Build strakes up from the keel, flush (carvel)",
            "Lock every seam with pegged mortise-and-tenon",
            "Shell now rigid - drop in the frames",
            "Fit the deck beams and mast step",
        };
        case BuildTradition::Viking: return {
            "Lay the T-keel, scarf on stem & sternpost",
            "Rivet the garboard strake to the keel",
            "Rivet each strake overlapping the last (clinker)",
            "Clench every rivet over an iron rove",
            "Lash the ribs to cleats on the finished shell",
            "Add crossbeams and the kerling mast-step",
        };
        case BuildTradition::English: return {
            "Scarph & bolt the elm keel; rabbet its sides",
            "Stem scarphed on; oak sternpost tenoned in",
            "Bolt the floors, run ribbands, infill the frames",
            "Raise futtocks & top timbers - skeleton done",
            "Plank flush on the frames from the wales down (treenails)",
            "Keelson, deck beams, knees; oakum the seams",
        };
    }
    return {};
}

bool keepOutsideCircle(double& px, double& pz, double cx, double cz, double radius) {
    const double dx = px - cx, dz = pz - cz;
    const double d = std::sqrt(dx * dx + dz * dz);
    if (d >= radius) return false;      // already clear
    if (d < 1e-9) { pz = cz + radius; return true; } // at the centre: push along +z
    const double s = radius / d;
    px = cx + dx * s;
    pz = cz + dz * s;
    return true;
}

ApparentWind apparentWind(double windDir, double trueWindSpeed,
                          double heading, double boatSpeed) {
    // Air velocity relative to the boat = true-wind velocity - boat velocity.
    const double ax = trueWindSpeed * std::sin(windDir) - boatSpeed * std::sin(heading);
    const double az = trueWindSpeed * std::cos(windDir) - boatSpeed * std::cos(heading);
    ApparentWind a;
    a.dir = std::atan2(ax, az);
    a.speed = std::sqrt(ax * ax + az * az);
    return a;
}

const char* pointOfSail(double windAngleOffBowDeg) {
    const double a = std::fabs(windAngleOffBowDeg);
    if (a < 43.0) return "in irons";
    if (a < 60.0) return "close-hauled";
    if (a < 80.0) return "close reach";
    if (a < 100.0) return "beam reach";
    if (a < 150.0) return "broad reach";
    return "running";
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

    // --- Gunnery ---
    {
        auto stbd = fireBroadside(base, +1, 0, 0, 0, 0.0);
        bool spawned = !stbd.empty();
        for (const auto& p : stbd) spawned = spawned && p.alive;
        push("Broadside spawns live cannonballs", spawned, num(double(stbd.size())) + " shots");

        auto port = fireBroadside(base, -1, 0, 0, 0, 0.0);
        const bool sided = !stbd.empty() && !port.empty() && stbd[0].vx > 0.0 && port[0].vx < 0.0;
        push("Broadside fires to the correct side", sided,
             sided ? "" : "starboard/port velocities wrong");

        std::vector<Projectile> arc = { stbd[0] };
        const double vy0 = arc[0].vy;
        for (int i = 0; i < 120; ++i) stepProjectiles(arc, 1.0 / 60.0);
        push("Gravity arcs the cannonball down", arc[0].vy < vy0);

        Ship target = makeShipFromConfig(baseCfg);
        const double before = getShipStats(target).damageRatio;
        std::vector<Projectile> point = { Projectile{ 0, 0, 0, 0, 0, 0, 4.0, true } };
        const int hit = resolveHits(point, target, 0, 0, 0, 0.0);
        const bool hitWorks = hit == 1 && !point[0].alive && getShipStats(target).damageRatio > before;
        push("A shot inside the hull hits and damages it", hitWorks);

        std::vector<Projectile> miss = { Projectile{ 1000, 0, 1000, 0, 0, 0, 4.0, true } };
        const int missed = resolveHits(miss, target, 0, 0, 0, 0.0);
        push("A shot outside the hull misses", missed == 0 && miss[0].alive);

        Ship victim = makeShipFromConfig(baseCfg);
        for (int i = 0; i < 400 && !getShipStats(victim).sinking; ++i) {
            std::vector<Projectile> volley = { Projectile{ 0, 0, 0, 0, 0, 0, 4.0, true } };
            resolveHits(volley, victim, 0, 0, 0, 0.0);
        }
        push("Sustained fire founders the target", getShipStats(victim).sinking);
    }

    // --- Enemy AI captain ---
    {
        const double eng = 30.0;
        // Far foe ahead-and-starboard: crowd sail and turn toward it, hold fire.
        AiOrders far = aiCaptain(0, 0, 0, 40, 200, eng, true);
        push("AI crowds sail and closes when far", far.sailTier == 2 && far.steer == 1 && far.fireSide == 0);

        // Foe abeam to starboard, in range, reloaded: fire starboard.
        AiOrders stbd = aiCaptain(0, 0, 0, 25, 0, eng, true);
        push("AI fires the starboard broadside when abeam", stbd.fireSide == 1);
        // Foe abeam to port: fire port.
        AiOrders port = aiCaptain(0, 0, 0, -25, 0, eng, true);
        push("AI fires the port broadside when abeam", port.fireSide == -1);

        // Foe dead ahead in range: turn to bring it onto the beam (don't just ram).
        AiOrders ahead = aiCaptain(0, 0, 0, 0, 25, eng, true);
        push("AI turns to present a broadside", ahead.steer != 0);

        // Out of range: hold fire even if reloaded.
        AiOrders oor = aiCaptain(0, 0, 0, 0, 200, eng, true);
        push("AI holds fire out of range", oor.fireSide == 0);
        // Abeam and in range but reloading: hold fire.
        AiOrders busy = aiCaptain(0, 0, 0, 25, 0, eng, false);
        push("AI holds fire while reloading", busy.fireSide == 0);
    }

    // --- Points of sail ---
    {
        push("No drive in the no-go zone (can't sail into the wind)",
             sailPower(0) == 0.0 && sailPower(30) < 0.05,
             num(sailPower(30) * 100) + "% at 30 deg");
        push("A reach is faster than a dead run", sailPower(95) > sailPower(180),
             num(sailPower(95) * 100) + "% vs " + num(sailPower(180) * 100) + "%");
        push("A reach is faster than close-hauled", sailPower(95) > sailPower(50),
             num(sailPower(95) * 100) + "% vs " + num(sailPower(50) * 100) + "%");
        const bool labels = std::string(pointOfSail(20)) == "in irons"
                         && std::string(pointOfSail(90)) == "beam reach"
                         && std::string(pointOfSail(175)) == "running";
        push("Point-of-sail labels name the angle", labels);

        // Apparent wind shifts forward and strengthens when sailing across it.
        const double twd = PI * 0.5, tws = 12.0, hd = 0.0, bs = 8.0; // true wind abeam, making 8
        const ApparentWind aw = apparentWind(twd, tws, hd, bs);
        auto offBow = [&](double blowToward) {
            const double src = blowToward + PI - hd;             // source bearing off the bow
            return std::fabs(std::atan2(std::sin(src), std::cos(src)));
        };
        const bool fwd = offBow(aw.dir) < offBow(twd) && aw.speed > tws;
        push("Apparent wind shifts forward and strengthens", fwd,
             num(offBow(aw.dir) * 57.3) + " vs " + num(offBow(twd) * 57.3) + " deg off bow");
    }

    // --- Build order (shell-first vs frame-first) ---
    {
        // Rank of each piece within a tradition's build order.
        auto ranks = [&](BuildTradition t) {
            const std::vector<int> ord = buildOrder(base, t);
            std::vector<int> rank(base.pieces.size(), 0);
            for (int i = 0; i < static_cast<int>(ord.size()); ++i) rank[ord[i]] = i;
            return rank;
        };
        auto planksBeforeFrames = [&](BuildTradition t) {
            const std::vector<int> rank = ranks(t);
            int maxPlank = -1, minRib = 1 << 30;
            for (int i = 0; i < static_cast<int>(base.pieces.size()); ++i) {
                if (base.pieces[i].type == "plank") maxPlank = std::max(maxPlank, rank[i]);
                if (base.pieces[i].type == "rib") minRib = std::min(minRib, rank[i]);
            }
            return maxPlank < minRib;
        };
        push("Shell-first (Viking) lays planks before frames", planksBeforeFrames(BuildTradition::Viking));
        push("Shell-first (Roman) lays planks before frames", planksBeforeFrames(BuildTradition::Roman));
        push("Frame-first (English) raises frames before planks", !planksBeforeFrames(BuildTradition::English));

        const std::vector<int> ord = buildOrder(base, BuildTradition::English);
        const bool ends = base.pieces[ord.front()].type == "keel" && base.pieces[ord.back()].type == "deck";
        push("Keel is laid first and the deck fitted last", ends);

        // Backbone: a stem and a sternpost exist, laid (with the keel) before planking.
        int stems = 0, sternposts = 0;
        for (const auto& p : base.pieces) {
            if (p.type == "stem") ++stems;
            if (p.type == "sternpost") ++sternposts;
        }
        push("Ship has a stem and a sternpost (the backbone)", stems == 1 && sternposts == 1,
             num(stems) + " stem, " + num(sternposts) + " sternpost");

        std::vector<int> rk(base.pieces.size(), 0);
        for (int i = 0; i < static_cast<int>(ord.size()); ++i) rk[ord[i]] = i;
        int maxBack = -1, minPlank = 1 << 30;
        for (int i = 0; i < static_cast<int>(base.pieces.size()); ++i) {
            const std::string& t = base.pieces[i].type;
            if (t == "keel" || t == "stem" || t == "sternpost") maxBack = std::max(maxBack, rk[i]);
            if (t == "plank") minPlank = std::min(minPlank, rk[i]);
        }
        push("The whole backbone is laid before the planking", maxBack < minPlank);
    }

    // --- Island collision ---
    {
        // A point inside the island circle is pushed out to the shore.
        double px = 5.0, pz = 122.0;
        const bool hit = keepOutsideCircle(px, pz, 0.0, 120.0, 56.0);
        const double d = std::sqrt(px * px + (pz - 120.0) * (pz - 120.0));
        push("Collision runs the hull aground at the shore", hit && std::fabs(d - 56.0) < 1e-6);

        // Open water passes untouched.
        double ox = 0.0, oz = 0.0;
        const bool clear = keepOutsideCircle(ox, oz, 0.0, 120.0, 56.0);
        push("Open water passes the collision check", !clear && ox == 0.0 && oz == 0.0);

        // Dead centre still resolves to the boundary (no divide-by-zero).
        double cx = 0.0, cz = 120.0;
        const bool cHit = keepOutsideCircle(cx, cz, 0.0, 120.0, 56.0);
        const double cd = std::sqrt(cx * cx + (cz - 120.0) * (cz - 120.0));
        push("Collision at the exact centre still resolves", cHit && std::fabs(cd - 56.0) < 1e-6);
    }

    return r;
}

} // namespace sea
