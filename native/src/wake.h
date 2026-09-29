// Sea Trial — waterline foam / wake packing and bow spray. Public domain (Unlicense).
//
// Pure logic (no GPU). The water shader in shaders/fs_water.sc evaluates the
// same hull foam for up to kMaxWakeHulls ships. u_ship / u_shipDyn are arrays;
// slot layout is packWakes(). Bow spray is a handful of deterministic sheets
// thrown off the stem — sampleBowSpray() — drawn by the view, not a particle
// engine.
#pragma once

#include "ship_model.hpp"

#include <vector>

namespace sea {

// fs_water.sc loops this many u_ship / u_shipDyn slots. water_gpu static_asserts it.
constexpr int kMaxWakeHulls = 8;

// Speed (m/s) at which bow wave, wake, and spray reach full strength.
// Matches the scale the player foam already used (speed / 7).
constexpr float kWakeFullSpeed = 7.0f;

// Bow spray starts here (fraction of kWakeFullSpeed). Below this, no sheets.
constexpr float kBowSprayMinSpeed = 0.22f;

// Drops emitted per moving hull, and the frame-wide cap the view will draw.
constexpr int kBowSprayPerHull = 10;
constexpr int kMaxBowSprayDrops = 24;

// Harbour hulls tied at the piers, island-local metres (the island scene origin
// is added by harbourMoorings). kind 0 = the small boat mesh (bow toward +Z,
// heading 0); kind 1 = the galleon mesh (bow toward -Z, heading pi). The mesh
// lambdas in ship_view bake that bow direction; `heading` is what the foam uses.
// Beam is the full hull width the mesh draws. The pier-deck fishing boat is not
// in this list — it sits on the pier, not in the water.
struct MoorSpec {
    float localX;
    float localZ;
    float length;
    float beam;
    float heading;
    int kind;
};

inline constexpr MoorSpec kHarbourMoorings[] = {
    { -32.0f, -50.0f,  7.0f, 2.6f, 0.0f,        0 },
    {  -8.0f, -52.0f,  8.0f, 2.6f, 0.0f,        0 },
    {  31.0f, -56.0f, 22.0f, 6.0f, 3.14159265f, 1 },
};
inline constexpr int kHarbourMooringCount =
    int(sizeof(kHarbourMoorings) / sizeof(kHarbourMoorings[0]));

// One hull the sea should foam around. Scene XZ is the scrolling-ocean frame
// (the player sits at 0,0). halfLen / halfBeam are metres; halfBeam is the full
// beam * 0.6, the same ellipse the player waterline already used.
// speed01 is 0..1 (see wakeSpeed01). baseY is the spray origin (heave); the
// foam shader ignores it and draws on the sea surface.
struct WakeHull {
    float sinH = 0.0f;
    float cosH = 1.0f;
    float halfLen = 0.0f;
    float halfBeam = 0.0f;
    float speed01 = 0.0f;
    float sceneX = 0.0f;
    float sceneZ = 0.0f;
    float baseY = 0.0f;
};

// 0 at rest, 1 at kWakeFullSpeed and above. Negative speed (there is no reverse) is 0.
float wakeSpeed01(float speedMps);

// Heading in radians, length/beam in metres (full extents), speed in m/s.
WakeHull makeWake(float headingRad, float length, float beam, float speedMps,
                  float sceneX, float sceneZ, float baseY = 0.0f);

// u_ship[i]  = (sin, cos, halfLen, halfBeam)
// u_shipDyn[i] = (speed01, sceneX, sceneZ, 0)
// Empty slots have halfLen 0 so the shader skips them. Hulls with no footprint
// are dropped. Hulls past kMaxWakeHulls are dropped (first come, first kept).
struct WakePack {
    float ship[kMaxWakeHulls][4] = {};
    float dyn[kMaxWakeHulls][4] = {};
    int count = 0;
};

int packWakes(const WakeHull* hulls, int hullCount, WakePack& out);

// Analytic foam at undisplaced scene XZ, summed over the pack and clamped to
// 0..1. `timeSec` scrolls the V-wake. The shader runs this on the Gerstner-
// displaced surface point (v_wpos.xz) — same pattern, pinched onto the crests.
float wakeFoamAt(const WakePack& pack, float sceneX, float sceneZ, float timeSec);

// Pier boats + the moored galleon, in scene space. Speed is 0 (a waterline
// ring, no bow wave, wake, or spray). Returns how many were written (<= cap).
int harbourMoorings(float islandSceneX, float islandSceneZ, WakeHull* out, int cap);

struct SprayDrop {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float size = 0.0f;
};

// Deterministic sheets for one hull (same inputs -> same drops). Returns 0
// below kBowSprayMinSpeed, or when the hull has no footprint. Writes at most
// `cap` drops, and never more than kBowSprayPerHull.
int sampleBowSpray(const WakeHull& hull, float timeSec, SprayDrop* out, int cap);

std::vector<TestResult> runWakeSelfTest();

} // namespace sea
