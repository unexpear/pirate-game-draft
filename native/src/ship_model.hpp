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
// Damage the mast/rigging by `amount` (a high shot rakes the rig).
void damageRig(Ship& ship, double amount);
// Fraction of the rig (mast) still intact, 0..1 — scales the ship's sail drive.
double rigIntegrity(const Ship& ship);

// Mechanical advantage of a block-and-tackle with `parts` rope parts supporting
// the load: hauling `load / parts` of effort raises the load (frictionless). The
// yard's pulleys hoist heavy spars & timbers this way.
double tackleAdvantage(int parts);

// Decide orders for an enemy captain at (ex,ez,eHeading) engaging a foe at
// (ox,oz): close to `engageRange`, then turn to present a broadside and fire the
// bearing side when roughly abeam, in range, and `reloadReady`.
AiOrders aiCaptain(double ex, double ez, double eHeading,
                   double ox, double oz, double engageRange, bool reloadReady);

// --- On-deck boarding melee ------------------------------------------------
// A short third-person fight on the enemy's deck. Pressing B to board (the app)
// starts one of these instead of capturing outright; the prize is taken only
// when the crew is cut down. Pure state — no rendering. Step it once per tick.
//
// Lose path: phase becomes Lost when the boarder is downed. That is a failed
// boarding, not a sinking defeat — the app throws you back to your own ship.
// The prize stays struck, so you can pull alongside and board again.
enum class MeleePhase { Inactive, Fighting, Won, Lost };

// One verb packet for a tick. `attack` is an edge (a click / tap); `block` is
// held. `throttle` is -1 astern .. +1 ahead along the fighter's facing;
// `turn` is -1 port .. +1 starboard.
struct MeleeInput {
    double throttle = 0;
    double turn = 0;
    bool attack = false;
    bool block = false;
};

// Deck-local metres: +x starboard, +z toward the bow, heading 0 faces the bow
// (the same yaw convention as the ship). `swing` / `hurt` / `walkPhase` are
// cues for the view.
struct MeleeFighter {
    double x = 0, z = 0;
    double heading = 0;
    double hp = 100, maxHp = 100;
    double attackCooldown = 0;
    double hitstun = 0;
    double swing = 0;
    double hurt = 0;
    double walkPhase = 0;
    bool alive = true;
    bool blocking = false;
};

struct BoardingMelee {
    MeleePhase phase = MeleePhase::Inactive;
    MeleeFighter player;
    std::vector<MeleeFighter> crew;
    double deckHalfWidth = 1.2;   // clamp |x| to the deck plank
    double deckHalfLength = 2.0;  // clamp z around deckCenterZ
    double deckCenterZ = 0;
    int crewTotal = 0;
};

inline bool meleeFighting(const BoardingMelee& m) { return m.phase == MeleePhase::Fighting; }

inline int meleeCrewLeft(const BoardingMelee& m) {
    int n = 0;
    for (const auto& c : m.crew) if (c.alive) ++n;
    return n;
}

// Stand up a fight on `deck`. `boardSide` is +1 if you climb the starboard
// rail, -1 for port. `crewCount` is clamped to a small party (1..5).
BoardingMelee startBoardingMelee(const Ship& deck, int boardSide, int crewCount = 3);
// Advance the fight by `dt` seconds. No-op unless phase is Fighting.
void stepBoardingMelee(BoardingMelee& m, const MeleeInput& in, double dt);

// Ship-local Y of the deck surface (feet height, before heave).
double meleeDeckSurfaceY(const Ship& deck);
// 0..1 attack-swing cue (1 = just swung).
double meleeAttackSwing(const MeleeFighter& f);

// Deck-local point -> world/scene. `shipHeading` / origin are the deck ship's
// pose. Local heading 0 (facing the bow) becomes `shipHeading` in the world.
void meleeDeckToWorld(double shipHeading, double originX, double originZ,
                      double localX, double localZ, double localHeading,
                      double& worldX, double& worldZ, double& worldHeading);

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

// --- Build -> Sail bridge: bake a ship's shape into a water-physics profile ---
// Hull hydrostatics + handling factors derived from the built shape. Different
// shapes produce different profiles, so no two boats sail the same. Handling
// factors are relative to the reference sloop (L=12, B=4), which bakes to ~1.0.
// See build-mode.md.
struct HullProfile {
    double length = 0, beam = 0, draft = 0;   // hull envelope (m)
    double lengthBeamRatio = 0;
    double blockCoeff = 0;                     // Cb fullness (0..1)
    double displacementVol = 0;                // m^3 of water displaced at draft
    double displacementMass = 0;               // kg of water displaced
    double waterplaneArea = 0;                 // m^2
    double gm = 0;                             // metacentric-height stability proxy (higher = stiffer)
    double topSpeedFactor = 1;                 // relative max speed (long & fine => higher)
    double turnFactor = 1;                     // relative agility (short & beamy => higher)
    double dragFactor = 1;                     // relative resistance
    double heelFactor = 1;                     // how far it leans under sail (tender => higher)
    double accelFactor = 1;                    // how quickly it gathers way (heavy => lower)
};
HullProfile bakeHullProfile(const Ship& ship);

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

// --- Difficulty zones: a shifting-level sea map -----------------------------
// The open sea is banded into difficulty LEVELS by distance from the home
// harbour: a combat-free truce ring (level 0), then successive danger bands of
// rising level outward. Bands OVERLAP slightly at their frontier, so a tougher
// ship from the next band out can prowl a little way into the calmer territory
// (you meet the odd heavy near a border). See decisions in next-steps.md.
struct ZoneMap {
    double harborX = 0.0, harborZ = 0.0; // home port centre (safe waters)
    double safeRadius = 95.0;            // level 0: no combat inside this ring
    double bandWidth  = 260.0;          // radial width of each danger band
    double overlap    = 60.0;           // how far a higher band's ships reach inward
    int    maxLevel   = 6;               // deepest danger band
};

// Base danger level at a world point (0 = safe harbour, rising outward).
int zoneLevel(const ZoneMap& m, double x, double z);

// The level of an enemy that spawns at (x,z): usually the base level, but inside
// the overlap strip just inward of a frontier a `roll` in [0,1) can bump it one
// level higher — a heavy from the next zone out, spilling into softer waters.
// Returns 0 in the safe ring (spawn nothing there).
int spawnLevel(const ZoneMap& m, double x, double z, double roll);

// A short flavour name for a danger level (Sloop .. Flagship).
const char* zoneShipName(int level);

// An enemy ShipConfig scaled to a danger level: bigger, more guns, tougher as
// the level rises. Level <= 1 is the light raider; the reference sloop is L0.
ShipConfig enemyConfigForLevel(int level);

std::vector<TestResult> runSelfTest();

} // namespace sea
