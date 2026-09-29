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

// --- Seamless ship <-> foot -------------------------------------------------
// One body, two ways to drive it. At the helm the chase camera and the sail
// gearbox are in charge. On foot the same body walks the deck, a gangplank, the
// dock, or the beach, and can step off any of those into the water and climb
// back out. Deck coordinates are ship-local, so a fixed footing rides the hull
// through heave, pitch, heel, and yaw instead of sliding off as she rolls.
// Swimming is scene-space, like the dock: the hull is moored so the sea doesn't
// scroll out from under you.
//
// The ship-local -> scene transform matches bx::mtxSRT (rotation ZYX, column
// vectors): local +Z is the bow, local +X is starboard. Positive pitch lifts
// the bow; positive heel drops the starboard rail.
enum class ControlMode { Helm, Foot };
enum class FootSurface { Deck, Plank, Ground, Water };

// Feet sit this far below the surface while swimming. The figure is ~1.7 m to
// the crown, so the head and shoulders stay out and the chest is at the waterline.
constexpr double kSwimDraft = 1.12;
// Slower than the 3.4 m/s walk. Same facing (WASD, A/D turn).
constexpr double kSwimSpeed = 2.2;

struct DeckFrame {
    double length = 0, beam = 0, depth = 0;
    double deckY = 0;       // feet height in ship-local (stands on the deck)
    double halfLength = 0;  // walkable extent, ship-local
    double halfBeam = 0;
    double helmX = 0, helmZ = 0; // where "take the wheel" lands, on the deck
};

// Heave of a hull sitting on the yard stocks. deckY + stocksHeaveY equals the
// build-scene deck the character already stands on (ship_view::deckStandHeight).
double stocksHeaveY(double depth);
DeckFrame deckFrameFor(const Ship& ship);

// Scene pose of the hull the body is standing on. `heel` is the total roll the
// mesh uses (wave heel + sail heel), not wave heel alone.
struct PoseTR {
    double heaveY = 0, pitch = 0, heel = 0, heading = 0;
    double originX = 0, originZ = 0;
};

void shipLocalToScene(double lx, double ly, double lz, const PoseTR& pose,
                      double& ox, double& oy, double& oz);
void sceneToShipLocal(double sx, double sy, double sz, const PoseTR& pose,
                      double& lx, double& ly, double& lz);

// Leeward roll (radians) from sail pressure. Same formula the mesh adds on top
// of the wave heel, so feet and planks share one deck.
double sailWindHeel(double heading, double windDir, double sailFullness, double heelScale);

// Upright yaw for renderCharacter. Movement yaw uses (sin, cos) as its forward;
// the mesh's yaw sends local +Z to (-sin, cos), so the body faces the other way
// unless this conversion is applied. Follow cameras keep the movement yaw.
double renderYawFromMove(double moveYaw);

struct ShipFoot {
    ControlMode mode = ControlMode::Helm;
    FootSurface surface = FootSurface::Deck;
    double x = 0, z = 0;   // Deck: ship-local. Ground and Water: scene. Unused on the plank.
    double face = 0;       // Deck: ship-local yaw (0 = bow). Plank/Ground: scene move-yaw.
    double plankT = 1;     // 0 = dock end, 1 = deck end
    double camBlend = 0;   // 0 = chase behind the bow, 1 = third-person follow
};

struct FootInput {
    double move = 0;       // -1 astern/back, +1 forward, in the facing direction
    double turn = 0;       // -1 port, +1 starboard
    bool leaveHelm = false;
    bool takeHelm = false;
    double dt = 0;
};

// A ramp from a ship-local deck edge to a scene-space dock point.
struct PlankSpec {
    bool deployed = false;
    double deckLocalX = 0, deckLocalZ = 0;
    double dockX = 0, dockY = 0, dockZ = 0;
};

// Walkable rectangle. Harbour berths are island-local; the yard is one scene rect.
struct BerthRect {
    double cx = 0, cz = 0, hx = 0, hz = 0, feetY = 1.5;
};

struct FootWorld {
    DeckFrame deck;
    PoseTR pose;
    PlankSpec plank;
    bool yard = false;
    double yardMinX = -26, yardMaxX = 26, yardMinZ = -20, yardMaxZ = 26, yardFeetY = 1.8;
    // scene = island-local + berthToScene. Harbour berths are island-local.
    double berthToSceneX = 0, berthToSceneZ = 0;
    const BerthRect* berths = nullptr;
    int berthCount = 0;
    // When set, stepping off the deck, a berth, or the beach enters the water
    // instead of being clamped. Off in the yard: the dry dock is not the sea.
    bool swim = false;
    // Beach ring around the island, in the same frame as the berths (island-local
    // plus berthToScene). `shoreBeach` is how many metres of sand inland of the
    // waterline you may walk. Deeper than that is the town — blocked, not this slice.
    bool shore = false;
    double shoreBeach = 8.0;
    double waterY = 0.0; // scene-space surface under the body (calm sea, or a wave sample)
};

struct FootSample {
    double x = 0, y = 0, z = 0;
    double moveYaw = 0;    // follow camera
    double renderYaw = 0;  // ship_mesh::renderCharacter
};

// The yard gangplank: stern edge down onto the dry-dock (always deployed).
PlankSpec yardGangplank(const DeckFrame& deck);

// Quay + three piers, island-local, matching the harbour boxes in ship_view.
// feetY is the unlifted top; scenery sitting on the beach adds its own lift.
constexpr int kHarbourBerthCount = 4;
void harbourBerths(BerthRect out[kHarbourBerthCount]);

// Nearest point on the berth set. Distance is in the same frame as x/z.
// Returns a large value when there are no berths.
double nearestBerth(const BerthRect* berths, int count, double x, double z,
                    double& outX, double& outZ, double& outY);

// Gangplank from the deck edge closest to a berth, when nearly stopped and in reach.
PlankSpec planGangplank(const DeckFrame& deck, const PoseTR& pose,
                        const BerthRect* berths, int count,
                        double berthToSceneX, double berthToSceneZ,
                        double speed, double reach = 8.0, double maxSpeed = 1.8);

void stepShipFoot(ShipFoot& foot, const FootInput& in, const FootWorld& world);
void stepCameraHandoff(ShipFoot& foot, double dt);
FootSample sampleShipFoot(const ShipFoot& foot, const FootWorld& world);
bool helmInReach(const ShipFoot& foot, const DeckFrame& deck);

// True while the hull should keep her way: at the helm, or walking the deck.
// On the plank, ashore, or swimming she is moored (the caller zeros speed).
bool footUnderWay(const ShipFoot& foot);

const char* shipFootLabel(const ShipFoot& foot);

std::vector<TestResult> runSelfTest();

} // namespace sea
