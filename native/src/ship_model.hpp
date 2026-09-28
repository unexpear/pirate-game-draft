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

// --- On deck: ship-local footing -------------------------------------------
// The sailor's position is stored in the hull frame (+X starboard, +Y up, +Z
// bow) and only then transformed into the scene. Heave, pitch, and heel move
// the deck under a planted foot; they do not change the stored coordinates, so
// the figure does not slide as the hull works.
//
// The scene transform matches bx::mtxSRT (scale 1, rotation order ZYX: pitch
// about X, heading about Y, heel about Z, then translate), which is the ship
// root used to draw the hull. `windHeel` is the extra leeward roll from sail
// pressure (see leewardHeel) and is added to pose.heel.

// Leeward roll (radians) from sail pressure. Positive lowers the starboard
// gunwale. `sailFullness` is 0..1 (furled..full); `heelScale` is the hull's
// heelFactor (tender hulls lean more).
double leewardHeel(double heading, double windDir, double sailFullness, double heelScale);

// Axis-aligned rectangle in the horizontal plane, plus a feet height.
struct DeckRect {
    double minX = 0, maxX = 0, minZ = 0, maxZ = 0;
    double feetY = 0;
};

// Walkable top of the deck plank, already inset by the figure's body radius,
// with feetY on the plank surface. Falls back to an inset hull footprint if
// the ship has no deck piece.
DeckRect deckFootprint(const Ship& ship);

// Push (x,z) onto that footprint and out of deck fittings (mast, capstan,
// helm). Returns true if the point moved.
bool clampDeckFooting(const Ship& ship, double& x, double& z);

// One tick of deck walking. `forward` is metres along `heading` this tick
// (+ faces the bow when heading is 0); `turn` is radians added to heading.
// Blocked steps slide along the deck edge or a fitting instead of leaving it.
void stepDeckWalk(const Ship& ship, double& x, double& z, double& heading,
                  double forward, double turn);

// Ship-local point -> scene. `shipSceneX/Z` is the hull origin in the scene
// (the player's ship is held at the origin while the ocean scrolls).
Vec3 shipLocalToScene(double localX, double localY, double localZ,
                      const FloatPose& pose, double shipHeading, double windHeel,
                      double shipSceneX, double shipSceneZ);
// Inverse of shipLocalToScene (pure rotation + translation, scale 1).
Vec3 sceneToShipLocal(double sceneX, double sceneY, double sceneZ,
                      const FloatPose& pose, double shipHeading, double windHeel,
                      double shipSceneX, double shipSceneZ);

// Unit scene-horizontal forward for a follow camera behind a sailor facing
// `localHeading` on this deck. Matches the mesh yaw, so the camera sits behind
// the drawn figure.
void deckFlatForward(double localHeading, const FloatPose& pose, double shipHeading,
                     double windHeel, double& outX, double& outZ);

// --- Yard gangplank: climb aboard instead of teleporting -------------------
// A straight ramp along Z, from the dock end (z0, y0) up to the deck edge
// (z1, y1). x is the centreline; halfWidth is the walkable half-breadth.
struct Ramp {
    double x = 0, halfWidth = 0.9;
    double z0 = 0, z1 = 0;
    double y0 = 0, y1 = 0;
};
struct RampHit { bool on = false; double y = 0; double t = 0; };

// Stern gangplank in the build scene. z1 meets yardDeckRect's aft edge so a
// walker steps onto the deck without a position snap. `dockFeetY` / `deckFeetY`
// are feet heights in that scene.
Ramp yardGangplank(double shipLength, double dockFeetY, double deckFeetY);
RampHit sampleRamp(const Ramp& ramp, double x, double z);

// Walkable top of a hull on the stocks: hull footprint inset 0.5 m, feet at
// `feetY` (the stand height is a scene concern; pass it in).
DeckRect yardDeckRect(const Ship& ship, double feetY);

struct Aabb2 { double minX = 0, maxX = 0, minZ = 0, maxZ = 0; };
struct YardMove { double x = 0, z = 0, y = 0; bool onDeck = false; };

// Integrate one yard step. On the dock, walking up the gangplank raises the
// feet and boards at the top. On deck, walking aft off the stern mouth steps
// back onto the plank. Elsewhere the point is clamped to the dock or the deck.
YardMove stepYardFooting(const Ramp& ramp, const Aabb2& dock, const DeckRect& deck,
                         double dockFeetY, double x, double z, double heading,
                         double forward, bool onDeck);

std::vector<TestResult> runSelfTest();

} // namespace sea
