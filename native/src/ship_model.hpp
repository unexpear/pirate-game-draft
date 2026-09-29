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
// dock, the beach, or the harbour streets, and can step off the deck, a pier,
// the beach, or a slipway into the water and climb back out. Alongside a struck
// prize, a second gangplank spans the two rails: walk it (or climb it from the
// helm) onto her deck, and the prize is taken only by cutting down her crew.
// Deck coordinates are ship-local — your hull, or hers while you are on it — so
// a fixed footing rides that hull through heave, pitch, heel, and yaw. The mast,
// capstan, and wheel are solid: a step that would pass through them stops or
// slides along the timber. The helm stand is left clear. Swimming and the port
// are scene-space, like the dock: your hull is moored so the sea doesn't scroll
// out from under you. She also holds still while you are on a gangplank, hauling
// over the rail, or on a prize's deck. Hauling out of the water is a climb —
// feet rise from the surface onto the deck — and a gangplank is walked along,
// joined where you already stand instead of a grab from a metre away.
//
// The ship-local -> scene transform matches bx::mtxSRT (rotation ZYX, column
// vectors): local +Z is the bow, local +X is starboard. Positive pitch lifts
// the bow; positive heel drops the starboard rail.
enum class ControlMode { Helm, Foot };
// EnemyDeck is the prize's weather deck, in that hull's ship-local frame.
// The boarding plank is still Plank; ShipFoot::prizeRamp says which ramp.
// Rail is the haul from the water up over your own rail onto the deck.
enum class FootSurface { Deck, Plank, Ground, Water, EnemyDeck, Rail };

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

// Body-centre boxes for the mast, the capstan, and the wheel, ship-local.
// Half-extents include a coat's clearance so you don't stand inside the timber.
// The wheel stops short of the helm stand. Lines, blocks, and shrouds are not
// in this set — they stay thin.
constexpr int kDeckSolidCount = 3;
struct DeckSolid {
    double cx = 0, cz = 0, hx = 0, hz = 0;
};
void deckSolids(const DeckFrame& deck, DeckSolid out[kDeckSolidCount]);
// True when a body centre at ship-local (x, z) is inside one of those boxes.
bool deckBlocked(const DeckFrame& deck, double x, double z);

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
// Ship-local facing (0 = bow) to scene movement yaw. The mesh yaw sends local
// (sin f, cos f) to scene (sin(f - heading), cos(f - heading)).
double sceneMoveYawFromLocal(double localFace, double heading);

struct ShipFoot {
    ControlMode mode = ControlMode::Helm;
    FootSurface surface = FootSurface::Deck;
    // Deck and EnemyDeck: ship-local. Ground, Water, and the foot of a rail
    // climb: scene. Unused on the plank.
    double x = 0, z = 0;
    // Deck / EnemyDeck: ship-local yaw (0 = bow). Plank, Ground, Water, Rail: scene move-yaw.
    double face = 0;
    double plankT = 1;     // 0 = far end (dock or prize rail), 1 = own deck end
    double railT = 0;      // Rail: 0 still in the water, 1 over the rail on deck
    double railX = 0, railZ = 0; // Rail: ship-local landing
    double camBlend = 0;   // 0 = chase behind the bow, 1 = third-person follow
    bool prizeRamp = false; // Plank is the boarding plank onto a prize, not the shore
};

struct FootInput {
    double move = 0;       // -1 astern/back, +1 forward, in the facing direction
    double turn = 0;       // -1 port, +1 starboard
    bool leaveHelm = false;
    bool takeHelm = false;
    bool strike = false;   // edge: cut at whoever is in front of the blade (enemy deck)
    bool block = false;    // held: catch a blow; you can still shuffle
    double dt = 0;
};

// A ramp from a ship-local deck edge to a scene-space dock point.
struct PlankSpec {
    bool deployed = false;
    double deckLocalX = 0, deckLocalZ = 0;
    double dockX = 0, dockY = 0, dockZ = 0;
    // A boarding plank's far end is another hull's rail, not the shore.
    // prizeLocal* is that rail in the prize's ship-local frame; dock* is the
    // same point in scene space (re-seat it when the prize pose moves).
    bool prize = false;
    double prizeLocalX = 0, prizeLocalZ = 0;
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
    // When set, stepping off the deck, a berth, the beach, or a slipway enters
    // the water instead of being clamped. Off in the yard: the dry dock is not the sea.
    bool swim = false;
    // Beach ring around the island, in the same frame as the berths (island-local
    // plus berthToScene). `shoreBeach` is how many metres of sand inland of the
    // waterline you may walk. Deeper than that is closed, unless `town` is set.
    bool shore = false;
    double shoreBeach = 8.0;
    // Harbour streets past the quay: the port row, the plaza up to its north
    // edge, and the shipyard apron, minus building footprints. Off in the yard,
    // and off unless the caller is at the island port. The hill above the plaza
    // is not part of the rect — that bound is where this walk ends.
    bool town = false;
    double waterY = 0.0; // scene-space surface under the body (calm sea, or a wave sample)
    // Prize hull you can board. `board` is the ramp between the two rails.
    // Deck coordinates while surface == EnemyDeck are prize-local. The prize
    // pose's origin is the prize's scene position (player ship at the origin).
    bool prizeLive = false;
    DeckFrame prizeDeck;
    PoseTR prizePose;
    PlankSpec board;
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

// Port walk, island-local, matching the town in ship_view. Streets with
// feetY < 0 stand on the terrain (island_shape::heightAt). feetY >= 0 is a
// flat deck top before the centre of that rect is lifted by the ground, the
// same way a pier box is. Solids are building footprints, already padded so
// the body stops outside the wall.
constexpr int kTownStreetCount = 3;
constexpr int kTownSolidCount = 16;
void townStreets(BerthRect out[kTownStreetCount]);
void townSolids(BerthRect out[kTownSolidCount]);
// Rect membership only. A step also requires dry ground (or a slip deck above
// the sand) and rejects any solid.
bool townStreetContains(double x, double z);
bool townSolidContains(double x, double z);

// Nearest point on the berth set. Distance is in the same frame as x/z.
// Returns a large value when there are no berths.
double nearestBerth(const BerthRect* berths, int count, double x, double z,
                    double& outX, double& outZ, double& outY);

// Gangplank from the deck edge closest to a berth, when nearly stopped and in reach.
PlankSpec planGangplank(const DeckFrame& deck, const PoseTR& pose,
                        const BerthRect* berths, int count,
                        double berthToSceneX, double berthToSceneZ,
                        double speed, double reach = 8.0, double maxSpeed = 1.8);

// Boarding plank between two hulls when nearly stopped and in reach. The own
// end is ship-local on `own`; the far end is a rail on `prize` (prize-local and
// scene). Stowed if `speed` is above `maxSpeed` or the rails are farther than
// `reach`. This does not take the prize — it only drops the ramp.
PlankSpec planBoardingPlank(const DeckFrame& own, const PoseTR& ownPose,
                            const DeckFrame& prize, const PoseTR& prizePose,
                            double speed, double reach = 12.0, double maxSpeed = 4.5);
// Keep a held boarding plank's far end glued to the prize rail as she heaves.
void seatBoardingPlank(PlankSpec& plank, const DeckFrame& prize, const PoseTR& prizePose);

// --- On-foot fight on the prize's deck --------------------------------------
// Capture is this fight, not a timer at the helm. Three sailors. You cut the
// one in front of the blade; they close and strike back unless you are blocking.
// Won takes the prize. Lost is a repulse: the app puts you back on your own
// deck, and the prize stays struck so you can try again. Neither outcome sinks
// you. Crew hold still while you are not on her deck (a retreat is not a loss).
enum class DeckFightPhase { Inactive, Fighting, Won, Lost };

struct DeckCrew {
    double x = 0, z = 0, face = 0;
    double hp = 2, cooldown = 0, hurt = 0, walk = 0;
    bool alive = true;
};

struct DeckFight {
    DeckFightPhase phase = DeckFightPhase::Inactive;
    double playerHp = 5, playerMax = 5;
    double playerCooldown = 0, playerHurt = 0;
    double halfBeam = 1, halfLength = 2; // prize deck the crew are clamped to
    std::vector<DeckCrew> crew;
};

inline bool prizeTaken(const DeckFight& f) { return f.phase == DeckFightPhase::Won; }

inline int deckCrewLeft(const DeckFight& f) {
    int n = 0;
    for (const auto& c : f.crew) if (c.alive) ++n;
    return n;
}

// Stand up a fresh fight on `deck`. `boardSide` is +1 if you came over the
// starboard rail, -1 for port. The crew start on the far side of the deck.
DeckFight beginDeckFight(const DeckFrame& deck, int boardSide);
// Advance crew and resolve blows. `px/pz/face` are the boarder's prize-local
// footing (the foot step owns the walking). No-op unless phase is Fighting.
// `aboard` is false once you have stepped back onto your own hull: crew hold,
// and they cannot hit you from the other ship.
void stepDeckFight(DeckFight& fight, double px, double pz, double face,
                   bool aboard, bool strike, bool block, double dt);

void stepShipFoot(ShipFoot& foot, const FootInput& in, const FootWorld& world);
void stepCameraHandoff(ShipFoot& foot, double dt);
FootSample sampleShipFoot(const ShipFoot& foot, const FootWorld& world);
bool helmInReach(const ShipFoot& foot, const DeckFrame& deck);

// Climb the boarding plank's far end onto the prize. Does not take her — the
// fight does. No-op if the ramp is stowed.
void boardPrize(ShipFoot& foot, const DeckFrame& prize, const PlankSpec& board);
// Failed fight, or the prize foundered under you: back on your own deck, on
// foot, inboard of the boarding plank (or at the wheel if it is stowed).
void repulseToOwnDeck(ShipFoot& foot, const DeckFrame& own, const PlankSpec& board);

// True while the hull should keep her way: at the helm, or walking your own deck.
// On the plank, ashore, swimming, hauling over the rail, or on a prize's deck
// she is moored (the caller zeros speed) so the sea does not scroll out from
// under you.
bool footUnderWay(const ShipFoot& foot);

const char* shipFootLabel(const ShipFoot& foot);

std::vector<TestResult> runSelfTest();

} // namespace sea
