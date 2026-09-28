// Sea Trial — native ship model (Milestone 0). Public domain (The Unlicense).
#include "ship_model.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
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
        if (!isShellPiece(p.type) && !isFramePiece(p.type)) continue;
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

bool isClinker(BuildTradition t) { return t == BuildTradition::Viking; }

bool isFramePiece(const std::string& type) {
    return type == "rib" || type == "floor" || type == "futtock";
}

bool isShellPiece(const std::string& type) {
    return type == "plank" || type == "garboard" || type == "wale";
}

bool isBackbonePiece(const std::string& type) {
    return type == "keel" || type == "stem" || type == "sternpost";
}

namespace {

bool isFormPiece(const std::string& type) {
    return isBackbonePiece(type) || isFramePiece(type) || isShellPiece(type)
        || type == "deck" || type == "knee" || type == "keelson" || type == "deckbeam";
}

// Side a strake actually sits on. Hand-placed pieces store `side`; the seeded
// template leaves it 0 and encodes port/starboard in the position.
int pieceSide(const Piece& p) {
    if (p.side > 0) return 1;
    if (p.side < 0) return -1;
    if (p.position.x > 0.05) return 1;
    if (p.position.x < -0.05) return -1;
    return 0;
}

bool shipHas(const Ship& ship, bool (*pred)(const std::string&)) {
    for (const auto& p : ship.pieces)
        if (pred(p.type)) return true;
    return false;
}

// The whole backbone — keel, stem, and sternpost — has to be on the stocks
// before frames, planks, or knees. One timber of the three is not enough.
bool backboneReady(const Ship& ship) {
    bool keel = false, stem = false, stern = false;
    for (const auto& p : ship.pieces) {
        if (p.type == "keel") keel = true;
        else if (p.type == "stem") stem = true;
        else if (p.type == "sternpost") stern = true;
    }
    return keel && stem && stern;
}

double keelLengthOf(const Ship& ship) {
    for (const auto& p : ship.pieces)
        if (p.type == "keel") return std::max(1.0, p.bounds.z);
    return std::max(1.0, ship.bounds.length);
}

// Stem was generated at bounds.y = depth * 1.4. Keep using that so later
// placements don't drift when refit overwrites ship.bounds.depth.
double hullDepthOf(const Ship& ship) {
    for (const auto& p : ship.pieces)
        if (p.type == "stem") return std::max(1.0, p.bounds.y / 1.4);
    return std::max(0.8, ship.bounds.depth);
}

PlaceSpec normalizedSpec(const Ship& ship, const PlaceSpec& in) {
    PlaceSpec s = in;
    const int n = s.stations >= 2 ? std::min(24, s.stations) : hullStationCount(ship);
    s.stations = n;
    if (s.station < 0) s.station = 0;
    if (s.station >= n) s.station = n - 1;
    s.halfBeam = clampd(s.halfBeam, 0.35, 4.2);
    s.height = clampd(s.height <= 0.0 ? 1.6 : s.height, 0.4, 3.2);
    s.side = s.side >= 0 ? 1 : -1;
    s.course = std::max(0, std::min(7, s.course));
    if (s.tool == PlaceTool::Garboard) s.course = 0;
    if (s.tool == PlaceTool::Rib || s.tool == PlaceTool::Floor) s.side = 0;
    return s;
}

const char* toolType(PlaceTool tool) {
    switch (tool) {
        case PlaceTool::Rib: return "rib";
        case PlaceTool::Plank: return "plank";
        case PlaceTool::Garboard: return "garboard";
        case PlaceTool::Wale: return "wale";
        case PlaceTool::Floor: return "floor";
        case PlaceTool::Knee: return "knee";
    }
    return "rib";
}

PlaceReport refusePlace(const char* code, const char* message) {
    PlaceReport r;
    r.ok = false;
    r.code = code;
    r.message = message;
    return r;
}

} // namespace

std::vector<int> buildOrder(const Ship& ship, BuildTradition t) {
    const bool frameFirst = isFrameFirst(t);
    auto stage = [&](const std::string& type) -> int {
        if (type == "keel") return 0;                          // laid first
        if (type == "stem" || type == "sternpost") return 1;   // rest of the backbone
        if (type == "knee" || type == "keelson" || type == "deckbeam") return 4;
        if (type == "deck") return 4;                          // deck after the hull is planked
        if (type == "mast" || type == "bowsprit") return 5;    // step the spars
        if (type == "helm" || type == "capstan") return 6;     // fit out
        if (type == "line" || type == "block") return 7;       // reeve the rigging (stays, halyards, blocks)
        const bool frame = isFramePiece(type);
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

int hullStationCount(const Ship& ship) {
    const double L = keelLengthOf(ship);
    const int n = std::max(5, static_cast<int>(std::lround(L / 1.4)));
    return std::min(16, n);
}

HullMeasure measureHull(const Ship& ship) {
    HullMeasure m;
    double minX = 1e9, maxX = -1e9, minY = 1e9, maxY = -1e9, minZ = 1e9, maxZ = -1e9;
    double postMinY = 1e9, postMaxY = -1e9;
    int n = 0;
    double sumHalf = 0.0, maxHalf = 0.0;
    int nSec = 0;
    for (const auto& piece : ship.pieces) {
        if (!isFormPiece(piece.type)) continue;
        const double hx = std::fabs(piece.bounds.x) * 0.5;
        const double hy = std::fabs(piece.bounds.y) * 0.5;
        const double hz = std::fabs(piece.bounds.z) * 0.5;
        minX = std::min(minX, piece.position.x - hx);
        maxX = std::max(maxX, piece.position.x + hx);
        minZ = std::min(minZ, piece.position.z - hz);
        maxZ = std::max(maxZ, piece.position.z + hz);
        // Stem and sternpost are raked taller than the hull body. They set the
        // length, but the draft comes from the keel, frames, and strakes.
        const bool post = piece.type == "stem" || piece.type == "sternpost";
        if (post) {
            postMinY = std::min(postMinY, piece.position.y - hy);
            postMaxY = std::max(postMaxY, piece.position.y + hy);
        } else {
            minY = std::min(minY, piece.position.y - hy);
            maxY = std::max(maxY, piece.position.y + hy);
        }
        ++n;
        if (isFramePiece(piece.type) || isShellPiece(piece.type)) {
            const double outer = std::fabs(piece.position.x) + hx;
            sumHalf += outer;
            maxHalf = std::max(maxHalf, outer);
            ++nSec;
        }
    }
    if (n == 0) return m;
    if (minY > maxY) { minY = postMinY; maxY = postMaxY; }
    m.length = std::max(1.0, maxZ - minZ);
    m.beam = std::max(0.5, maxX - minX);
    m.depth = std::max(0.3, maxY - minY);
    if (nSec >= 2 && maxHalf > 1e-6) {
        const double fullness = clampd((sumHalf / nSec) / maxHalf, 0.0, 1.0);
        m.blockCoeff = clampd(0.36 + 0.34 * fullness, 0.36, 0.72);
    }
    m.ok = true;
    return m;
}

namespace {

void refitHullBounds(Ship& ship) {
    const HullMeasure m = measureHull(ship);
    if (!m.ok) return;
    ship.bounds.length = m.length;
    ship.bounds.width = m.beam;
    ship.bounds.depth = m.depth;
    ship.bounds.height = m.depth + 5.4;
}

bool hasWale(const Ship& ship) {
    for (const auto& p : ship.pieces)
        if (p.type == "wale") return true;
    return false;
}

} // namespace

PlaceReport validatePlace(const Ship& ship, BuildTradition t, const PlaceSpec& in) {
    const PlaceSpec spec = normalizedSpec(ship, in);
    const bool frameTool = spec.tool == PlaceTool::Rib || spec.tool == PlaceTool::Floor;
    const bool shellTool = spec.tool == PlaceTool::Plank || spec.tool == PlaceTool::Garboard
                        || spec.tool == PlaceTool::Wale;
    const bool kneeTool = spec.tool == PlaceTool::Knee;
    if ((frameTool || shellTool || kneeTool) && !backboneReady(ship))
        return refusePlace("NO_BACKBONE", "Lay the keel, stem, and sternpost before framing or planking.");

    const bool hasShell = shipHas(ship, isShellPiece);
    const bool hasFrame = shipHas(ship, isFramePiece);
    if (frameTool && !isFrameFirst(t) && !hasShell)
        return refusePlace("SHELL_FIRST", "Shell-first (Roman/Viking): plank the hull before raising frames.");
    if (shellTool && isFrameFirst(t) && !hasFrame)
        return refusePlace("FRAME_FIRST", "Frame-first (English): raise floors or ribs before planking.");
    if (kneeTool && !(isFrameFirst(t) ? hasFrame : hasShell))
        return refusePlace(isFrameFirst(t) ? "FRAME_FIRST" : "SHELL_FIRST",
                           "Knees brace the finished hull - raise the frames (or the shell) first.");

    if (shellTool && isClinker(t) && spec.course > 0) {
        bool lap = false;
        for (const auto& piece : ship.pieces) {
            if (!isShellPiece(piece.type) || pieceSide(piece) != spec.side) continue;
            if (piece.course == spec.course - 1) { lap = true; break; }
        }
        if (!lap)
            return refusePlace("CLINKER_LAP",
                               "Clinker: each strake has to lap the course below it on the same side.");
    }
    if (ship.pieces.size() >= 220)
        return refusePlace("PIECE_CAP", "This yard holds 220 pieces.");

    PlaceReport r;
    r.ok = true;
    r.code = "OK";
    r.message = "Ready to place.";
    if (shellTool && isFrameFirst(t) && spec.tool != PlaceTool::Wale && !hasWale(ship)) {
        r.warning = true;
        r.code = "WALE_FIRST";
        r.message = "Frame-first carvel usually starts with the wales. It will still be placed.";
    }
    return r;
}

PlaceReport placePiece(Ship& ship, BuildTradition t, const PlaceSpec& in) {
    const PlaceReport gate = validatePlace(ship, t, in);
    if (!gate.ok) return gate;
    const PlaceSpec spec = normalizedSpec(ship, in);
    const double depth = hullDepthOf(ship);
    const double L = keelLengthOf(ship);
    const double u = spec.stations <= 1 ? 0.5 : double(spec.station) / double(spec.stations - 1);
    const double z = lerp(-L * 0.5, L * 0.5, u);

    std::string material = "oak";
    for (const auto& piece : ship.pieces)
        if (!piece.material.empty() && piece.material != "hemp") { material = piece.material; break; }
    const double density = materialDensity(material);
    const std::string type = toolType(spec.tool);

    int handN = 0;
    for (const auto& piece : ship.pieces)
        if (piece.id.compare(0, 5, "hand_") == 0) ++handN;

    Piece p;
    p.id = std::string("hand_") + type + "_" + pad3(handN + 1);
    p.type = type;
    p.material = material;
    p.course = spec.course;
    p.side = spec.side;

    if (spec.tool == PlaceTool::Rib || spec.tool == PlaceTool::Floor) {
        const bool floor = spec.tool == PlaceTool::Floor;
        const double h = floor ? std::max(0.35, spec.height * 0.42) : spec.height;
        p.bounds = { spec.halfBeam * 2.0, h, floor ? 0.12 : 0.08 };
        p.position = { 0.0, floor ? -depth * 0.42 : -depth * 0.16, z };
        p.volume = std::max(0.01, spec.halfBeam * 2.0 * h * 0.02);
    } else if (spec.tool == PlaceTool::Knee) {
        p.bounds = { 0.16, 0.55, 0.42 };
        p.position = { double(spec.side) * spec.halfBeam * 0.45, -depth * 0.02, z };
        p.rotation = { 0.0, 0.0, spec.side * 0.9 };
        p.volume = 0.02;
        p.course = 0;
    } else {
        const double bay = L / std::max(1, spec.stations - 1);
        const double thick = spec.tool == PlaceTool::Wale ? 0.20 : 0.11;
        const double ph = spec.tool == PlaceTool::Garboard ? depth * 0.22 : std::max(0.18, spec.height * 0.22);
        const double y = lerp(-depth * 0.28, depth * 0.22, spec.course / 7.0);
        p.bounds = { thick, ph, bay * 1.05 };
        p.position = { double(spec.side) * spec.halfBeam, y, z };
        p.volume = std::max(0.01, bay * ph * (spec.tool == PlaceTool::Wale ? 0.06 : 0.035));
        if (isClinker(t)) {
            p.position.x += spec.side * (0.035 + 0.012 * spec.course);
            p.rotation = { 0.0, spec.side * 0.20, spec.side * 0.32 };
            p.style = "clinker";
        } else {
            p.rotation = { 0.0, spec.side * 0.05, spec.side * 0.05 };
            p.style = "carvel";
        }
    }
    p.mass = p.volume * density;
    ship.pieces.push_back(p);
    ship.freeform = true;
    refitHullBounds(ship);

    PlaceReport r = gate;
    r.ok = true;
    if (!r.warning) {
        r.code = "OK";
        r.message = std::string("Placed a ") + type + ".";
    } else if (r.code == "WALE_FIRST") {
        r.message = std::string("Placed a ") + type + ". Frame-first carvel usually starts with the wales.";
    }
    return r;
}

bool strikeLastHand(Ship& ship) {
    for (int i = static_cast<int>(ship.pieces.size()) - 1; i >= 0; --i) {
        if (ship.pieces[i].id.compare(0, 5, "hand_") == 0) {
            ship.pieces.erase(ship.pieces.begin() + i);
            ship.freeform = true;
            refitHullBounds(ship);
            return true;
        }
    }
    return false;
}

int stripTimbers(Ship& ship, bool frames) {
    const auto dead = [frames](const Piece& p) {
        return frames ? isFramePiece(p.type) : isShellPiece(p.type);
    };
    const auto it = std::remove_if(ship.pieces.begin(), ship.pieces.end(), dead);
    const int n = static_cast<int>(ship.pieces.end() - it);
    ship.pieces.erase(it, ship.pieces.end());
    if (n > 0) {
        ship.freeform = true;
        refitHullBounds(ship);
    }
    return n;
}

void stampJoinery(Ship& ship, BuildTradition t) {
    const char* st = isClinker(t) ? "clinker" : "carvel";
    for (auto& p : ship.pieces)
        if (isShellPiece(p.type)) p.style = st;
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
    // Slider-seeded hulls keep the design bounds. A hand-shaped hull (freeform)
    // is measured from the timbers actually on the stocks.
    double L = std::max(1.0, ship.bounds.length);
    double B = std::max(0.5, ship.bounds.width);
    double depth = std::max(0.3, ship.bounds.depth);
    double cb = 0.50;
    if (ship.freeform) {
        const HullMeasure m = measureHull(ship);
        if (m.ok) {
            L = m.length;
            B = m.beam;
            depth = m.depth;
            cb = m.blockCoeff;
        }
    }
    const double T = std::max(0.3, depth * 0.55); // draft ~ submerged depth
    p.length = L; p.beam = B; p.draft = T;
    p.lengthBeamRatio = L / B;

    p.blockCoeff = cb;
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
    if (s.freeform) o += "freeform 1\n";
    o += "pieces " + std::to_string(s.pieces.size()) + "\n";
    for (const auto& p : s.pieces) {
        o += "piece " + p.id + " " + p.type + " " + p.material + " " +
             d2s(p.volume) + " " + d2s(p.mass) + " " + d2s(p.damage) + " " +
             d2s(p.position.x) + " " + d2s(p.position.y) + " " + d2s(p.position.z) + " " +
             d2s(p.rotation.x) + " " + d2s(p.rotation.y) + " " + d2s(p.rotation.z) + " " +
             d2s(p.bounds.x) + " " + d2s(p.bounds.y) + " " + d2s(p.bounds.z) + " " +
             std::to_string(p.course) + " " + std::to_string(p.side) + " " +
             (p.style.empty() ? "-" : p.style) + "\n";
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
        else if (key == "freeform") { int f = 0; ls >> f; s.freeform = f != 0; }
        else if (key == "pieces") { /* count is implied by the piece lines */ }
        else if (key == "piece") {
            Piece p;
            ls >> p.id >> p.type >> p.material >> p.volume >> p.mass >> p.damage >>
                p.position.x >> p.position.y >> p.position.z >>
                p.rotation.x >> p.rotation.y >> p.rotation.z >>
                p.bounds.x >> p.bounds.y >> p.bounds.z;
            if (ls >> p.course) {
                ls >> p.side;
                std::string st;
                if (ls >> st && st != "-") p.style = st;
            }
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

    // --- Freeform placement: tradition gates + geometry bake -------------------
    {
        auto backboneOnly = [](Ship s) {
            s.pieces.erase(std::remove_if(s.pieces.begin(), s.pieces.end(), [](const Piece& p) {
                return !isBackbonePiece(p.type);
            }), s.pieces.end());
            return s;
        };
        PlaceSpec rib;
        rib.tool = PlaceTool::Rib;
        rib.halfBeam = 1.5;
        rib.height = 1.6;
        rib.station = 2;
        rib.stations = 5;
        PlaceSpec plank = rib;
        plank.tool = PlaceTool::Plank;
        plank.side = 1;
        plank.course = 0;

        Ship bare = backboneOnly(makeShipFromConfig(ShipConfig{}));
        bare.pieces.erase(std::remove_if(bare.pieces.begin(), bare.pieces.end(), [](const Piece& p) {
            return p.type == "stem";
        }), bare.pieces.end());
        const std::size_t bareN = bare.pieces.size();
        const PlaceReport noBack = placePiece(bare, BuildTradition::English, rib);
        push("Backbone must be laid before a frame",
             !noBack.ok && noBack.code == "NO_BACKBONE" && bare.pieces.size() == bareN,
             noBack.code);

        Ship shell = backboneOnly(makeShipFromConfig(ShipConfig{}));
        const std::size_t shellN = shell.pieces.size();
        const PlaceReport vikRib = placePiece(shell, BuildTradition::Viking, rib);
        const PlaceReport romRib = placePiece(shell, BuildTradition::Roman, rib);
        push("Shell-first refuses frames before any strake",
             !vikRib.ok && vikRib.code == "SHELL_FIRST"
             && !romRib.ok && romRib.code == "SHELL_FIRST"
             && shell.pieces.size() == shellN);

        PlaceSpec garboard = plank;
        garboard.tool = PlaceTool::Garboard;
        const PlaceReport g = placePiece(shell, BuildTradition::Viking, garboard);
        const PlaceReport afterShell = placePiece(shell, BuildTradition::Viking, rib);
        push("A garboard strake unlocks shell-first framing",
             g.ok && afterShell.ok && shell.pieces.back().type == "rib");

        Ship framed = backboneOnly(makeShipFromConfig(ShipConfig{}));
        const PlaceReport earlyPlank = placePiece(framed, BuildTradition::English, plank);
        const std::size_t framedN = framed.pieces.size();
        PlaceSpec floor = rib;
        floor.tool = PlaceTool::Floor;
        const PlaceReport fl = placePiece(framed, BuildTradition::English, floor);
        const PlaceReport afterFrame = placePiece(framed, BuildTradition::English, plank);
        push("Frame-first refuses planks before frames; a floor unlocks them",
             !earlyPlank.ok && earlyPlank.code == "FRAME_FIRST" && framedN == backboneOnly(makeShipFromConfig(ShipConfig{})).pieces.size()
             && fl.ok && afterFrame.ok && afterFrame.warning && afterFrame.code == "WALE_FIRST",
             earlyPlank.code + "/" + afterFrame.code);

        Ship waled = backboneOnly(makeShipFromConfig(ShipConfig{}));
        const PlaceReport fl2 = placePiece(waled, BuildTradition::English, floor);
        PlaceSpec wale = plank;
        wale.tool = PlaceTool::Wale;
        wale.course = 4;
        const PlaceReport w = placePiece(waled, BuildTradition::English, wale);
        const PlaceReport afterWale = placePiece(waled, BuildTradition::English, plank);
        push("Wales-first planking does not warn",
             fl2.ok && w.ok && !w.warning && afterWale.ok && !afterWale.warning && afterWale.code == "OK");

        Ship lap = backboneOnly(makeShipFromConfig(ShipConfig{}));
        PlaceSpec high = plank;
        high.course = 1;
        high.side = -1;
        const std::size_t lapN = lap.pieces.size();
        const PlaceReport skip = placePiece(lap, BuildTradition::Viking, high);
        const PlaceReport carvelSkip = placePiece(lap, BuildTradition::Roman, high);
        push("Clinker refuses a strake with nothing to lap; carvel allows it",
             !skip.ok && skip.code == "CLINKER_LAP" && lap.pieces.size() == lapN + 1
             && carvelSkip.ok && lap.pieces.back().style == "carvel",
             skip.code);
        PlaceSpec low = high;
        low.course = 0;
        const PlaceReport first = placePiece(lap, BuildTradition::Viking, low);
        const PlaceReport second = placePiece(lap, BuildTradition::Viking, high);
        push("Clinker strakes lap the course below and record clinker joinery",
             first.ok && first.code == "OK" && second.ok
             && lap.pieces.back().style == "clinker" && lap.pieces.back().course == 1);

        // Floors sort with frames, garboards with planks, in both traditions.
        Ship order;
        order.pieces.push_back(Piece{});
        order.pieces.back().type = "keel";
        order.pieces.push_back(Piece{});
        order.pieces.back().type = "floor";
        order.pieces.push_back(Piece{});
        order.pieces.back().type = "garboard";
        auto rankOf = [](const Ship& s, BuildTradition t, const std::string& type) {
            const std::vector<int> ord = buildOrder(s, t);
            for (int i = 0; i < static_cast<int>(ord.size()); ++i)
                if (s.pieces[ord[i]].type == type) return i;
            return -1;
        };
        push("Floors and garboards follow frame-first vs shell-first order",
             rankOf(order, BuildTradition::English, "floor") < rankOf(order, BuildTradition::English, "garboard")
             && rankOf(order, BuildTradition::Viking, "garboard") < rankOf(order, BuildTradition::Viking, "floor"));

        Ship knees = backboneOnly(makeShipFromConfig(ShipConfig{}));
        PlaceSpec knee;
        knee.tool = PlaceTool::Knee;
        knee.side = 1;
        knee.halfBeam = 1.5;
        knee.station = 2;
        knee.stations = 5;
        const PlaceReport kneeEarly = placePiece(knees, BuildTradition::English, knee);
        placePiece(knees, BuildTradition::English, rib);
        const PlaceReport kneeLate = placePiece(knees, BuildTradition::English, knee);
        const bool kneePlaced = kneeLate.ok && !knees.pieces.empty() && knees.pieces.back().type == "knee";
        const bool struck = strikeLastHand(knees);
        const bool ribLeft = !knees.pieces.empty() && knees.pieces.back().type == "rib";
        push("Knees wait for the frames, and strike removes the last hand timber",
             !kneeEarly.ok && kneePlaced && struck && ribLeft);

        // Same keel, same max beam: a boxy rib plan is fuller than one with fine ends.
        auto ribsOf = [](std::initializer_list<double> beams) {
            Ship s = makeShipFromConfig(ShipConfig{});
            stripTimbers(s, true);
            stripTimbers(s, false);
            PlaceSpec spec;
            spec.tool = PlaceTool::Rib;
            spec.stations = static_cast<int>(beams.size());
            spec.height = 1.8;
            int i = 0;
            for (double b : beams) {
                spec.station = i++;
                spec.halfBeam = b;
                placePiece(s, BuildTradition::English, spec);
            }
            return s;
        };
        const Ship boxy = ribsOf({ 2.0, 2.0, 2.0, 2.0, 2.0 });
        const Ship fineRibs = ribsOf({ 0.6, 1.2, 2.0, 1.2, 0.6 });
        const HullProfile boxyP = bakeHullProfile(boxy);
        const HullProfile fineP = bakeHullProfile(fineRibs);
        push("A fuller rib plan bakes a higher block coefficient",
             boxy.freeform && boxyP.blockCoeff > fineP.blockCoeff + 0.04,
             num(boxyP.blockCoeff * 100) + " vs " + num(fineP.blockCoeff * 100));
        push("Fuller ribs displace more at the same length and max beam",
             boxyP.displacementVol > fineP.displacementVol
             && std::fabs(boxyP.beam - fineP.beam) < 0.05
             && std::fabs(boxyP.length - fineP.length) < 0.05);

        auto shaped = [](double length, double halfBeam) {
            ShipConfig cfg;
            cfg.length = length;
            cfg.width = 4.0;
            cfg.depth = 2.8;
            Ship s = makeShipFromConfig(cfg);
            stripTimbers(s, true);
            stripTimbers(s, false);
            PlaceSpec spec;
            spec.tool = PlaceTool::Rib;
            spec.stations = 5;
            spec.height = 1.8;
            spec.halfBeam = halfBeam;
            for (int i = 0; i < 5; ++i) {
                spec.station = i;
                placePiece(s, BuildTradition::English, spec);
            }
            return s;
        };
        // Slider seed is 4 m of beam either way. The placed ribs override it.
        const Ship narrow = shaped(18.0, 1.3);
        const Ship beamy = shaped(10.0, 3.0);
        const HullProfile nP = bakeHullProfile(narrow);
        const HullProfile bP = bakeHullProfile(beamy);
        push("Bake reads the placed beam, not the slider seed",
             narrow.freeform && nP.beam > 2.2 && nP.beam < 3.2 && bP.beam > 5.5 && bP.beam < 6.8,
             num(nP.beam * 10) + " / " + num(bP.beam * 10));
        push("A long narrow hand-shaped hull is faster than a short beamy one",
             nP.topSpeedFactor > bP.topSpeedFactor && nP.length > bP.length,
             num(nP.topSpeedFactor * 100) + "% vs " + num(bP.topSpeedFactor * 100) + "%");
        push("A short beamy hand-shaped hull is stiffer and turns tighter",
             bP.gm > nP.gm && bP.turnFactor > nP.turnFactor);
    }

    return r;
}

} // namespace sea
