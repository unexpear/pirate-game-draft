// Sea Trial — native ship model (Milestone 0)
//
// Pure-logic port of the Three.js spike's model spine: ship generation,
// weight-vs-buoyancy stats, validation, deterministic Gerstner sampling,
// serialize/deserialize, and the 11 self-tests. No graphics, no external
// libraries — this compiles with just a C++17 compiler.
//
// Public domain (The Unlicense). See ../../LICENSE.
#pragma once

#include <string>
#include <vector>

namespace sea {

constexpr double WATER_DENSITY = 1000.0;

// Wood densities (kg/m^3). Default is oak.
double materialDensity(const std::string& key);

struct Vec3 { double x = 0, y = 0, z = 0; };

struct Piece {
    std::string id;
    std::string type;      // keel | rib | plank | deck
    std::string material;  // pine | oak | teak
    double volume = 0;
    double mass = 0;
    double damage = 0;     // 0..1
    Vec3 position;
    Vec3 rotation;
    Vec3 bounds;
};

struct Bounds { double length = 0, width = 0, depth = 0, height = 0; };

struct Systems {
    int helm_count = 0;
    int sail_count = 0;
    int mast_count = 0;
    int cannon_count = 0;
    double cargo_mass = 0;
};

struct Ship {
    int schema_version = 1;
    std::string ship_id;
    std::string display_name;
    std::string created_by;
    std::string build_version;
    Bounds bounds;
    Systems systems;
    std::vector<Piece> pieces;
};

struct ShipConfig {
    std::string name = "Test Sloop";
    double length = 12;
    double width = 4;
    double depth = 2.8;
    std::string material = "oak";
    bool hasHelm = true;
    bool hasSail = true;
    int cannonCount = 2;
    double cargoMass = 0;
};

struct Stats {
    int pieceCount = 0;
    int healthyPieces = 0;
    double activeVolume = 0;
    double maxVolume = 0;
    double hullMass = 0;
    double cargoMass = 0;
    double cannonMass = 0;
    double rigMass = 0;
    double crewMass = 0;
    double mass = 0;
    double buoyancyScore = 0;
    double floatMargin = 0;
    double damageRatio = 0;
    double submergedRatio = 0;
    bool sinking = false;
};

struct Issue { std::string code; std::string message; };

struct ValidationResult {
    bool valid = false;
    std::vector<Issue> errors;
    std::vector<Issue> warnings;
    Stats stats;
};

struct Wave { double direction, amplitude, wavelength, speed, phase; };
struct WaterSample { double height, slopeX, slopeZ; };

// How the ship rides the water: vertical offset (heave) plus pitch (nose
// up/down, about X) and heel (roll, about Z), derived from the wave surface at
// the Tier-2 sample points and the ship's float margin.
struct FloatPose { double heaveY = 0, pitch = 0, heel = 0; };

// A cannonball in flight (world space, metres / seconds).
struct Projectile { double x = 0, y = 0, z = 0, vx = 0, vy = 0, vz = 0, life = 0; bool alive = true; };

constexpr double GRAVITY = 9.8;

// What an AI captain wants to do this tick.
struct AiOrders {
    int steer = 0;     // -1 port, 0 hold, +1 starboard
    int sailTier = 1;  // desired sail state 0..3
    int fireSide = 0;  // 0 hold, +1 fire starboard, -1 fire port
};

struct TestResult { std::string name; bool pass; std::string details; };

Ship makeShipFromConfig(const ShipConfig& cfg);
Stats getShipStats(const Ship& ship);
ValidationResult validateShip(const Ship& ship);
std::vector<Wave> makeWaveField(const std::string& seed);
WaterSample sampleWater(const std::vector<Wave>& waves, double x, double z, double t);
FloatPose computeFloatPose(const Ship& ship, const std::vector<Wave>& waves, double t,
                           double worldX = 0.0, double worldZ = 0.0, double heading = 0.0);
std::string serialize(const Ship& ship);
Ship deserialize(const std::string& text);

// --- Gunnery ---------------------------------------------------------------
// Fire a broadside from `ship` (centred at world sx,sy,sz, yawed `heading`) out
// the given `side` (+1 = starboard, -1 = port): one cannonball per cannon,
// spread along the hull and lobbed slightly up. Returns the new projectiles.
std::vector<Projectile> fireBroadside(const Ship& ship, int side,
                                      double sx, double sy, double sz,
                                      double heading, double muzzleSpeed = 34.0);
// Advance projectiles under gravity; kill spent/underwater ones.
void stepProjectiles(std::vector<Projectile>& shots, double dt);
// Is world point (px,py,pz) inside `target`'s hull box (centre tx,ty,tz, yaw)?
bool pointInHull(const Ship& target, double tx, double ty, double tz, double heading,
                 double px, double py, double pz);
// Kill each live projectile inside `target` and flood one hull piece; returns hits.
int resolveHits(std::vector<Projectile>& shots, Ship& target,
                double tx, double ty, double tz, double heading);
// Flood (damage) the least-damaged intact hull plank/rib by `amount`.
void damageHull(Ship& ship, double amount);

// Decide orders for an enemy captain at (ex,ez,eHeading) engaging a foe at
// (ox,oz): close to `engageRange`, then turn to present a broadside and fire the
// bearing side when roughly abeam, in range, and `reloadReady`.
AiOrders aiCaptain(double ex, double ez, double eHeading,
                   double ox, double oz, double engageRange, bool reloadReady);

// --- Real sailing: points of sail -----------------------------------------
// Boat-speed fraction (0..1) for `windAngleOffBowDeg`, the angle of the wind
// SOURCE off the bow (0 = dead into the wind, 180 = dead astern). Models real
// points of sail: a no-go zone (~<43 deg) where the sails luff and there is no
// drive (you must tack), fastest on a reach (~90-110 deg), and slower dead
// downwind than on a reach.
double sailPower(double windAngleOffBowDeg);
// Short point-of-sail name for the same angle ("in irons", "close-hauled",
// "close reach", "beam reach", "broad reach", "running").
const char* pointOfSail(double windAngleOffBowDeg);

// Apparent wind = what the sails actually feel: the true wind combined with the
// headwind from the boat's own motion. Given the true wind (blowing toward
// `windDir` at `trueWindSpeed`) and the boat (heading `heading`, speed
// `boatSpeed`), returns the direction the apparent wind blows TOWARD and its
// speed. Apparent wind shifts forward (toward the bow) and strengthens as you
// sail — which is why fast boats "sail to the apparent wind".
struct ApparentWind { double dir = 0.0, speed = 0.0; };
ApparentWind apparentWind(double windDir, double trueWindSpeed,
                          double heading, double boatSpeed);

// Keep a point (px,pz) outside a circle centred (cx,cz) of `radius` — the hull
// running aground on an island. If the point is inside, push it out to the
// boundary along the radial and return true (a collision); otherwise leave it
// untouched and return false.
bool keepOutsideCircle(double& px, double& pz, double cx, double cz, double radius);

// --- Build mode: how a hull goes together, per historical tradition --------
// See references/shipbuilding-history.md. Roman & Viking build SHELL-FIRST
// (planking shell before internal frames); English Age-of-Sail builds
// FRAME-FIRST (raise the skeleton, then plank it).
enum class BuildTradition { Roman, Viking, English };
const char* traditionName(BuildTradition t);
bool isFrameFirst(BuildTradition t);
// The ship's piece indices in the order they'd be placed on the stocks for `t`:
// keel first; shell-first => planks before ribs; frame-first => ribs before
// planks; deck last. Reveal them in order to watch the hull assemble.
std::vector<int> buildOrder(const Ship& ship, BuildTradition t);
// Ordered, human-readable build steps for `t` (with per-tradition joinery flavor).
std::vector<std::string> buildSequence(BuildTradition t);

std::vector<TestResult> runSelfTest();

} // namespace sea
