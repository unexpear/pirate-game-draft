// Sea Trial — shadow cascade focus (pure math, no GPU). Public domain (Unlicense).
//
// Two orthographic shadow maps, not a world cascade:
//
//   Cascade 0 — the ship. Half-extent kShipRadius (30 m) at 2048², about 2.9 cm
//   texels. This is the old offshore map, and it is no longer widened when the
//   island is near, so hull/sail shadows stay at that resolution in harbour too.
//
//   Cascade 1 — the island. Half-extent kIslandRadius (128 m) at the same 2048²,
//   about 12.5 cm texels. Covers the heightfield disk (92 m) plus shadow run-out
//   onto the shore. Active while that coast can still sit inside the 500 m
//   camera far plane (kIslandVisibleRange). Past that, the island is out of
//   frame and the second map is skipped.
//
// Receivers prefer cascade 0 when a fragment lands in it (the tight map also
// receives the town while the two squares can overlap, so quay shadows on the
// hull stay sharp). Otherwise they sample cascade 1. Open water that falls in
// neither square is unshadowed — there is no third cascade.
//
// Both maps are R32F + D16 (about 24 MB each, 48 MB together). 3×3 PCF is
// unchanged. A fragment samples one map: the ship map when it is inside it,
// otherwise the island map. The island map is not an extra 9 taps on the hull.
//
// Check on a D3D11 machine (this VM cannot open the window). Island world
// centre is (0, 120). `--head 0` looks toward +Z.
//
//   Offshore, island ahead (~370 m), shadows on the town:
//     sea_trial --shot shadow-offshore.png --scene sail --pos 0 -250 --head 0 \
//       --cam -40 30 -60  0 12 200 --shot-frames 90
//
//   Near-ship only (island past the far plane, cascade 1 off — same 30 m map):
//     sea_trial --shot shadow-ship.png --scene sail --pos 0 -800 --head 0 \
//       --shot-frames 60
//
//   Harbour, both maps (town also drawn into the ship map):
//     sea_trial --shot shadow-harbour.png --scene sail --pos 0 50 --head 0 \
//       --cam -35 22 -25  10 6 40 --shot-frames 90
//
//   A/B the maps: add --noshadow.
#pragma once

namespace shadow {

// Direction TO the sun. Must match the u_lightDir literals in the lit shaders.
constexpr float kSunX = 0.42f;
constexpr float kSunY = 0.68f;
constexpr float kSunZ = -0.55f;

constexpr float kShipRadius = 30.0f;
constexpr float kIslandRadius = 128.0f;
// Mean above-water radius, and the heightfield half-extent (island_gpu). The
// island square covers that disk; the square's underwater corners can still
// fall outside it.
constexpr float kIslandLandDisk = 56.0f;
constexpr float kIslandHeightfield = 92.0f;
// Chase camera sits ~24 m astern; far plane is 500 m. 530 m from the ship keeps
// the near coastline inside that plane when heading toward the island.
constexpr float kIslandVisibleRange = 530.0f;
// Tall casters lean in light-space (sun is ~45°). Draw the town into the ship
// map when the island is close enough that those casters can project into it.
constexpr float kCasterMargin = 24.0f;
constexpr int kCascadeResolution = 2048;
// Eye distance along the sun = radius * this; ortho far = that distance * 2.
constexpr float kLightDistScale = 2.5f;

struct MapFocus {
    float cx = 0.0f;
    float cz = 0.0f;
    float radius = 0.0f;
    bool active = false;
};

struct FrameFocus {
    MapFocus ship;    // cascade 0
    MapFocus island;  // cascade 1
    // Submit town + terrain into the ship map (as well as the island map).
    bool townInShipMap = false;
    // Submit hulls into the island map (as well as the ship map).
    bool hullsInIslandMap = false;
};

// `islandRelX/Z` is the island centre in scene space (the player ship is at the
// origin). `isoRadius` > 0 is the building-isolation shot: one tight map, no
// island cascade.
FrameFocus planFrame(float islandRelX, float islandRelZ, float isoRadius = 0.0f);

// World-space texel of one cascade (square edge / resolution).
float texelMeters(float radius);

// Left-handed look-at basis matching bx::mtxLookAt (view = normalize(at-eye)
// = -sun, right = normalize(cross(worldUp, view)), up = cross(view, right)).
struct LightBasis {
    float right[3];
    float up[3];
    float view[3];
};

LightBasis lightBasis(float sunX, float sunY, float sunZ);

// True when (x,y,z) projects inside the cascade's ortho square and depth range.
// Inactive maps never contain a point.
bool inMap(const LightBasis& basis, const MapFocus& map, float x, float y, float z);

} // namespace shadow
