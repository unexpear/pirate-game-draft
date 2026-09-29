// Sea Trial — native ship model (Milestone 0). Public domain (The Unlicense).
#include "ship_model.hpp"
#include "island_shape.hpp"

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
        const double u = i / static_cast<double>(std::max(1, ribCount - 1)); // 0 stern .. 1 bow
        const double z = lerp(-length / 2, length / 2, u);
        // Beam profile: near-points at bow/stern, max beam slightly aft of centre
        // (the pow(u,0.82) skews the peak toward the stern) — a boat, not a box.
        const double beam = std::sin(PI * std::pow(u, 0.82));
        const double ribWidth = width * (0.10 + 0.82 * beam);
        const double sheer = depth * 0.16 * (1.0 - std::sin(PI * u)); // gunwale rises at the ends
        Piece p;
        p.id = "rib_" + pad3(i + 1);
        p.type = "rib";
        p.material = cfg.material;
        p.volume = ribWidth * depth * 0.014;
        p.mass = p.volume * density;
        p.position = { 0, -depth * 0.16 + sheer, z };
        p.bounds = { ribWidth, depth * 0.75, 0.07 };
        ship.pieces.push_back(p);
    }

    // Planks (port then starboard)
    for (int side = -1; side <= 1; side += 2) {
        for (int i = 0; i < plankCount; ++i) {
            const double u = i / static_cast<double>(std::max(1, plankCount - 1)); // 0 stern .. 1 bow
            const double z = lerp(-length / 2, length / 2, u);
            // Same skewed beam profile as the ribs: the strakes taper to a stem
            // point at the bow and a fine stern, widest slightly aft of amidships.
            const double beam = std::sin(PI * std::pow(u, 0.82));
            const double halfBeam = width * (0.05 + 0.42 * beam);
            const double x = side * halfBeam;
            const double sheer = depth * 0.16 * (1.0 - std::sin(PI * u)); // sheer rises to bow/stern
            const double y = -depth * 0.22 + sheer;
            // Toe the strakes inward toward the ends so they follow the converging
            // hull instead of standing square (sharper the nearer the stem).
            const double toeIn = -side * (u - 0.5) * 1.3;
            Piece p;
            p.id = std::string("plank_") + (side > 0 ? "starboard_" : "port_") + pad3(i + 1);
            p.type = "plank";
            p.material = cfg.material;
            p.volume = (length / plankCount) * depth * 0.04;
            p.mass = p.volume * density;
            p.position = { x, y, z };
            p.rotation = { 0, side * 0.18 + toeIn, side * 0.16 };
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
        p.position = { 0, depth * 0.03, -length * 0.02 };
        p.bounds = { width * 0.60, 0.08, length * 0.66 }; // fits within the tapered hull
        ship.pieces.push_back(p);
    }

    // --- Spars, fittings, and the standing rigging (lines). Rendered from their
    // bounds; given tiny volumes so they don't distort the crude float model
    // (the HullProfile bake carries the real hydrostatics). ---
    const double deckY = depth * 0.03;
    const double mastH = depth * 0.9 + 6.0;
    const double mastZ = -length * 0.05;
    const double mastTopY = deckY + mastH;
    const double gunY = deckY + 0.4;
    auto fit = [&](const std::string& id, const std::string& type, const std::string& mat,
                   Vec3 pos, Vec3 rot, Vec3 bnd, double vol) {
        Piece p; p.id = id; p.type = type; p.material = mat;
        p.volume = vol; p.mass = vol * materialDensity(mat);
        p.position = pos; p.rotation = rot; p.bounds = bnd;
        ship.pieces.push_back(p);
    };
    if (cfg.hasSail) {
        fit("mast_001", "mast", "pine", { 0, deckY + mastH * 0.5, mastZ }, {}, { 0.32, mastH, 0.32 }, 0.06);
        const double Lb = length * 0.42;
        fit("bowsprit_001", "bowsprit", "pine",
            { 0, deckY + 0.8, length * 0.5 + Lb * 0.32 }, { -0.5, 0, 0 }, { 0.26, 0.26, Lb }, 0.04);
        // Standing rigging: shrouds (athwartships, tilt about Z) + fore/back stays
        // (fore-aft, tilt about X). Drawn as taut straight lines.
        {
            const double dx = width * 0.42, dy = mastTopY - gunY;
            const double L = std::sqrt(dx * dx + dy * dy), a = std::atan2(dx, dy);
            fit("shroud_p", "line", "hemp", { -dx * 0.5, (gunY + mastTopY) * 0.5, mastZ }, { 0, 0, a }, { 0.06, L, 0.06 }, 0.01);
            fit("shroud_s", "line", "hemp", {  dx * 0.5, (gunY + mastTopY) * 0.5, mastZ }, { 0, 0, -a }, { 0.06, L, 0.06 }, 0.01);
        }
        {
            const double bz = length * 0.45, dz = bz - mastZ, dy = mastTopY - gunY;
            const double L = std::sqrt(dz * dz + dy * dy), a = std::atan2(dz, dy);
            fit("forestay", "line", "hemp", { 0, (gunY + mastTopY) * 0.5, (bz + mastZ) * 0.5 }, { a, 0, 0 }, { 0.06, L, 0.06 }, 0.01);
        }
        {
            const double sz = -length * 0.5, dz = mastZ - sz, dy = mastTopY - gunY;
            const double L = std::sqrt(dz * dz + dy * dy), a = std::atan2(dz, dy);
            fit("backstay", "line", "hemp", { 0, (gunY + mastTopY) * 0.5, (sz + mastZ) * 0.5 }, { -a, 0, 0 }, { 0.06, L, 0.06 }, 0.01);
        }
        // Running rigging: a halyard up the mast and two sheets to the deck.
        const double halL = mastTopY - (deckY + 0.3);
        fit("halyard", "line", "hemp", { 0.35, (deckY + 0.3 + mastTopY) * 0.5, mastZ }, {}, { 0.05, halL, 0.05 }, 0.01);
        const double shx = width * 0.35, shTop = deckY + 2.4, shBot = deckY + 0.4, shDz = length * 0.24;
        {
            const double dy = shTop - shBot, L = std::sqrt(shDz * shDz + dy * dy), a = std::atan2(shDz, dy);
            fit("sheet_p", "line", "hemp", { -shx, (shTop + shBot) * 0.5, mastZ - shDz * 0.5 }, { -a, 0, 0 }, { 0.05, L, 0.05 }, 0.01);
            fit("sheet_s", "line", "hemp", {  shx, (shTop + shBot) * 0.5, mastZ - shDz * 0.5 }, { -a, 0, 0 }, { 0.05, L, 0.05 }, 0.01);
        }
        // Blocks (pulleys) at the mast foot, masthead, and the sheet lands.
        fit("block_foot", "block", "oak", { 0.35, deckY + 0.4, mastZ }, {}, { 0.28, 0.34, 0.28 }, 0.005);
        fit("block_head", "block", "oak", { 0.0, mastTopY - 0.2, mastZ }, {}, { 0.30, 0.36, 0.30 }, 0.005);
        fit("block_sp", "block", "oak", { -shx, deckY + 0.4, mastZ - shDz }, {}, { 0.26, 0.32, 0.26 }, 0.005);
        fit("block_ss", "block", "oak", {  shx, deckY + 0.4, mastZ - shDz }, {}, { 0.26, 0.32, 0.26 }, 0.005);
    }
    if (cfg.hasHelm) {
        fit("helm_001", "helm", cfg.material, { 0, deckY + 0.7, -length * 0.34 }, {}, { 0.14, 1.3, 1.3 }, 0.03);
        fit("capstan_001", "capstan", cfg.material, { 0, deckY + 0.5, -length * 0.12 }, {}, { 0.9, 1.0, 0.9 }, 0.04);
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
    // Six trains with NON-HARMONIC wavelengths and directions SPREAD around a
    // dominant swell — so crests don't line up on a lattice (the "tiled grid"
    // read). A long dominant swell plus shorter cross-chop. { dir, amplitude,
    // wavelength, speed, phase }. Total amplitude ~1.07 (a modest sea).
    return {
        { 0.35 + rnd(1) * 0.15,  0.40, 22.7, 1.05, rnd(2)  * PI * 2 }, // long dominant swell
        { 0.05 + rnd(3) * 0.20,  0.26, 15.3, 1.35, rnd(4)  * PI * 2 },
        { 0.85 + rnd(5) * 0.20,  0.17, 10.1, 1.70, rnd(6)  * PI * 2 }, // cross chop
        { 0.20 + rnd(7) * 0.25,  0.11,  6.7, 2.10, rnd(8)  * PI * 2 },
        { 1.15 + rnd(9) * 0.30,  0.075, 4.3, 2.50, rnd(10) * PI * 2 },
        { 0.55 + rnd(11) * 0.30, 0.05,  2.9, 3.00, rnd(12) * PI * 2 },
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
    // Clamp trim so a steep swell rocks the hull without spearing the bow under
    // (a boat rides the wave; it doesn't dive into it).
    p.pitch = clampd(std::atan2(front / std::max(1, fc) - rear / std::max(1, rc), std::max(1.0, ship.bounds.length)), -0.14, 0.14);
    p.heel = clampd(std::atan2(right / std::max(1, rr) - left / std::max(1, lc), std::max(1.0, ship.bounds.width)), -0.22, 0.22);
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

void damageRig(Ship& ship, double amount) {
    // Damage the mast (the rigging). A shot-up rig loses drive (see rigIntegrity).
    for (auto& p : ship.pieces)
        if (p.type == "mast" && p.damage < 1.0) { p.damage = clampd(p.damage + amount, 0.0, 1.0); return; }
}

double rigIntegrity(const Ship& ship) {
    double sum = 0.0; int n = 0;
    for (const auto& p : ship.pieces)
        if (p.type == "mast") { sum += 1.0 - clampd(p.damage, 0.0, 1.0); ++n; }
    return n ? clampd(sum / n, 0.0, 1.0) : 1.0;
}

double tackleAdvantage(int parts) {
    return parts < 1 ? 1.0 : double(parts);
}

int resolveHits(std::vector<Projectile>& shots, Ship& target,
                double tx, double ty, double tz, double heading) {
    int hits = 0;
    for (auto& p : shots) {
        if (!p.alive) continue;
        if (pointInHull(target, tx, ty, tz, heading, p.x, p.y, p.z)) {
            p.alive = false;
            // High shots rake the rigging (slowing her); low shots flood the hull.
            if (p.y > ty + target.bounds.depth * 0.55) damageRig(target, 0.5);
            else damageHull(target, 0.34);
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
        if (type == "keel") return 0;                          // laid first
        if (type == "stem" || type == "sternpost") return 1;   // rest of the backbone
        if (type == "deck") return 4;                          // deck after the hull is planked
        if (type == "mast" || type == "bowsprit") return 5;    // step the spars
        if (type == "helm" || type == "capstan") return 6;     // fit out
        if (type == "line" || type == "block") return 7;       // reeve the rigging (stays, halyards, blocks)
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
            "Build strakes up from the keel, flush (carvel)",
            "Lock every seam with pegged mortise-and-tenon (nails)",
            "Shell now rigid - drop in the frames",
            "Fit the deck beams and lay the deck",
            "Step the mast; rig the bowsprit",
            "Fit out (helm, capstan)",
            "Reeve the rigging - stays, shrouds, halyards & sheets through the blocks",
        };
        case BuildTradition::Viking: return {
            "Lay the T-keel, scarf on stem & sternpost",
            "Rivet each strake overlapping the last (clinker planking)",
            "Clench every rivet over an iron rove (nails)",
            "Lash the ribs to cleats on the finished shell",
            "Add the crossbeams and lay the deck",
            "Step the mast in the kerling; rig the bowsprit",
            "Fit out (helm, capstan)",
            "Reeve the rigging - stays, shrouds, halyards & sheets through the blocks",
        };
        case BuildTradition::English: return {
            "Scarph the keel; step the stem & sternpost",
            "Raise the floors, futtocks & top timbers (frames)",
            "Plank flush on the frames, from the wales down",
            "Drive the treenails & iron fastenings (nails)",
            "Keelson, deck beams & knees; lay the deck",
            "Step the mast; rig the bowsprit",
            "Fit out (helm, capstan)",
            "Reeve the rigging - stays, shrouds, halyards & sheets through the blocks",
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

// --- Difficulty zones -------------------------------------------------------
int zoneLevel(const ZoneMap& m, double x, double z) {
    const double dx = x - m.harborX, dz = z - m.harborZ;
    const double d = std::sqrt(dx * dx + dz * dz);
    if (d <= m.safeRadius) return 0;
    const int lvl = 1 + int((d - m.safeRadius) / m.bandWidth);
    return lvl < m.maxLevel ? lvl : m.maxLevel;
}

int spawnLevel(const ZoneMap& m, double x, double z, double roll) {
    const int base = zoneLevel(m, x, z);
    if (base == 0) return 0;             // harbour truce: nothing spawns
    if (base >= m.maxLevel) return base; // already at the cap, no higher tier to spill in
    const double dx = x - m.harborX, dz = z - m.harborZ;
    const double d = std::sqrt(dx * dx + dz * dz);
    // Distance from this band's OUTER frontier (0 at the border, bandWidth deep in).
    const double into = (d - m.safeRadius) - double(base - 1) * m.bandWidth;
    const double toBorder = m.bandWidth - into;
    if (m.overlap > 1e-9 && toBorder < m.overlap) {
        // Nearer the frontier, likelier a heavy from the next band has strayed in.
        const double t = (m.overlap - toBorder) / m.overlap; // 0 at strip edge .. 1 at border
        if (roll < 0.65 * t) return base + 1;
    }
    return base;
}

const char* zoneShipName(int level) {
    switch (level) {
        case 0: return "Fishing Smack";
        case 1: return "Sloop";
        case 2: return "Brig";
        case 3: return "Frigate";
        case 4: return "Man-o'-War";
        case 5: return "Ship of the Line";
        default: return "Flagship";
    }
}

ShipConfig enemyConfigForLevel(int level) {
    const int L = level < 1 ? 1 : (level > 6 ? 6 : level);
    ShipConfig c;
    c.name = zoneShipName(L);
    c.length = 13.0 + 2.4 * L;   // L1 ~15.4 m .. L6 ~27.4 m
    c.width  = 5.0 + 0.7 * L;
    c.depth  = 2.7 + 0.28 * L;
    c.cannonCount = 2 * (1 + L); // L1: 4 guns (2/side) .. L6: 14 guns
    c.hasHelm = true;
    c.hasSail = true;
    return c;
}

HullProfile bakeHullProfile(const Ship& ship) {
    HullProfile p;
    const double L = std::max(1.0, ship.bounds.length);
    const double B = std::max(0.5, ship.bounds.width);
    const double T = std::max(0.3, ship.bounds.depth * 0.55); // draft ~ submerged depth
    p.length = L; p.beam = B; p.draft = T;
    p.lengthBeamRatio = L / B;

    p.blockCoeff = 0.50;                       // fullness (later: from the actual planking)
    p.displacementVol = L * B * T * p.blockCoeff;
    p.displacementMass = p.displacementVol * WATER_DENSITY;
    p.waterplaneArea = L * B * 0.85;

    // Stability proxy: metacentric height ~ Beam^2 / draft. Beamy & shallow is
    // stiff/stable; narrow & deep is tender/tippy.
    p.gm = 0.08 * (B * B) / T - 0.55;

    // Handling, relative to the reference sloop (L=12, B=4, L/B=3 -> factors ~1).
    const double refL = 12.0, refB = 4.0;
    p.topSpeedFactor = std::sqrt(L / refL) * (0.70 + 0.10 * p.lengthBeamRatio);
    p.turnFactor = (refL / L) * std::sqrt(B / refB);
    p.dragFactor = 0.85 + 0.12 * (B / refB);

    // Heel: a tender (low-GM) hull leans hard under sail; a stiff hull barely.
    p.heelFactor = clampd(1.0 / (0.5 + std::max(-0.4, p.gm)), 0.55, 1.9);
    // Acceleration: a heavier (bigger-displacement) hull gathers way more slowly.
    const double refDisp = refL * refB * (2.8 * 0.55) * 0.50 * WATER_DENSITY;
    p.accelFactor = clampd(refDisp / std::max(1.0, p.displacementMass), 0.6, 1.4);
    return p;
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

double stocksHeaveY(double depth) { return 2.9 + depth * 0.55; }

DeckFrame deckFrameFor(const Ship& ship) {
    DeckFrame d;
    d.length = ship.bounds.length;
    d.beam = ship.bounds.width;
    d.depth = ship.bounds.depth;
    // Top of the deck plank, plus a couple of centimetres so the feet sit on it
    // rather than in it. Summed with stocksHeaveY this is the yard deck height.
    d.deckY = ship.bounds.depth * 0.03 + 0.06;
    d.halfBeam = std::max(0.6, ship.bounds.width * 0.5 - 0.35);
    d.halfLength = std::max(0.8, ship.bounds.length * 0.5 - 0.35);
    d.helmX = 0.0;
    // Just forward of the wheel (the helm piece is centred at -length*0.34 and
    // about 1.3 m fore-and-aft), so the body stands at the wheel instead of in it.
    d.helmZ = clampd(-ship.bounds.length * 0.34 + 0.85, -d.halfLength + 0.4, d.halfLength - 0.4);
    return d;
}

void shipLocalToScene(double lx, double ly, double lz, const PoseTR& pose,
                      double& ox, double& oy, double& oz) {
    // bx::mtxSRT / mtxRotateZYX, column-major, M * v. Columns below.
    const double sx = std::sin(pose.pitch), cx = std::cos(pose.pitch);
    const double sy = std::sin(pose.heading), cy = std::cos(pose.heading);
    const double sz = std::sin(pose.heel), cz = std::cos(pose.heel);
    const double c0x = cy * cz;
    const double c0y = cz * sx * sy - cx * sz;
    const double c0z = cx * cz * sy + sx * sz;
    const double c1x = cy * sz;
    const double c1y = cx * cz + sx * sy * sz;
    const double c1z = -cz * sx + cx * sy * sz;
    const double c2x = -sy;
    const double c2y = cy * sx;
    const double c2z = cx * cy;
    ox = pose.originX + c0x * lx + c1x * ly + c2x * lz;
    oy = pose.heaveY + c0y * lx + c1y * ly + c2y * lz;
    oz = pose.originZ + c0z * lx + c1z * ly + c2z * lz;
}

void sceneToShipLocal(double sx, double sy, double sz, const PoseTR& pose,
                      double& lx, double& ly, double& lz) {
    const double spx = std::sin(pose.pitch), cx = std::cos(pose.pitch);
    const double spy = std::sin(pose.heading), cy = std::cos(pose.heading);
    const double shz = std::sin(pose.heel), cz = std::cos(pose.heel);
    const double c0x = cy * cz;
    const double c0y = cz * spx * spy - cx * shz;
    const double c0z = cx * cz * spy + spx * shz;
    const double c1x = cy * shz;
    const double c1y = cx * cz + spx * spy * shz;
    const double c1z = -cz * spx + cx * spy * shz;
    const double c2x = -spy;
    const double c2y = cy * spx;
    const double c2z = cx * cy;
    const double dx = sx - pose.originX, dy = sy - pose.heaveY, dz = sz - pose.originZ;
    lx = c0x * dx + c0y * dy + c0z * dz;
    ly = c1x * dx + c1y * dy + c1z * dz;
    lz = c2x * dx + c2y * dy + c2z * dz;
}

double sailWindHeel(double heading, double windDir, double sailFullness, double heelScale) {
    const double align = std::cos(heading - windDir);
    const double awa = std::acos(clampd(-align, -1.0, 1.0));
    const double lee = (std::sin(windDir - heading) >= 0.0) ? 1.0 : -1.0;
    const double power = sailPower(awa * (180.0 / PI));
    return lee * 0.34 * power * std::sin(awa) * sailFullness * heelScale;
}

double renderYawFromMove(double moveYaw) { return -moveYaw; }

PlankSpec yardGangplank(const DeckFrame& deck) {
    PlankSpec p;
    p.deployed = true;
    p.deckLocalX = 0.0;
    p.deckLocalZ = -deck.halfLength;
    p.dockX = 0.0;
    p.dockY = 1.8; // dry-dock top in the build scene
    p.dockZ = -deck.halfLength - 2.6;
    return p;
}

void harbourBerths(BerthRect out[kHarbourBerthCount]) {
    // Island-local, matching ship_view::renderIsland (y is the box centre).
    // Quay:  B(-6, -1.55, -40, 82, 6.9, 10)  top = 1.90
    // Pier 1: B(-26, 1.1, -54, 4.5, 0.8, 30) top = 1.50
    // Pier 2: B(-2,  1.1, -56, 4.5, 0.8, 34) top = 1.50
    // Pier 3: B(22,  1.1, -52, 4.5, 0.8, 26) top = 1.50
    out[0] = { -6.0, -40.0, 41.0, 5.0, 1.90 };
    out[1] = { -26.0, -54.0, 2.25, 15.0, 1.50 };
    out[2] = { -2.0, -56.0, 2.25, 17.0, 1.50 };
    out[3] = { 22.0, -52.0, 2.25, 13.0, 1.50 };
}

double nearestBerth(const BerthRect* berths, int count, double x, double z,
                    double& outX, double& outZ, double& outY) {
    if (!berths || count <= 0) { outX = x; outZ = z; outY = 0.0; return 1.0e9; }
    double best = 1.0e9;
    outX = x; outZ = z; outY = berths[0].feetY;
    for (int i = 0; i < count; ++i) {
        const BerthRect& b = berths[i];
        const double cx = clampd(x, b.cx - b.hx, b.cx + b.hx);
        const double cz = clampd(z, b.cz - b.hz, b.cz + b.hz);
        const double dx = x - cx, dz = z - cz;
        const double d = std::sqrt(dx * dx + dz * dz);
        if (d < best) { best = d; outX = cx; outZ = cz; outY = b.feetY; }
    }
    return best;
}

static bool berthContains(const BerthRect* berths, int count, double x, double z, double& y) {
    double bx, bz, by;
    const double d = nearestBerth(berths, count, x, z, bx, bz, by);
    if (d > 1.0e-6) return false;
    y = by;
    return true;
}

PlankSpec planGangplank(const DeckFrame& deck, const PoseTR& pose,
                        const BerthRect* berths, int count,
                        double berthToSceneX, double berthToSceneZ,
                        double speed, double reach, double maxSpeed) {
    PlankSpec best;
    if (!berths || count <= 0 || speed > maxSpeed) return best;
    const double edge[8][2] = {
        { 0.0, deck.halfLength }, { 0.0, -deck.halfLength },
        { -deck.halfBeam, 0.0 }, { deck.halfBeam, 0.0 },
        { -deck.halfBeam, deck.halfLength }, { deck.halfBeam, deck.halfLength },
        { -deck.halfBeam, -deck.halfLength }, { deck.halfBeam, -deck.halfLength },
    };
    double bestD = reach;
    for (const auto& e : edge) {
        double sx, sy, sz;
        shipLocalToScene(e[0], deck.deckY, e[1], pose, sx, sy, sz);
        double qx, qz, qy;
        const double d = nearestBerth(berths, count, sx - berthToSceneX, sz - berthToSceneZ, qx, qz, qy);
        if (d <= bestD) {
            bestD = d;
            best.deployed = true;
            best.deckLocalX = e[0];
            best.deckLocalZ = e[1];
            best.dockX = qx + berthToSceneX;
            best.dockZ = qz + berthToSceneZ;
            best.dockY = qy;
        }
    }
    return best;
}

static void placeAtHelm(ShipFoot& foot, const DeckFrame& deck) {
    foot.surface = FootSurface::Deck;
    foot.x = deck.helmX;
    foot.z = deck.helmZ;
    foot.face = 0.0;
    foot.plankT = 1.0;
}

bool helmInReach(const ShipFoot& foot, const DeckFrame& deck) {
    if (foot.mode != ControlMode::Foot || foot.surface != FootSurface::Deck) return false;
    const double dx = foot.x - deck.helmX, dz = foot.z - deck.helmZ;
    return dx * dx + dz * dz <= 1.05 * 1.05;
}

bool footUnderWay(const ShipFoot& foot) {
    return foot.mode == ControlMode::Helm || foot.surface == FootSurface::Deck;
}

const char* shipFootLabel(const ShipFoot& foot) {
    if (foot.mode == ControlMode::Helm) return "at the helm";
    if (foot.surface == FootSurface::Plank) return "on the gangplank";
    if (foot.surface == FootSurface::Water) return "swimming";
    if (foot.surface == FootSurface::Ground) return "ashore";
    return "on deck";
}

static void plankEnds(const PlankSpec& plank, const FootWorld& world,
                      double& deckX, double& deckY, double& deckZ) {
    shipLocalToScene(plank.deckLocalX, world.deck.deckY, plank.deckLocalZ, world.pose, deckX, deckY, deckZ);
}

static double sceneMoveYawFromLocal(double localFace, double heading) {
    // Mesh yaw sends local (sin f, cos f) to scene (sin(f - h), cos(f - h)).
    return localFace - heading;
}

// Where a ground step is allowed to stand. Land is the town (and the dry-dock
// apron): solid, but not this slice's walk. Sea is swimmable.
enum class Stand { Sea, Walk, Land };

static bool groundFeet(const FootWorld& world, double x, double z, double& y) {
    if (world.yard) {
        if (x >= world.yardMinX && x <= world.yardMaxX && z >= world.yardMinZ && z <= world.yardMaxZ) {
            y = world.yardFeetY;
            return true;
        }
        return false;
    }
    const double ilx = x - world.berthToSceneX;
    const double ilz = z - world.berthToSceneZ;
    if (berthContains(world.berths, world.berthCount, ilx, ilz, y)) return true;
    if (world.shore) {
        const float inland = island_shape::metresInland(float(ilx), float(ilz));
        if (inland >= 0.0f && inland <= float(world.shoreBeach)) {
            // A couple of centimetres so the soles sit on the sand, not in it.
            y = std::max(0.02, double(island_shape::heightAt(float(ilx), float(ilz))));
            return true;
        }
    }
    return false;
}

static Stand standAt(const FootWorld& world, double x, double z) {
    double y = 0.0;
    if (groundFeet(world, x, z, y)) return Stand::Walk;
    if (world.yard) return Stand::Land; // the stocks apron is not the sea
    if (world.shore) {
        const double ilx = x - world.berthToSceneX, ilz = z - world.berthToSceneZ;
        if (island_shape::metresInland(float(ilx), float(ilz)) > float(world.shoreBeach))
            return Stand::Land;
    }
    return Stand::Sea;
}

static void enterWater(ShipFoot& foot, double sx, double sz, double sceneFace) {
    foot.surface = FootSurface::Water;
    foot.x = sx;
    foot.z = sz;
    foot.face = sceneFace;
}

// Step off the walkable deck into the sea, just outside the hull skin so the
// body isn't left inside the mesh. Facing stays the way you were walking.
static void dropOffDeck(ShipFoot& foot, const FootWorld& world, double ox, double oz) {
    const DeckFrame& deck = world.deck;
    const double hx = std::max(0.4, deck.beam * 0.5);
    const double hz = std::max(0.4, deck.length * 0.5);
    double dx = ox, dz = oz;
    double len = std::sqrt(dx * dx + dz * dz);
    if (len < 1e-4) {
        dx = std::sin(foot.face);
        dz = std::cos(foot.face);
        len = 1.0;
    }
    const double ux = dx / len, uz = dz / len;
    const double tx = std::fabs(ux) > 1e-6 ? hx / std::fabs(ux) : 1.0e9;
    const double tz = std::fabs(uz) > 1e-6 ? hz / std::fabs(uz) : 1.0e9;
    const double clear = std::min(tx, tz) + 0.65; // past the skin, room for the body
    double sx, sy, sz;
    shipLocalToScene(ux * clear, 0.0, uz * clear, world.pose, sx, sy, sz);
    (void)sy;
    enterWater(foot, sx, sz, sceneMoveYawFromLocal(foot.face, world.pose.heading));
}

// Haul up onto the deck when the stroke carries you into the hull and the
// freeboard is one a person can reach. Swimming away leaves you in the water.
static bool climbHull(ShipFoot& foot, const FootWorld& world, double moveSign) {
    const DeckFrame& deck = world.deck;
    const double hx = std::max(0.4, deck.beam * 0.5);
    const double hz = std::max(0.4, deck.length * 0.5);
    double lx, ly, lz;
    sceneToShipLocal(foot.x, world.waterY, foot.z, world.pose, lx, ly, lz);
    (void)ly;
    const double ax = std::fabs(lx) - hx;
    const double az = std::fabs(lz) - hz;
    double outside;
    if (ax <= 0.0 && az <= 0.0) outside = -std::min(hx - std::fabs(lx), hz - std::fabs(lz));
    else if (ax > 0.0 && az > 0.0) outside = std::sqrt(ax * ax + az * az);
    else outside = std::max(ax, az);
    if (outside > 1.25) return false;

    const double qx = clampd(lx, -hx, hx);
    const double qz = clampd(lz, -hz, hz);
    double cx, cy, cz;
    shipLocalToScene(qx, deck.deckY, qz, world.pose, cx, cy, cz);
    if (cy - world.waterY > 8.0) return false; // stocks-high: you cannot reach the rail

    const double tdx = cx - foot.x, tdz = cz - foot.z;
    const double td = std::sqrt(tdx * tdx + tdz * tdz);
    if (outside > 0.0 && td > 0.05) {
        const double toward = (std::sin(foot.face) * moveSign * tdx
                             + std::cos(foot.face) * moveSign * tdz) / td;
        if (toward < 0.35) return false;
    }

    double fx = clampd(lx, -deck.halfBeam, deck.halfBeam);
    double fz = clampd(lz, -deck.halfLength, deck.halfLength);
    if (std::fabs(lx) > deck.halfBeam || std::fabs(lz) > deck.halfLength) {
        const double ix = -fx, iz = -fz;
        const double il = std::sqrt(ix * ix + iz * iz);
        if (il > 1e-4) {
            fx = clampd(fx + ix / il * 0.45, -deck.halfBeam, deck.halfBeam);
            fz = clampd(fz + iz / il * 0.45, -deck.halfLength, deck.halfLength);
        }
    }
    foot.surface = FootSurface::Deck;
    foot.x = fx;
    foot.z = fz;
    foot.face += world.pose.heading; // scene move-yaw -> ship-local
    foot.plankT = 1.0;
    return true;
}

void stepShipFoot(ShipFoot& foot, const FootInput& in, const FootWorld& world) {
    const double dt = in.dt > 0.0 ? in.dt : 0.0;
    const DeckFrame& deck = world.deck;
    if (foot.mode == ControlMode::Helm) {
        placeAtHelm(foot, deck);
        if (in.leaveHelm) foot.mode = ControlMode::Foot;
        return;
    }
    if (in.takeHelm && helmInReach(foot, deck)) {
        foot.mode = ControlMode::Helm;
        placeAtHelm(foot, deck);
        return;
    }

    const double turnRate = 2.2;
    const double walkSpeed = 3.4;
    const double climbSpeed = 2.15;
    foot.face += in.turn * turnRate * dt;

    auto mountPlank = [&](double t) {
        foot.surface = FootSurface::Plank;
        foot.plankT = t;
        if (t >= 0.5)
            foot.face = sceneMoveYawFromLocal(foot.face, world.pose.heading);
    };

    if (foot.surface == FootSurface::Deck) {
        const double ox = foot.x + std::sin(foot.face) * in.move * walkSpeed * dt;
        const double oz = foot.z + std::cos(foot.face) * in.move * walkSpeed * dt;
        const bool outside = std::fabs(ox) > deck.halfBeam || std::fabs(oz) > deck.halfLength;
        const double dx = ox - world.plank.deckLocalX, dz = oz - world.plank.deckLocalZ;
        if (world.plank.deployed && outside && dx * dx + dz * dz < 1.3 * 1.3) {
            mountPlank(1.0);
        } else if (world.swim && outside && std::fabs(in.move) > 0.2) {
            dropOffDeck(foot, world, ox, oz);
        } else {
            foot.x = clampd(ox, -deck.halfBeam, deck.halfBeam);
            foot.z = clampd(oz, -deck.halfLength, deck.halfLength);
        }
        return;
    }

    double deckX, deckY, deckZ;
    plankEnds(world.plank, world, deckX, deckY, deckZ);
    const double rampX = deckX - world.plank.dockX;
    const double rampZ = deckZ - world.plank.dockZ;
    const double rampLen = std::max(0.2, std::sqrt(rampX * rampX + rampZ * rampZ));
    const double dirX = rampX / rampLen, dirZ = rampZ / rampLen; // toward the deck

    if (foot.surface == FootSurface::Plank) {
        if (!world.plank.deployed) {
            // She sailed out from under the ramp: stay with the hull.
            foot.surface = FootSurface::Deck;
            foot.x = world.plank.deckLocalX;
            foot.z = world.plank.deckLocalZ;
            foot.face = foot.face + world.pose.heading;
            return;
        }
        const double along = std::sin(foot.face) * dirX + std::cos(foot.face) * dirZ;
        foot.plankT += in.move * along * climbSpeed * dt / rampLen;
        if (foot.plankT >= 1.0) {
            foot.surface = FootSurface::Deck;
            foot.plankT = 1.0;
            const double ix = -world.plank.deckLocalX, iz = -world.plank.deckLocalZ;
            const double il = std::sqrt(ix * ix + iz * iz);
            foot.x = world.plank.deckLocalX;
            foot.z = world.plank.deckLocalZ;
            if (il > 1e-4) {
                foot.x += ix / il * 0.45;
                foot.z += iz / il * 0.45;
            }
            foot.x = clampd(foot.x, -deck.halfBeam, deck.halfBeam);
            foot.z = clampd(foot.z, -deck.halfLength, deck.halfLength);
            foot.face = foot.face + world.pose.heading; // scene move-yaw -> ship-local
            return;
        }
        if (foot.plankT <= 0.0) {
            foot.surface = FootSurface::Ground;
            foot.plankT = 0.0;
            foot.x = world.plank.dockX;
            foot.z = world.plank.dockZ;
            return;
        }
        return;
    }

    // Water. Scene space, slower than a walk. A stroke onto sand or a berth
    // climbs out; a stroke into the hull hauls you onto the deck.
    if (foot.surface == FootSurface::Water) {
        const double moveSign = in.move >= 0.0 ? 1.0 : -1.0;
        const double sx = foot.x + std::sin(foot.face) * in.move * kSwimSpeed * dt;
        const double sz = foot.z + std::cos(foot.face) * in.move * kSwimSpeed * dt;
        const Stand dest = standAt(world, sx, sz);
        if (std::fabs(in.move) > 0.05 && dest == Stand::Walk) {
            foot.surface = FootSurface::Ground;
            foot.x = sx;
            foot.z = sz;
            return;
        }
        if (dest == Stand::Land) {
            // Don't swim through the island. A short probe still finds the sand
            // if a long frame would have jumped the beach into the town.
            const double probe = std::min(0.45, kSwimSpeed * dt);
            const double px = foot.x + std::sin(foot.face) * in.move * probe;
            const double pz = foot.z + std::cos(foot.face) * in.move * probe;
            if (std::fabs(in.move) > 0.05 && standAt(world, px, pz) == Stand::Walk) {
                foot.surface = FootSurface::Ground;
                foot.x = px;
                foot.z = pz;
                return;
            }
            if (standAt(world, sx, foot.z) != Stand::Land) foot.x = sx;
            else if (standAt(world, foot.x, sz) != Stand::Land) foot.z = sz;
            return;
        }
        if (std::fabs(in.move) > 0.2 && climbHull(foot, world, moveSign)) return;
        foot.x = sx;
        foot.z = sz;
        return;
    }

    // Ground.
    const double nx = foot.x + std::sin(foot.face) * in.move * walkSpeed * dt;
    const double nz = foot.z + std::cos(foot.face) * in.move * walkSpeed * dt;
    auto walkable = [&](double gx, double gz) {
        double y = 0.0;
        return groundFeet(world, gx, gz, y);
    };
    bool intoSea = false;
    if (walkable(nx, nz)) { foot.x = nx; foot.z = nz; }
    else if (walkable(nx, foot.z) && std::fabs(nx - foot.x) > 1e-9) foot.x = nx;
    else if (walkable(foot.x, nz) && std::fabs(nz - foot.z) > 1e-9) foot.z = nz;
    else if (!walkable(foot.x, foot.z)) {
        // Already off the walk. At sea that means you're swimming; on the
        // stocks or inland, snap back so a bad step can't soft-lock you.
        if (world.swim && standAt(world, foot.x, foot.z) == Stand::Sea) {
            enterWater(foot, foot.x, foot.z, foot.face);
            return;
        }
        if (world.yard) {
            foot.x = clampd(foot.x, world.yardMinX, world.yardMaxX);
            foot.z = clampd(foot.z, world.yardMinZ, world.yardMaxZ);
        } else if (world.berthCount > 0) {
            double qx, qz, qy;
            nearestBerth(world.berths, world.berthCount,
                         foot.x - world.berthToSceneX, foot.z - world.berthToSceneZ, qx, qz, qy);
            foot.x = qx + world.berthToSceneX;
            foot.z = qz + world.berthToSceneZ;
        }
    } else if (world.swim && std::fabs(in.move) > 0.2 && standAt(world, nx, nz) == Stand::Sea) {
        intoSea = true;
    }

    if (world.plank.deployed && in.move > 0.2) {
        const double dx = foot.x - world.plank.dockX, dz = foot.z - world.plank.dockZ;
        const double toDeck = std::sin(foot.face) * (deckX - foot.x) + std::cos(foot.face) * (deckZ - foot.z);
        if (dx * dx + dz * dz < 1.25 * 1.25 && toDeck > 0.0) {
            foot.surface = FootSurface::Plank;
            foot.plankT = 0.0;
            return;
        }
    }
    if (intoSea) enterWater(foot, nx, nz, foot.face);
}

void stepCameraHandoff(ShipFoot& foot, double dt) {
    const double target = foot.mode == ControlMode::Foot ? 1.0 : 0.0;
    const double k = 3.2;
    const double t = dt > 0.0 ? dt : 0.0;
    foot.camBlend += (target - foot.camBlend) * std::min(1.0, t * k);
    if (std::fabs(foot.camBlend - target) < 1.0e-3) foot.camBlend = target;
}

FootSample sampleShipFoot(const ShipFoot& foot, const FootWorld& world) {
    FootSample s;
    if (foot.surface == FootSurface::Deck || foot.mode == ControlMode::Helm) {
        const double lx = foot.mode == ControlMode::Helm ? world.deck.helmX : foot.x;
        const double lz = foot.mode == ControlMode::Helm ? world.deck.helmZ : foot.z;
        const double face = foot.mode == ControlMode::Helm ? 0.0 : foot.face;
        shipLocalToScene(lx, world.deck.deckY, lz, world.pose, s.x, s.y, s.z);
        s.moveYaw = sceneMoveYawFromLocal(face, world.pose.heading);
        s.renderYaw = renderYawFromMove(s.moveYaw);
        return s;
    }
    if (foot.surface == FootSurface::Plank && world.plank.deployed) {
        double deckX, deckY, deckZ;
        plankEnds(world.plank, world, deckX, deckY, deckZ);
        const double t = clampd(foot.plankT, 0.0, 1.0);
        s.x = world.plank.dockX + (deckX - world.plank.dockX) * t;
        s.y = world.plank.dockY + (deckY - world.plank.dockY) * t;
        s.z = world.plank.dockZ + (deckZ - world.plank.dockZ) * t;
        s.moveYaw = foot.face;
        s.renderYaw = renderYawFromMove(s.moveYaw);
        return s;
    }
    if (foot.surface == FootSurface::Water) {
        s.x = foot.x;
        s.z = foot.z;
        s.y = world.waterY - kSwimDraft;
        s.moveYaw = foot.face;
        s.renderYaw = renderYawFromMove(s.moveYaw);
        return s;
    }
    s.x = foot.x;
    s.z = foot.z;
    s.moveYaw = foot.face;
    s.renderYaw = renderYawFromMove(s.moveYaw);
    double gy = 0.0;
    if (groundFeet(world, foot.x, foot.z, gy)) s.y = gy;
    else if (world.yard) s.y = world.yardFeetY;
    else {
        gy = world.yardFeetY;
        const double ilx = foot.x - world.berthToSceneX, ilz = foot.z - world.berthToSceneZ;
        double qx, qz;
        nearestBerth(world.berths, world.berthCount, ilx, ilz, qx, qz, gy);
        s.y = gy;
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

        // A HIGH shot rakes the rigging (slowing her); a low shot floods the hull.
        Ship rigTarget = makeShipFromConfig(baseCfg);
        const double rig0 = rigIntegrity(rigTarget);
        const double hi = rigTarget.bounds.depth * 0.85; // upper part of the hit box
        std::vector<Projectile> high = { Projectile{ 0, hi, 0, 0, 0, 0, 4.0, true } };
        resolveHits(high, rigTarget, 0, 0, 0, 0.0);
        push("A high shot rakes the rigging (rig loses integrity)", rigIntegrity(rigTarget) < rig0);
        push("An undamaged rig reads full integrity", rig0 > 0.99);

        // A low shot floods the hull and spares the rig.
        Ship hullTarget = makeShipFromConfig(baseCfg);
        std::vector<Projectile> low = { Projectile{ 0, 0, 0, 0, 0, 0, 4.0, true } };
        resolveHits(low, hullTarget, 0, 0, 0, 0.0);
        push("A low shot floods the hull, not the rig",
             getShipStats(hullTarget).damageRatio > 0.0 && rigIntegrity(hullTarget) > 0.99);
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
        int deckRank = -1, mastRank = -1;
        for (int i = 0; i < static_cast<int>(ord.size()); ++i) {
            const std::string& t = base.pieces[ord[i]].type;
            if (t == "deck") deckRank = i;
            if (t == "mast") mastRank = i;
        }
        push("Keel laid first; mast stepped after the deck",
             base.pieces[ord.front()].type == "keel" && deckRank >= 0 && mastRank > deckRank);

        int masts = 0, bows = 0, helms = 0, lines = 0;
        for (const auto& p : base.pieces) {
            if (p.type == "mast") ++masts;
            if (p.type == "bowsprit") ++bows;
            if (p.type == "helm") ++helms;
            if (p.type == "line") ++lines;
        }
        push("Ship has a mast, bowsprit, helm and rigging lines",
             masts == 1 && bows == 1 && helms == 1 && lines >= 3,
             num(masts) + "/" + num(bows) + "/" + num(helms) + "/" + num(lines));

        int blocks = 0;
        for (const auto& p : base.pieces) if (p.type == "block") ++blocks;
        push("Ship is rigged with blocks (pulleys)", blocks >= 4, num(blocks) + " blocks");
        push("A block-and-tackle multiplies hauling force",
             tackleAdvantage(4) > tackleAdvantage(2) && tackleAdvantage(1) == 1.0 && tackleAdvantage(3) == 3.0);

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

    // --- Hull profile bake (build -> sail) ---
    {
        ShipConfig fineCfg;  fineCfg.length = 18; fineCfg.width = 3; fineCfg.depth = 2.8;
        ShipConfig beamyCfg; beamyCfg.length = 10; beamyCfg.width = 6; beamyCfg.depth = 2.8;
        const HullProfile fine  = bakeHullProfile(makeShipFromConfig(fineCfg));
        const HullProfile beamy = bakeHullProfile(makeShipFromConfig(beamyCfg));

        push("A finer hull has a higher top speed", fine.topSpeedFactor > beamy.topSpeedFactor,
             num(fine.topSpeedFactor * 100) + "% vs " + num(beamy.topSpeedFactor * 100) + "%");
        push("A beamy hull is more stable (higher GM)", beamy.gm > fine.gm);
        push("A beamy hull turns tighter", beamy.turnFactor > fine.turnFactor);

        ShipConfig bigCfg;   bigCfg.length = 20; bigCfg.width = 7; bigCfg.depth = 3.2;
        ShipConfig smallCfg; smallCfg.length = 9; smallCfg.width = 3; smallCfg.depth = 2.4;
        const bool disp = bakeHullProfile(makeShipFromConfig(bigCfg)).displacementVol
                        > bakeHullProfile(makeShipFromConfig(smallCfg)).displacementVol;
        push("A bigger hull displaces more water", disp);

        // The reference sloop bakes to ~unit handling factors.
        const HullProfile ref = bakeHullProfile(makeShipFromConfig(ShipConfig{}));
        push("The reference sloop bakes to ~1.0 handling", std::fabs(ref.topSpeedFactor - 1.0) < 0.05
             && std::fabs(ref.turnFactor - 1.0) < 0.05);

        push("A tender hull heels more than a stiff one", fine.heelFactor > beamy.heelFactor,
             num(fine.heelFactor * 100) + "% vs " + num(beamy.heelFactor * 100) + "%");
        const bool accel = bakeHullProfile(makeShipFromConfig(bigCfg)).accelFactor
                         < bakeHullProfile(makeShipFromConfig(smallCfg)).accelFactor;
        push("A bigger hull gathers way more slowly", accel);
    }

    // --- Difficulty zones -----------------------------------------------------
    {
        ZoneMap zm; // harbour at origin, safe 95, bands of 260, overlap 60
        push("The harbour is a level-0 safe zone", zoneLevel(zm, 0, 0) == 0);
        push("Just past the truce ring is level 1", zoneLevel(zm, zm.safeRadius + 5.0, 0) == 1);
        // Level rises monotonically with distance out to sea.
        const int lA = zoneLevel(zm, zm.safeRadius + 100.0, 0);
        const int lB = zoneLevel(zm, zm.safeRadius + 100.0 + zm.bandWidth, 0);
        const int lC = zoneLevel(zm, zm.safeRadius + 100.0 + 2 * zm.bandWidth, 0);
        push("Danger level rises band by band outward", lA < lB && lB < lC,
             num(lA) + " < " + num(lB) + " < " + num(lC));
        push("Danger level is capped at maxLevel", zoneLevel(zm, 100000.0, 0) == zm.maxLevel);
        push("Nothing spawns in the safe harbour", spawnLevel(zm, 0, 0, 0.0) == 0);

        // Deep inside a band (far from either frontier), spawns are the base level
        // regardless of the roll.
        const double midBand1 = zm.safeRadius + zm.bandWidth * 0.5;
        push("Mid-band spawns match the base level", spawnLevel(zm, midBand1, 0, 0.0) == 1
             && spawnLevel(zm, midBand1, 0, 0.99) == 1);

        // Right at a band's outer frontier, a lucky roll pulls a heavier ship in
        // from the next zone; an unlucky roll leaves the local level.
        const double atBorder = zm.safeRadius + zm.bandWidth - 1.0; // just inside band 1's frontier
        push("A heavy from the next zone spills over the border", spawnLevel(zm, atBorder, 0, 0.0) == 2,
             "got L" + num(spawnLevel(zm, atBorder, 0, 0.0)));
        push("Not every ship near a border is a heavy", spawnLevel(zm, atBorder, 0, 0.99) == 1);

        // Config scaling: tougher ships in deeper waters.
        const ShipConfig l1 = enemyConfigForLevel(1);
        const ShipConfig l5 = enemyConfigForLevel(5);
        push("Higher-level enemies carry more guns", l5.cannonCount > l1.cannonCount,
             num(l1.cannonCount) + " -> " + num(l5.cannonCount));
        push("Higher-level enemies are larger", l5.length > l1.length && l5.width > l1.width);
        push("Enemy broadsides are even (guns split per side)", (l1.cannonCount % 2) == 0 && (l5.cannonCount % 2) == 0);
    }

    // --- Seamless ship <-> foot ----------------------------------------------
    {
        auto near = [](double a, double b, double eps = 1e-6) { return std::fabs(a - b) < eps; };
        const Ship sloop = makeShipFromConfig(ShipConfig{});
        const DeckFrame deck = deckFrameFor(sloop);
        push("Helm stands on the walkable deck",
             std::fabs(deck.helmZ) < deck.halfLength && std::fabs(deck.helmX) <= deck.halfBeam);
        push("Yard deck height matches the stocks",
             near(deck.deckY + stocksHeaveY(deck.depth),
                  2.9 + deck.depth * 0.55 + deck.depth * 0.03 + 0.06));

        PoseTR pose;
        double ox, oy, oz, lx, ly, lz;
        pose.heading = 0.4;
        shipLocalToScene(0.0, 0.0, 1.0, pose, ox, oy, oz);
        push("Bow follows the mesh yaw (bx ZYX)",
             near(ox, -std::sin(0.4)) && near(oz, std::cos(0.4)) && near(oy, 0.0));

        pose.heading = 0.0;
        pose.heel = 0.3;
        pose.heaveY = 2.0;
        shipLocalToScene(1.0, 0.0, 0.0, pose, ox, oy, oz);
        push("Positive heel drops the starboard rail", oy < pose.heaveY - 0.05);

        pose.pitch = 0.12;
        pose.heel = -0.18;
        pose.heading = 1.25;
        pose.heaveY = 1.4;
        pose.originX = 3.0;
        pose.originZ = -6.0;
        shipLocalToScene(0.5, 1.1, -2.0, pose, ox, oy, oz);
        sceneToShipLocal(ox, oy, oz, pose, lx, ly, lz);
        push("Ship-local footing round-trips through pitch, heel and yaw",
             near(lx, 0.5, 1e-8) && near(ly, 1.1, 1e-8) && near(lz, -2.0, 1e-8));

        FootWorld world;
        world.deck = deck;
        world.pose.heaveY = 1.2;
        world.pose.heading = 0.4;
        world.pose.pitch = 0.05;
        world.pose.heel = -0.08;
        ShipFoot foot;
        FootInput idle;
        idle.dt = 0.016;
        stepShipFoot(foot, idle, world);
        const FootSample atWheel = sampleShipFoot(foot, world);
        idle.leaveHelm = true;
        stepShipFoot(foot, idle, world);
        const FootSample steppedOff = sampleShipFoot(foot, world);
        push("Leaving the helm keeps you at the wheel",
             foot.mode == ControlMode::Foot && foot.surface == FootSurface::Deck
             && near(atWheel.x, steppedOff.x, 1e-8) && near(atWheel.y, steppedOff.y, 1e-8)
             && near(atWheel.z, steppedOff.z, 1e-8));

        foot.x = 0.0;
        foot.z = deck.halfLength * 0.85;
        foot.face = 0.0;
        FootInput grab;
        grab.dt = 0.016;
        grab.takeHelm = true;
        stepShipFoot(foot, grab, world);
        push("The wheel is out of reach from the bow", foot.mode == ControlMode::Foot);

        foot.x = deck.helmX;
        foot.z = deck.helmZ;
        stepShipFoot(foot, grab, world);
        push("Taking the helm only works at the wheel", foot.mode == ControlMode::Helm);

        // Fixed ship-local footing: heel and yaw move the scene point, not the feet.
        foot.mode = ControlMode::Foot;
        foot.surface = FootSurface::Deck;
        foot.x = 1.0;
        foot.z = 2.0;
        foot.face = 0.3;
        world.pose.heel = 0.0;
        world.pose.pitch = 0.0;
        world.pose.heading = 0.0;
        world.pose.originX = 0.0;
        world.pose.originZ = 0.0;
        const FootSample flat = sampleShipFoot(foot, world);
        world.pose.heel = 0.22;
        world.pose.pitch = -0.1;
        world.pose.heading = 1.1;
        world.pose.originX = 10.0;
        world.pose.originZ = -4.0;
        FootInput hold;
        hold.dt = 0.05;
        stepShipFoot(foot, hold, world);
        const FootSample rolled = sampleShipFoot(foot, world);
        push("Heeling does not slide a planted footing",
             near(foot.x, 1.0) && near(foot.z, 2.0) && near(foot.face, 0.3));
        sceneToShipLocal(rolled.x, rolled.y, rolled.z, world.pose, lx, ly, lz);
        push("A planted footing rides the hull when she heels and yaws",
             !near(flat.y, rolled.y, 0.02) && near(lx, 1.0, 1e-6) && near(lz, 2.0, 1e-6)
             && near(ly, deck.deckY, 1e-6) && near(world.pose.originX, 10.0));

        world.plank.deployed = false;
        foot.x = 0.0;
        foot.z = -deck.halfLength + 0.02;
        foot.face = PI;
        FootInput aft;
        aft.move = 1.0;
        aft.dt = 0.05;
        for (int i = 0; i < 20; ++i) stepShipFoot(foot, aft, world);
        push("With the plank stowed you stay on deck",
             foot.surface == FootSurface::Deck && foot.z >= -deck.halfLength - 1e-9);

        world.yard = true;
        world.pose = {};
        world.pose.heaveY = stocksHeaveY(deck.depth);
        world.plank = yardGangplank(deck);
        foot.surface = FootSurface::Ground;
        foot.x = world.plank.dockX;
        foot.z = world.plank.dockZ;
        foot.face = 0.0; // toward the stern, up the ramp
        FootInput climb;
        climb.move = 1.0;
        climb.dt = 0.05;
        for (int i = 0; i < 50 && foot.surface != FootSurface::Deck; ++i) stepShipFoot(foot, climb, world);
        const FootSample aboard = sampleShipFoot(foot, world);
        const double deckSceneY = world.pose.heaveY + deck.deckY;
        push("Climbing the gangplank boards the deck",
             foot.surface == FootSurface::Deck && near(aboard.y, deckSceneY, 0.15));

        foot.surface = FootSurface::Plank;
        foot.plankT = 0.5;
        foot.face = 0.0;
        const FootSample mid = sampleShipFoot(foot, world);
        double deckEndX, deckEndY, deckEndZ;
        shipLocalToScene(world.plank.deckLocalX, deck.deckY, world.plank.deckLocalZ, world.pose,
                         deckEndX, deckEndY, deckEndZ);
        push("The gangplank rises between dock and deck",
             mid.y > world.plank.dockY + 0.2 && mid.y < deckEndY - 0.2
             && near(mid.y, 0.5 * (world.plank.dockY + deckEndY), 1e-6));

        foot.surface = FootSurface::Deck;
        foot.x = 0.0;
        foot.z = -deck.halfLength + 0.02;
        foot.face = PI;
        for (int i = 0; i < 40 && foot.surface != FootSurface::Ground; ++i) stepShipFoot(foot, aft, world);
        push("Walking off the stern goes ashore, not a teleport",
             foot.surface == FootSurface::Ground
             && near(foot.x, world.plank.dockX, 0.6) && near(foot.z, world.plank.dockZ, 0.6));

        BerthRect pier;
        pier.cx = 0.0;
        pier.cz = -deck.halfLength - 4.0;
        pier.hx = 3.0;
        pier.hz = 1.0;
        pier.feetY = 1.5;
        world.yard = false;
        world.berths = &pier;
        world.berthCount = 1;
        world.pose = {};
        const PlankSpec alongside = planGangplank(deck, world.pose, &pier, 1, 0.0, 0.0, 0.0);
        const PlankSpec sailing = planGangplank(deck, world.pose, &pier, 1, 0.0, 0.0, 5.0);
        push("A stopped hull near a pier drops the gangplank", alongside.deployed && !sailing.deployed);

        BerthRect quay;
        quay.cx = 0.0;
        quay.cz = 0.0;
        quay.hx = 2.0;
        quay.hz = 2.0;
        quay.feetY = 1.9;
        world.berths = &quay;
        world.berthCount = 1;
        world.plank.deployed = false;
        foot.surface = FootSurface::Ground;
        foot.x = 0.0;
        foot.z = 0.0;
        foot.face = 0.0;
        FootInput wander = climb;
        for (int i = 0; i < 80; ++i) stepShipFoot(foot, wander, world);
        double by = 0.0;
        const bool still = berthContains(&quay, 1, foot.x, foot.z, by);
        push("Ashore, you stay on the quay", still && foot.z > 1.5 && foot.z <= 2.0 + 1e-6);

        BerthRect harbour[kHarbourBerthCount];
        harbourBerths(harbour);
        world.berths = harbour;
        world.berthCount = kHarbourBerthCount;
        foot.x = -2.0;
        foot.z = -56.0; // pier 2, island-local == scene when the ship is at the island origin
        foot.face = 0.0;
        const double z0 = foot.z;
        for (int i = 0; i < 120; ++i) stepShipFoot(foot, wander, world);
        double hy = 0.0;
        push("The pier connects to the quay",
             foot.z > z0 + 8.0 && berthContains(harbour, kHarbourBerthCount, foot.x, foot.z, hy));

        foot.mode = ControlMode::Foot;
        foot.camBlend = 0.0;
        stepCameraHandoff(foot, 0.1);
        const double midBlend = foot.camBlend;
        for (int i = 0; i < 30; ++i) stepCameraHandoff(foot, 0.1);
        const double full = foot.camBlend;
        foot.mode = ControlMode::Helm;
        for (int i = 0; i < 30; ++i) stepCameraHandoff(foot, 0.1);
        push("The camera eases between chase and follow",
             midBlend > 0.2 && midBlend < 0.5 && near(full, 1.0) && near(foot.camBlend, 0.0));

        push("Sail heel leans to leeward and dies in irons",
             sailWindHeel(0.0, PI * 0.5, 1.0, 1.0) > 0.05
             && sailWindHeel(0.0, -PI * 0.5, 1.0, 1.0) < -0.05
             && std::fabs(sailWindHeel(0.0, PI, 1.0, 1.0)) < 0.02);
        push("Render yaw faces the way you walk",
             near(renderYawFromMove(0.0), 0.0) && near(renderYawFromMove(PI * 0.5), -PI * 0.5));
        ShipFoot ashore;
        ashore.mode = ControlMode::Foot;
        ashore.surface = FootSurface::Ground;
        ShipFoot onDeck;
        onDeck.mode = ControlMode::Foot;
        onDeck.surface = FootSurface::Deck;
        ShipFoot swimming;
        swimming.mode = ControlMode::Foot;
        swimming.surface = FootSurface::Water;
        push("The ship keeps way on deck and is moored ashore or swimming",
             footUnderWay(ShipFoot{}) && footUnderWay(onDeck)
             && !footUnderWay(ashore) && !footUnderWay(swimming));
        push("A swimmer is labelled swimming",
             std::string(shipFootLabel(swimming)) == "swimming");

        // --- Swimming: enter, move, climb out. No soft-lock. -----------------
        push("The island centre is land and deep water is offshore",
             island_shape::metresInland(0.0f, 0.0f) > 20.0f
             && island_shape::metresInland(0.0f, -140.0f) < -10.0f);

        auto faceToward = [](double dx, double dz) { return std::atan2(dx, dz); };
        FootInput stroke;
        stroke.move = 1.0;
        stroke.dt = 0.05;

        // Deck -> water -> deck, including heel and yaw so the body clears the skin.
        {
            const auto climbWaves = makeWaveField("swim-climb");
            const FloatPose ridden = computeFloatPose(sloop, climbWaves, 2.0);
            FootWorld sea;
            sea.deck = deck;
            sea.swim = true;
            sea.waterY = 0.0;
            sea.pose.heaveY = ridden.heaveY;
            sea.pose.heading = 0.8;
            sea.pose.heel = 0.12;
            sea.pose.pitch = -0.05;
            ShipFoot body;
            body.mode = ControlMode::Foot;
            body.surface = FootSurface::Deck;
            body.x = deck.halfBeam - 0.05;
            body.z = 0.4;
            body.face = PI * 0.5; // starboard, off the rail
            for (int i = 0; i < 8 && body.surface != FootSurface::Water; ++i) stepShipFoot(body, stroke, sea);
            const FootSample wet = sampleShipFoot(body, sea);
            const double outR = std::sqrt(wet.x * wet.x + wet.z * wet.z);
            push("Walking off the rail enters the water beside the hull",
                 body.surface == FootSurface::Water && near(wet.y, sea.waterY - kSwimDraft, 1e-6)
                 && outR > deck.beam * 0.5);
            const double leftFace = body.face;
            for (int i = 0; i < 12; ++i) stepShipFoot(body, stroke, sea);
            const double awayR = std::sqrt(body.x * body.x + body.z * body.z);
            push("Swimming away from the hull does not haul you back aboard",
                 body.surface == FootSurface::Water && awayR > outR + 0.4);
            body.face = leftFace + PI;
            bool boarded = false;
            for (int i = 0; i < 80 && body.surface != FootSurface::Deck; ++i) stepShipFoot(body, stroke, sea);
            boarded = body.surface == FootSurface::Deck
                   && std::fabs(body.x) <= deck.halfBeam + 1e-6
                   && std::fabs(body.z) <= deck.halfLength + 1e-6;
            const FootSample back = sampleShipFoot(body, sea);
            push("Swimming back into the hull climbs onto the deck",
                 boarded && back.y > wet.y + 0.8);
            FootInput grab;
            grab.dt = 0.016;
            grab.takeHelm = true;
            body.surface = FootSurface::Water;
            body.mode = ControlMode::Foot;
            body.x = 1.0;
            body.z = 0.0;
            stepShipFoot(body, grab, sea);
            push("The wheel is out of reach while you are swimming",
                 body.mode == ControlMode::Foot && body.surface == FootSurface::Water);
        }

        // Gangplank still wins over a swim drop when it is the exit you are on.
        {
            FootWorld sea;
            sea.deck = deck;
            sea.swim = true;
            sea.pose.heaveY = 1.2;
            sea.plank = yardGangplank(deck);
            ShipFoot body;
            body.mode = ControlMode::Foot;
            body.surface = FootSurface::Deck;
            body.x = 0.0;
            body.z = -deck.halfLength + 0.05;
            body.face = PI;
            for (int i = 0; i < 12 && body.surface == FootSurface::Deck; ++i) stepShipFoot(body, stroke, sea);
            push("The gangplank is the way off, not a drop into the sea",
                 body.surface == FootSurface::Plank);
        }

        // Open water: you travel, and nothing teleports you back to the ship.
        {
            FootWorld sea;
            sea.deck = deck;
            sea.swim = true;
            ShipFoot body;
            body.mode = ControlMode::Foot;
            body.surface = FootSurface::Water;
            body.x = 0.0;
            body.z = -90.0;
            body.face = PI; // further offshore
            const double z0 = body.z;
            for (int i = 0; i < 20; ++i) stepShipFoot(body, stroke, sea);
            const double swam = std::fabs(body.z - z0);
            ShipFoot walker;
            walker.mode = ControlMode::Foot;
            walker.surface = FootSurface::Deck;
            walker.x = 0.0;
            walker.z = 0.0;
            walker.face = 0.0;
            FootWorld dry = sea;
            dry.swim = false;
            for (int i = 0; i < 20; ++i) stepShipFoot(walker, stroke, dry);
            push("Swimming moves you, slower than a walk, and does not snap you back",
                 body.surface == FootSurface::Water && near(body.x, 0.0, 0.05)
                 && swam > 1.5 && swam + 0.4 < std::fabs(walker.z));
        }

        // Pier: off the end into the water, out, and back onto the same pier.
        {
            BerthRect harbour[kHarbourBerthCount];
            harbourBerths(harbour);
            FootWorld sea;
            sea.deck = deck;
            sea.swim = true;
            sea.shore = true;
            sea.berths = harbour;
            sea.berthCount = kHarbourBerthCount;
            ShipFoot body;
            body.mode = ControlMode::Foot;
            body.surface = FootSurface::Ground;
            body.x = -2.0;
            body.z = -73.0 + 0.25; // pier 2, just inside the south lip
            body.face = PI;
            for (int i = 0; i < 30 && body.surface != FootSurface::Water; ++i) stepShipFoot(body, stroke, sea);
            const double wetZ = body.z;
            push("Walking off the end of a pier enters the water",
                 body.surface == FootSurface::Water && wetZ < -73.0);
            for (int i = 0; i < 40; ++i) stepShipFoot(body, stroke, sea);
            push("You can swim clear of the pier without being put back on it",
                 body.surface == FootSurface::Water && body.z < wetZ - 1.0);
            body.face = 0.0; // back north, onto the pier
            for (int i = 0; i < 200 && body.surface != FootSurface::Ground; ++i) stepShipFoot(body, stroke, sea);
            double py = 0.0;
            push("Swimming back onto the pier climbs out",
                 body.surface == FootSurface::Ground
                 && berthContains(harbour, kHarbourBerthCount, body.x, body.z, py));

            // Landward side of the quay is the town, not the sea.
            body.surface = FootSurface::Ground;
            body.x = -6.0;
            body.z = -35.0 - 0.2;
            body.face = 0.0;
            for (int i = 0; i < 40; ++i) stepShipFoot(body, stroke, sea);
            const float inland = island_shape::metresInland(float(body.x), float(body.z));
            double qy = 0.0;
            const bool onQuay = berthContains(harbour, kHarbourBerthCount, body.x, body.z, qy);
            push("The town side of the quay does not drop you in the sea",
                 body.surface != FootSurface::Water && (onQuay || inland <= float(sea.shoreBeach) + 0.2f)
                 && inland < 30.0f);
        }

        // Beach: into the sea and back onto the sand. Inland of the sand is closed.
        {
            BerthRect harbour[kHarbourBerthCount];
            harbourBerths(harbour);
            double bx = 0.0, bz = 0.0;
            bool found = false;
            for (double ang = -0.15; ang <= 0.7 && !found; ang += 0.03) {
                for (double rad = 30.0; rad < 100.0; rad += 0.35) {
                    const double x = std::cos(ang) * rad, z = std::sin(ang) * rad;
                    const float inland = island_shape::metresInland(float(x), float(z));
                    if (inland < 2.2f || inland > 4.2f) continue;
                    double by = 0.0;
                    if (berthContains(harbour, kHarbourBerthCount, x, z, by)) continue;
                    bx = x; bz = z; found = true; break;
                }
            }
            push("A beach footing exists off the piers", found);
            if (found) {
                FootWorld sea;
                sea.deck = deck;
                sea.swim = true;
                sea.shore = true;
                sea.shoreBeach = 8.0;
                sea.berths = harbour;
                sea.berthCount = kHarbourBerthCount;
                const double rad = std::sqrt(bx * bx + bz * bz);
                ShipFoot body;
                body.mode = ControlMode::Foot;
                body.surface = FootSurface::Ground;
                body.x = bx;
                body.z = bz;
                body.face = faceToward(bx / rad, bz / rad); // seaward
                for (int i = 0; i < 80 && body.surface != FootSurface::Water; ++i) stepShipFoot(body, stroke, sea);
                const FootSample wet = sampleShipFoot(body, sea);
                push("Walking off the beach enters the water",
                     body.surface == FootSurface::Water && wet.y < -0.5);
                const double wx = body.x, wz = body.z;
                body.face = faceToward(-wx, -wz); // back toward the island
                for (int i = 0; i < 120 && body.surface != FootSurface::Ground; ++i) stepShipFoot(body, stroke, sea);
                const float backIn = island_shape::metresInland(float(body.x), float(body.z));
                const FootSample sand = sampleShipFoot(body, sea);
                push("Swimming back onto the beach climbs out on the sand",
                     body.surface == FootSurface::Ground && backIn >= 0.0f
                     && backIn <= float(sea.shoreBeach) + 0.3f && sand.y > 0.0 && sand.y < 6.0);

                double ix = 0.0, iz = 0.0;
                bool inner = false;
                for (double rang = -0.15; rang <= 0.7 && !inner; rang += 0.03) {
                    for (double rr = 30.0; rr < 100.0; rr += 0.3) {
                        const double x = std::cos(rang) * rr, z = std::sin(rang) * rr;
                        const float inland = island_shape::metresInland(float(x), float(z));
                        if (inland < 7.3f || inland > 7.95f) continue;
                        double by = 0.0;
                        if (berthContains(harbour, kHarbourBerthCount, x, z, by)) continue;
                        ix = x; iz = z; inner = true; break;
                    }
                }
                if (inner) {
                    const double ir = std::sqrt(ix * ix + iz * iz);
                    body.surface = FootSurface::Ground;
                    body.x = ix;
                    body.z = iz;
                    body.face = faceToward(-ix / ir, -iz / ir); // inland, toward the town
                    for (int i = 0; i < 80; ++i) stepShipFoot(body, stroke, sea);
                    const float stayed = island_shape::metresInland(float(body.x), float(body.z));
                    push("The beach does not open into the town",
                         body.surface == FootSurface::Ground && stayed <= float(sea.shoreBeach) + 0.05f
                         && stayed > 6.0f);
                } else {
                    push("The beach does not open into the town", false, "no inner beach point");
                }
            } else {
                push("Walking off the beach enters the water", false, "no beach");
                push("Swimming back onto the beach climbs out on the sand", false, "no beach");
                push("The beach does not open into the town", false, "no beach");
            }
        }

        // The dry dock is not the sea, even if swimming is left on.
        {
            FootWorld yard;
            yard.deck = deck;
            yard.yard = true;
            yard.swim = true;
            yard.pose.heaveY = stocksHeaveY(deck.depth);
            ShipFoot body;
            body.mode = ControlMode::Foot;
            body.surface = FootSurface::Ground;
            body.x = yard.yardMaxX - 0.15;
            body.z = 0.0;
            body.face = PI * 0.5;
            for (int i = 0; i < 40; ++i) stepShipFoot(body, stroke, yard);
            push("The shipyard apron does not drop you into the sea",
                 body.surface == FootSurface::Ground && body.x <= yard.yardMaxX + 1e-6);
        }
    }

    return r;
}

} // namespace sea
