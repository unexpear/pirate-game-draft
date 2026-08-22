// Sea Trial — 3D scene view (Milestone 0+1). Public domain (Unlicense).
//
// Orchestrates the 3D scene: camera, GPU water surface, and the lit ship
// meshes. Everything now goes through the bgfx shader pipeline (debugdraw is
// gone).
#include "ship_view.h"

#include "ship_model.hpp"
#include "water_gpu.h"
#include "island_gpu.h"
#include "sky_gpu.h"
#include "ship_mesh.h"

#include <bgfx/bgfx.h>
#include <bx/math.h>

#include <algorithm>
#include <cmath>

namespace ship_view {

// Shared sky/atmosphere palette (sky gradient + water horizon fog match).
static const float kSkyTop[3]     = { 0.28f, 0.45f, 0.68f }; // clear day-blue zenith
static const float kSkyHorizon[3] = { 0.90f, 0.85f, 0.74f }; // warm pale daylight haze

void init() {
    sky_gpu::init();
    water_gpu::init();
    island_gpu::init();
    ship_mesh::init();
}

void shutdown() {
    ship_mesh::shutdown();
    island_gpu::shutdown();
    water_gpu::shutdown();
    sky_gpu::shutdown();
}

// Optional fixed screenshot camera (scene space).
// Wall-audit debug: tint each of a building's four wall panels a distinct colour
// and paint its number on it, and drop a walkable character into the town, so
// every wall can be inspected individually.
static bool s_wallDebug = false;
static bool s_auditOnly = false;
static bool s_charOn = false;
static float s_charX = 0.0f, s_charZ = 0.0f, s_charH = 0.0f, s_charY = 0.0f;
void setWallDebug(bool on) { s_wallDebug = on; }
void setAuditOnly(bool on) { s_auditOnly = on; }
void setDebugCharacter(bool on, float x, float z, float heading, float yOffset) {
    s_charOn = on; s_charX = x; s_charZ = z; s_charH = heading; s_charY = yOffset;
}

static bool s_freeCam = false;
static bx::Vec3 s_freeEye = { 0, 0, 0 }, s_freeAt = { 0, 0, 0 };
void setFreeCamera(bool enabled, float ex, float ey, float ez, float ax, float ay, float az) {
    s_freeCam = enabled; s_freeEye = { ex, ey, ez }; s_freeAt = { ax, ay, az };
}

void render(uint16_t viewId, const sea::Ship& ship, const std::vector<sea::Wave>& waves,
            const sea::FloatPose& pose, float timeSec, float heading,
            float worldX, float worldZ, float windDir, float sailFullness,
            int width, int height, float heelScale,
            float cutWorldX, float cutWorldZ, float cutR, float speed) {
    // Chase camera behind the ship's heading (ship stays at the origin; the
    // ocean scrolls past via worldX/worldZ).
    const float dist = 24.0f;
    const float fwdX = bx::sin(heading);
    const float fwdZ = bx::cos(heading);
    const bx::Vec3 eye = s_freeCam ? s_freeEye : bx::Vec3{ -fwdX * dist, 9.0f, -fwdZ * dist };
    const bx::Vec3 at  = s_freeCam ? s_freeAt  : bx::Vec3{ fwdX * 5.0f, -0.4f, fwdZ * 5.0f };
    const bx::Vec3 up = { 0.0f, 1.0f, 0.0f };

    float view[16];
    float proj[16];
    bx::mtxLookAt(view, eye, at, up);
    const bgfx::Caps* caps = bgfx::getCaps();
    const float aspect = float(width) / float(height > 0 ? height : 1);
    bx::mtxProj(proj, 60.0f, aspect, 0.1f, 500.0f, caps->homogeneousDepth);
    bgfx::setViewTransform(viewId, view, proj);
    bgfx::setViewRect(viewId, 0, 0, uint16_t(width), uint16_t(height));

    sky_gpu::render(viewId, kSkyTop, kSkyHorizon);
    // The hull sits at the scene origin; feed the water shader the heading, hull
    // footprint and speed so it draws a waterline foam ring, bow wave and wake.
    const float shipSpd = speed > 0.0f ? (speed / 7.0f < 1.0f ? speed / 7.0f : 1.0f) : 0.0f;
    water_gpu::render(viewId, waves, timeSec, eye.x, eye.y, eye.z, worldX, worldZ,
                      cutWorldX, cutWorldZ, cutR,
                      bx::sin(heading), bx::cos(heading),
                      float(ship.bounds.length) * 0.5f, float(ship.bounds.width) * 0.6f, shipSpd);
    ship_mesh::render(viewId, ship, pose, heading, windDir, sailFullness, timeSec, 0.0f, 0.0f, heelScale);
}

void renderShadow(uint16_t viewId, float relX, float relZ, float halfWid, float halfLen) {
    ship_mesh::renderShadow(viewId, relX, 0.32f, relZ, halfWid, halfLen);
}

void renderShip(uint16_t viewId, const sea::Ship& ship, const sea::FloatPose& pose,
                float heading, float windDir, float sailFullness, float timeSec,
                float posX, float posZ) {
    ship_mesh::render(viewId, ship, pose, heading, windDir, sailFullness, timeSec, posX, posZ);
}

void renderTracer(uint16_t viewId, float x, float y, float z, float size,
                  float r, float g, float b) {
    ship_mesh::renderBox(viewId, x, y, z, size, r, g, b);
}

void renderIsland(uint16_t viewId, float relX, float relZ) {
    // Composed from lit boxes (matching the ship art). Island local frame: player
    // approaches from -z (the south), so the port + shipyard face that way.
    // Structures sit ON the terrain: lift each by the ground height at its
    // footprint (0 offshore, so piers/slipways stay at the waterline).
    auto B = [&](float cx, float cy, float cz, float sx, float sy, float sz,
                 float r, float g, float b, float mat = 1.0f) {
        const float gh = island_gpu::heightAt(cx, cz);
        const float lift = gh > 0.0f ? gh : 0.0f;
        ship_mesh::renderBoxSized(viewId, relX + cx, cy + lift, relZ + cz, sx, sy, sz, r, g, b, mat);
    };
    // Shared-shelf placement: a whole building/structure computes ONE ground lift
    // (curLift, set to the terrain height at its footprint centre) and places every
    // part rigidly on that flat shelf via BL / BLr — so windows, quoins, roofs and
    // walls never drift apart on the slope. The stone plinth hides the shelf's
    // downhill edge. This is how the town terraces the hill instead of floating.
    float curLift = 0.0f;
    auto BL = [&](float cx, float cy, float cz, float sx, float sy, float sz, const float* c, float mat) {
        ship_mesh::renderBoxSized(viewId, relX + cx, cy + curLift, relZ + cz, sx, sy, sz, c[0], c[1], c[2], mat);
    };
    auto BLr = [&](float cx, float cy, float cz, float sx, float sy, float sz,
                   float rx, float ry, float rz, const float* c, float mat) {
        ship_mesh::renderBoxRot(viewId, relX + cx, cy + curLift, relZ + cz, sx, sy, sz, rx, ry, rz, c[0], c[1], c[2], mat);
    };
    // Seat a structure on the HIGHEST terrain in its neighbourhood, so the rising
    // hill never cuts up through the walls (a deep plinth hides the downhill gap).
    auto setShelf = [&](float cx, float cz) {
        float gh = island_gpu::heightAt(cx, cz);
        for (int sx = -1; sx <= 1; ++sx) for (int sz = -1; sz <= 1; ++sz)
            gh = std::max(gh, island_gpu::heightAt(cx + sx * 7.0f, cz + sz * 7.0f));
        curLift = std::max(0.0f, gh);
    };
    const float MAT_STONE = 2.0f, MAT_FLAT = 0.0f;
    // Palette (the landmass itself is now the coloured terrain heightfield)
    const float wood[3]  = { 0.42f, 0.28f, 0.15f };
    const float timber[3]= { 0.52f, 0.40f, 0.27f };
    const float roof[3]  = { 0.35f, 0.20f, 0.14f };
    const float stone[3] = { 0.58f, 0.55f, 0.50f };
    const float white[3] = { 0.90f, 0.90f, 0.85f };
    const float red[3]   = { 0.70f, 0.16f, 0.13f };
    const float crate[3] = { 0.55f, 0.42f, 0.24f };
    const float metal[3] = { 0.25f, 0.22f, 0.20f };
    const float pileC[3] = { 0.30f, 0.22f, 0.14f };

    // Support pilings under a pier/dock (from below the water up to its deck), so
    // over-water structures stand on posts instead of floating.
    auto pilings = [&](float px, float pz, float plen, float halfw, float deckY) {
        const int n = int(plen / 6.0f);
        for (int i = 0; i <= n; ++i) {
            const float z = pz - plen * 0.5f + plen * i / float(n < 1 ? 1 : n);
            B(px - halfw, deckY * 0.5f - 1.5f, z, 0.5f, deckY + 3.0f, 0.5f, pileC[0], pileC[1], pileC[2], MAT_FLAT);
            B(px + halfw, deckY * 0.5f - 1.5f, z, 0.5f, deckY + 3.0f, 0.5f, pileC[0], pileC[1], pileC[2], MAT_FLAT);
        }
    };
    // A small moored boat (hull + a stub mast) tied at the dock.
    auto mooredBoat = [&](float cx, float cz, float len) {
        B(cx, 0.7f, cz, 2.6f, 1.0f, len, 0.44f, 0.30f, 0.17f, 1.0f);
        B(cx, 0.3f, cz + len * 0.5f, 1.4f, 0.7f, 1.2f, 0.44f, 0.30f, 0.17f, 1.0f);
        B(cx, 3.2f, cz, 0.2f, 5.0f, 0.2f, 0.36f, 0.26f, 0.16f, MAT_FLAT);
    };
    // A big MOORED SAILING SHIP alongside the quay — the pirate-port anchor. A
    // weathered plank hull with a raised stern, three masts with furled off-white
    // sails on yards, and a bowsprit; the masts rise above the town rooflines.
    auto mooredShip = [&](float cx, float cz, float len) {
        const float hull[3] = { 0.34f, 0.24f, 0.15f };
        const float deck[3] = { 0.46f, 0.33f, 0.20f };
        const float spar[3] = { 0.38f, 0.28f, 0.17f };
        const float canv[3] = { 0.86f, 0.82f, 0.70f };
        B(cx, 1.4f, cz, 6.0f, 3.6f, len, hull[0], hull[1], hull[2], 1.0f);           // hull
        B(cx, 3.4f, cz, 5.2f, 1.2f, len * 0.94f, deck[0], deck[1], deck[2], 1.0f);   // gunwale/deck
        B(cx, 5.0f, cz + len * 0.42f, 5.6f, 3.4f, len * 0.18f, hull[0], hull[1], hull[2], 1.0f); // raised stern castle
        B(cx, 2.6f, cz - len * 0.5f - 3.0f, 0.5f, 0.5f, 6.0f, spar[0], spar[1], spar[2], MAT_FLAT); // bowsprit
        for (int m = -1; m <= 1; ++m) {
            const float mz = cz + m * len * 0.26f;
            const float mh = 20.0f - float(m + 1) * 1.5f;
            B(cx, mh * 0.5f + 3.0f, mz, 0.55f, mh, 0.55f, spar[0], spar[1], spar[2], MAT_FLAT); // mast
            const float yy = 3.0f + mh * 0.62f;
            B(cx, yy + 0.6f, mz, 7.0f, 0.4f, 0.4f, spar[0], spar[1], spar[2], MAT_FLAT);        // yard
            B(cx, yy, mz + 0.3f, 6.4f, 3.0f, 0.3f, canv[0], canv[1], canv[2], MAT_FLAT);        // furled/set sail
        }
    };

    // A rotated lit box at an ABSOLUTE height (no terrain lift) — the primitive
    // for sloped roof panels. gableRoof/pyramidRoof compute the lift once and add
    // it to every panel so the whole roof moves as one rigid piece with the walls.
    auto BRabs = [&](float cx, float cy, float cz, float sx, float sy, float sz,
                     float rx, float ry, float rz, const float* c, float mat) {
        ship_mesh::renderBoxRot(viewId, relX + cx, cy, relZ + cz, sx, sy, sz, rx, ry, rz, c[0], c[1], c[2], mat);
    };
    // A PITCHED GABLE roof over a building: two sloped panels meeting at a ridge
    // (ridge runs along z / the depth), an eave + gable overhang, a ridge cap and
    // triangular gable-end walls — so the roof reads as a real roof, not a flat
    // slab on a cube. `wallTopY` is the wall-top height (pre-lift); `rise` is the
    // roof height; `rc` roof colour, `wc`/`wmat` the wall colour+material used to
    // fill the gable-end triangles.
    auto gableRoof = [&](float cx, float cz, float w, float d, float wallTopY,
                         float rise, const float* rc, const float* wc, float wmat) {
        const float lift  = std::max(0.0f, island_gpu::heightAt(cx, cz));
        const float halfW = w * 0.5f + 0.4f;                       // modest eave overhang past the walls
        const float depth = d + 0.9f;                              // modest gable overhang front/back
        const float slope = std::sqrt(halfW * halfW + rise * rise);
        const float ang   = std::atan2(rise, halfW);
        const float py    = wallTopY + lift + rise * 0.5f;
        const float pl    = slope * 1.1f;                          // panels overlap at the ridge (no gap) + deeper eave
        BRabs(cx - halfW * 0.5f, py, cz, pl, 0.4f, depth, 0.0f, 0.0f, -ang, rc, MAT_FLAT); // left slope (/\ peak)
        BRabs(cx + halfW * 0.5f, py, cz, pl, 0.4f, depth, 0.0f, 0.0f,  ang, rc, MAT_FLAT); // right slope
        const float ridge[3] = { rc[0] * 0.68f, rc[1] * 0.68f, rc[2] * 0.68f };
        BRabs(cx, wallTopY + lift + rise + 0.08f, cz, 1.1f, 0.42f, depth, 0.0f, 0.0f, 0.0f, ridge, MAT_FLAT); // ridge cap covers the seam
        for (int e = -1; e <= 1; e += 2) {                         // gable-end triangles (front & back)
            const float ez = cz + e * (d * 0.5f - 0.2f);           // inset so the roof panels overhang it
            const int steps = 16;                                  // enough steps that no sky shows through
            for (int s = 0; s < steps; ++s) {
                const float tTop = float(s + 1) / float(steps);    // width at the step's TOP edge follows
                const float fw = 2.0f * halfW * (1.0f - tTop);     // the roof line (never pokes past it)
                if (fw < 0.25f) continue;
                const float fy = wallTopY + lift + rise * (float(s) / float(steps)) + rise / (2.0f * steps);
                BRabs(cx, fy, ez, fw, rise / steps * 1.35f, 0.45f, 0.0f, 0.0f, 0.0f, wc, wmat);
            }
        }
    };
    // A four-sided PYRAMID/HIP roof (for towers) — four tilted panels to an apex.
    auto pyramidRoof = [&](float cx, float cz, float w, float wallTopY, float rise, const float* rc) {
        const float lift  = std::max(0.0f, island_gpu::heightAt(cx, cz));
        const float half  = w * 0.5f + 0.4f;
        const float slope = std::sqrt(half * half + rise * rise);
        const float ang   = std::atan2(rise, half);
        const float py    = wallTopY + lift + rise * 0.5f;
        BRabs(cx - half * 0.5f, py, cz, slope, 0.4f, w + 0.8f, 0.0f, 0.0f, -ang, rc, MAT_FLAT); // -x (apex peak)
        BRabs(cx + half * 0.5f, py, cz, slope, 0.4f, w + 0.8f, 0.0f, 0.0f,  ang, rc, MAT_FLAT); // +x
        BRabs(cx, py, cz - half * 0.5f, w + 0.8f, 0.4f, slope,  ang, 0.0f, 0.0f, rc, MAT_FLAT); // -z
        BRabs(cx, py, cz + half * 0.5f, w + 0.8f, 0.4f, slope, -ang, 0.0f, 0.0f, rc, MAT_FLAT); // +z
    };

    // --- Landmass: a real procedural island (coloured heightfield), rising from
    // the sea with an irregular coastline, beach, meadow and rocky heights. It
    // occludes the ocean where it stands above the waves; the sea is also carved
    // out under it (water_gpu land cut) so nothing floats on a flat sheet. ---
    island_gpu::render(viewId, relX, relZ);

    // Angular ROCK CRAGS at the summit to break the smooth-dome silhouette into a
    // ridged peak (drawn directly at the terrain height, not lifted).
    auto crag = [&](float cx, float cy, float cz, float sx, float sy, float sz) {
        ship_mesh::renderBoxSized(viewId, relX + cx, cy, relZ + cz, sx, sy, sz, 0.50f, 0.47f, 0.43f, MAT_STONE);
    };
    crag(6, 18, 34, 8, 9, 7);   crag(-1, 15, 30, 5, 7, 5);   crag(15, 16, 38, 6, 7, 5);
    crag(2, 20, 37, 5, 7, 4);   crag(11, 14, 30, 4, 6, 4);

    // --- Port (south shore) ---
    // Solid stone quay: the block extends well BELOW the waterline (no floating
    // slab underside) and back INTO the beach, so it reads as a masonry quay wall
    // rising from the water rather than a plate hovering over it.
    B(-6, -1.55f, -40, 82, 6.9f, 10, stone[0], stone[1], stone[2], MAT_STONE);   // stone quay (deep face)
    B(-26, 1.1f, -54, 4.5f, 0.8f, 30, wood[0], wood[1], wood[2]);  // pier 1
    B(-2, 1.1f, -56, 4.5f, 0.8f, 34, wood[0], wood[1], wood[2]);   // pier 2
    B(22, 1.1f, -52, 4.5f, 0.8f, 26, wood[0], wood[1], wood[2]);   // pier 3
    pilings(-26, -54, 30, 1.7f, 1.5f); pilings(-2, -56, 34, 1.7f, 1.5f); pilings(22, -52, 26, 1.7f, 1.5f);
    mooredBoat(-32, -50, 7); mooredBoat(-8, -52, 8);         // small boats tied along the piers
    mooredShip(31, -56, 22);                                 // the hero: a moored galleon alongside the east pier
    // (Old warehouses A/B and the harbourmaster tower are superseded by the new
    //  King's Bonded Warehouse and the west-point Sea Bastion in the town below.)
    B(40, 10, -62, 5, 20, 5, white[0], white[1], white[2], MAT_STONE); // lighthouse
    B(40, 20.5f, -62, 6, 2, 6, red[0], red[1], red[2]);
    B(-8, 2.1f, -40, 2.4f, 2, 2.4f, crate[0], crate[1], crate[2]); // crates on the quay
    B(-4, 2.1f, -41, 2.4f, 2, 2.4f, crate[0], crate[1], crate[2]);
    B(6, 2.6f, -39, 3, 3, 3, crate[0], crate[1], crate[2]);
    B(-26, 1.7f, -60, 3, 1.2f, 8, wood[0], wood[1], wood[2]);      // moored fishing boat

    // --- Pirate town: a row of shops along the waterfront street, a little
    // square behind, fences, market stalls, lamps and signs. Each shop is a
    // distinct form/colour with a hanging sign so it reads as its own trade. ---
    // Each trade gets its OWN wall colour, roof colour and a big projecting
    // signboard, so no two shops read alike from the water.
    // Black Flag / Caribbean colonial port: whitewashed & pastel daub walls,
    // warm terracotta tile roofs, distinguished by signs, awnings and form.
    const float smithyW[3] = { 0.40f, 0.39f, 0.41f };  // grey stone smithy
    const float tailorW[3] = { 0.90f, 0.87f, 0.78f };  // whitewash
    const float grocerW[3] = { 0.86f, 0.78f, 0.60f };  // sandy cream
    const float tavW[3]    = { 0.80f, 0.42f, 0.34f };  // warm terracotta-red tavern
    const float fenceWall[3]= { 0.30f, 0.26f, 0.22f }; // black-market dark timber
    const float cottW[3]   = { 0.72f, 0.80f, 0.80f };  // pale sea-green colonial
    const float cream[3]   = { 0.88f, 0.83f, 0.70f };  // cream daub
    const float darkW[3]   = { 0.26f, 0.20f, 0.15f };  // doors / trim / brackets
    const float redRoof[3] = { 0.66f, 0.30f, 0.18f };  // terracotta tile (warm)
    const float tealRoof[3]= { 0.66f, 0.30f, 0.18f };  // (colonial: terracotta too)
    const float goldRoof[3]= { 0.66f, 0.30f, 0.18f };  // (colonial: terracotta too)
    const float darkRoof[3]= { 0.34f, 0.22f, 0.15f };  // weathered shingle
    const float slate[3]   = { 0.42f, 0.43f, 0.47f };  // slate (smithy)
    const float plank[3]   = { 0.50f, 0.37f, 0.22f };  // boardwalk planks
    const float shutter[3] = { 0.30f, 0.45f, 0.60f };  // blue colonial shutters
    const float amberC[3]  = { 0.99f, 0.76f, 0.30f };  // lantern amber
    const float glow[3]    = { 1.00f, 0.52f, 0.16f };  // forge glow
    const float smoke[3]   = { 0.58f, 0.58f, 0.60f };  // chimney smoke
    const float cloth[3]   = { 0.56f, 0.30f, 0.64f };  // dyer's purple
    const float green[3]   = { 0.28f, 0.56f, 0.30f };  // grocer green
    const float barrel[3]  = { 0.46f, 0.32f, 0.18f };  // barrel wood

    // A shop/house: walls + overhanging roof + a south-facing door, and (if a
    // sign colour is given) a big signboard projecting from the front over the door.
    auto building = [&](float cx, float cz, float w, float hgt, float d,
                        const float* wall, const float* rf, float mat, const float* sc) {
        const float front = cz - d * 0.5f;
        // Walls sink 3 units into the ground so the base stays embedded on the
        // sloped terrain (no floating downhill edge).
        B(cx, (hgt - 3.0f) * 0.5f, cz, w, hgt + 3.0f, d, wall[0], wall[1], wall[2], mat);
        // A pitched gable roof (not a flat slab) — the main thing that stops the
        // buildings reading as stacked cubes.
        const float rise = std::max(2.6f, w * 0.32f);
        gableRoof(cx, cz, w, d, hgt, rise, rf, wall, mat);
        B(cx, 1.2f, front - 0.05f, 1.6f, 2.4f, 0.25f, darkW[0], darkW[1], darkW[2], MAT_FLAT); // door
        if (sc) {
            const float bx2 = cx + w * 0.30f;
            B(bx2, hgt - 0.6f, front - 0.5f, 0.2f, 0.2f, 1.3f, darkW[0], darkW[1], darkW[2], MAT_FLAT); // bracket
            B(bx2, hgt - 1.0f, front - 1.1f, 2.6f, 1.7f, 0.22f, sc[0], sc[1], sc[2], MAT_FLAT);          // board
            B(bx2, hgt - 1.0f, front - 1.22f, 1.1f, 1.1f, 0.1f, 0.94f, 0.90f, 0.80f, MAT_FLAT);          // painted trade glyph (light, reads on the board)
        }
    };
    auto stall = [&](float cx, float cz, const float* awn) {
        for (int ax = -1; ax <= 1; ax += 2) for (int az = -1; az <= 1; az += 2)
            B(cx + ax * 1.7f, 1.2f, cz + az * 1.3f, 0.18f, 2.4f, 0.18f, darkW[0], darkW[1], darkW[2], MAT_FLAT);
        B(cx, 2.7f, cz, 4.4f, 0.28f, 3.4f, awn[0], awn[1], awn[2], MAT_FLAT);
        B(cx, 1.05f, cz + 1.2f, 3.8f, 0.9f, 0.55f, wood[0], wood[1], wood[2]);
    };
    auto lamp = [&](float cx, float cz) {
        B(cx, 1.9f, cz, 0.18f, 3.8f, 0.18f, darkW[0], darkW[1], darkW[2], MAT_FLAT);
        B(cx, 4.0f, cz, 0.55f, 0.55f, 0.55f, amberC[0], amberC[1], amberC[2], MAT_FLAT);
    };
    auto barrelAt = [&](float cx, float cz) { B(cx, 0.9f, cz, 1.3f, 1.8f, 1.3f, barrel[0], barrel[1], barrel[2]); };
    auto fenceX = [&](float x0, float x1, float z) {
        const int n = int((x1 - x0) / 1.6f);
        for (int i = 0; i <= n; ++i) B(x0 + (x1 - x0) * i / float(n < 1 ? 1 : n), 0.8f, z, 0.2f, 1.6f, 0.2f, wood[0], wood[1], wood[2], MAT_FLAT);
        B((x0 + x1) * 0.5f, 1.2f, z, x1 - x0, 0.18f, 0.16f, wood[0], wood[1], wood[2], MAT_FLAT);
    };
    auto fenceZ = [&](float z0, float z1, float x) {
        const int n = int((z1 - z0) / 1.6f);
        for (int i = 0; i <= n; ++i) B(x, 0.8f, z0 + (z1 - z0) * i / float(n < 1 ? 1 : n), 0.2f, 1.6f, 0.2f, wood[0], wood[1], wood[2], MAT_FLAT);
        B(x, 1.2f, (z0 + z1) * 0.5f, 0.16f, 0.18f, z1 - z0, wood[0], wood[1], wood[2], MAT_FLAT);
    };

    // Blue colonial shutters flanking a front window.
    auto shutters = [&](float cx, float cz, float d, float y) {
        const float f = cz - d * 0.5f - 0.02f;
        B(cx, y, f, 1.6f, 1.4f, 0.1f, 0.30f, 0.34f, 0.40f, MAT_FLAT);        // window
        B(cx - 1.9f, y, f, 0.5f, 1.5f, 0.12f, shutter[0], shutter[1], shutter[2], MAT_FLAT);
        B(cx + 1.9f, y, f, 0.5f, 1.5f, 0.12f, shutter[0], shutter[1], shutter[2], MAT_FLAT);
    };

    // ===================== Detailed architecture kit =====================
    // Turns a plain box into believable colonial architecture: stone plinths,
    // corner quoins, framed windows with sills/lintels/shutters, exposed timber
    // framing, belt courses, chimneys and dormers — composed by house().
    const float beam[3]   = { 0.30f, 0.21f, 0.14f };  // dark oak framing timber
    const float glassC[3] = { 0.66f, 0.76f, 0.82f };  // sky-reflecting glass (lifted: the panes sit
                                                      // in a shaded reveal, so they need to read bright)
    const float sillC[3]  = { 0.82f, 0.74f, 0.57f };  // warm sandstone (sills, lintels, steps, plinths)
    const float quoinC[3] = { 0.83f, 0.75f, 0.59f };  // warm corner stone
    // Shutter-colour variety: windowOn uses curShutter when a building sets one,
    // else the default blue — so the town isn't one repeated blue-shutter motif.
    const float shutGreen[3] = { 0.28f, 0.48f, 0.33f };
    const float shutRed[3]   = { 0.60f, 0.27f, 0.23f };
    const float shutDark[3]  = { 0.26f, 0.23f, 0.20f };
    const float shutTeal[3]  = { 0.24f, 0.46f, 0.48f };
    const float* curShutter = nullptr;

    // A 7-SEGMENT DIGIT painted on a wall face with outward normal (nx,0,nz) —
    // used by wall-debug mode to number each wall so an audit can name exactly
    // which wall it is looking at.
    auto digitOn = [&](int v, float cx, float cy, float cz, float nx, float nz, float s, const float* c) {
        const bool face = (nz != 0.0f);
        auto seg = [&](float ax, float ay, float lw, float lh) {
            const float x = face ? cx + ax * s : cx + nx * 0.12f;
            const float z = face ? cz + nz * 0.12f : cz + ax * s;
            BL(x, cy + ay * s, z, face ? lw * s : 0.14f, lh * s, face ? 0.14f : lw * s, c, MAT_FLAT);
        };
        const bool A = (v != 1 && v != 4);
        const bool B = (v != 5 && v != 6);
        const bool C = (v != 2);
        const bool D = (v != 1 && v != 4 && v != 7);
        const bool E = (v == 0 || v == 2 || v == 6 || v == 8);
        const bool F = (v != 1 && v != 2 && v != 3 && v != 7);
        const bool G = (v != 0 && v != 1 && v != 7);
        const float hw = 0.52f, t = 0.18f;
        if (A) seg(0.0f,  1.0f, 1.0f, t);      // top
        if (G) seg(0.0f,  0.0f, 1.0f, t);      // middle
        if (D) seg(0.0f, -1.0f, 1.0f, t);      // bottom
        if (F) seg(-hw,   0.5f, t, 1.0f);      // upper-left
        if (B) seg( hw,   0.5f, t, 1.0f);      // upper-right
        if (E) seg(-hw,  -0.5f, t, 1.0f);      // lower-left
        if (C) seg( hw,  -0.5f, t, 1.0f);      // lower-right
    };

    // All kit helpers place via BL/BLr (the current shelf) so a building is rigid.
    // A framed WINDOW on a wall face with outward normal (nx,0,nz) (one is +/-1).
    auto windowOn = [&](float cx, float cy, float cz, float nx, float nz, float w, float h, bool shut) {
        const bool front = (nz != 0.0f);
        auto place = [&](float along, float up, float out, float ew, float eh, float ed, const float* c) {
            const float x = front ? cx + along : cx + nx * out;
            const float z = front ? cz + nz * out : cz + along;
            BL(x, cy + up, z, front ? ew : ed, eh, front ? ed : ew, c, MAT_FLAT);
        };
        // Depth order matters: the walls are solid boxes, so a pane can't truly be
        // recessed — instead the SURROUND projects furthest and the pane sits back
        // inside it, which reads as a window cut INTO the wall. (Getting this
        // backwards makes every window look stuck on the outside like a sticker.)
        place(0, 0, 0.09f, w + 0.44f, h + 0.44f, 0.26f, beam);       // stone/timber surround (outermost)
        place(0, 0, 0.00f, w, h, 0.12f, glassC);                     // glazing, set back in the reveal
        place(0, 0, 0.04f, 0.10f, h, 0.08f, beam);                   // vertical muntin (just proud of the glass)
        place(0, 0, 0.04f, w, 0.10f, 0.08f, beam);                   // horizontal muntin
        place(0, -h * 0.5f - 0.30f, 0.13f, w + 0.66f, 0.20f, 0.34f, sillC); // sill (modest shelf under the opening)
        place(0,  h * 0.5f + 0.30f, 0.10f, w + 0.60f, 0.18f, 0.26f, sillC); // lintel
        if (shut) {
            const float* sc = curShutter ? curShutter : shutter;      // shutters hinge on the face, flanking the surround
            place(-w * 0.5f - 0.42f, 0, 0.12f, 0.40f, h + 0.1f, 0.13f, sc);
            place( w * 0.5f + 0.42f, 0, 0.12f, 0.40f, h + 0.1f, 0.13f, sc);
        }
    };
    // A framed DOOR on the front face (normal -z), dressed-stone step, optional lantern.
    auto doorOn = [&](float cx, float cz, float w, float h, bool lantern) {
        const float f = cz;
        BL(cx, h * 0.5f, f - 0.02f, w + 0.5f, h + 0.4f, 0.18f, beam, MAT_FLAT); // frame
        BL(cx, h * 0.5f, f - 0.10f, w, h, 0.12f, darkW, MAT_FLAT);              // door
        BL(cx, 0.2f, f - 0.6f, w + 1.1f, 0.4f, 1.3f, sillC, MAT_STONE);         // step
        if (lantern) BL(cx + w * 0.5f + 0.5f, h - 0.3f, f - 0.3f, 0.5f, 0.5f, 0.5f, amberC, MAT_FLAT);
    };
    // A masonry CHIMNEY from baseY to topY, cap, pots, optional smoke plume.
    auto chimneyAt = [&](float cx, float cz, float baseY, float topY, bool smoking) {
        const float h = topY - baseY;
        BL(cx, baseY + h * 0.5f, cz, 1.8f, h, 1.8f, stone, MAT_STONE);   // stack
        BL(cx, topY + 0.2f, cz, 2.3f, 0.5f, 2.3f, darkW, MAT_FLAT);      // cap
        BL(cx - 0.5f, topY + 0.6f, cz - 0.5f, 0.5f, 0.7f, 0.5f, darkW, MAT_FLAT); // pots
        BL(cx + 0.5f, topY + 0.6f, cz + 0.5f, 0.5f, 0.7f, 0.5f, darkW, MAT_FLAT);
        if (smoking) for (int s = 0; s < 4; ++s)
            BL(cx + s * 0.5f, topY + 1.4f + s * 2.0f, cz + s * 0.3f, 1.6f - s * 0.22f, 1.7f, 1.6f - s * 0.22f, smoke, MAT_FLAT);
    };
    // Alternating CORNER QUOINS up all four corners.
    auto quoins = [&](float cx, float cz, float w, float d, float topY) {
        for (int sx = -1; sx <= 1; sx += 2) for (int sz = -1; sz <= 1; sz += 2) {
            const int n = std::max(2, int(topY / 1.3f));
            for (int i = 0; i < n; ++i) {
                // Inset so each stone only steps ~0.1 proud of the two wall faces it
                // wraps — a dressed corner, not blocks hanging off the building.
                const float s = (i % 2 == 0) ? 1.5f : 1.0f;
                const float qx = cx + sx * (w * 0.5f - s * 0.5f + 0.10f);
                const float qz = cz + sz * (d * 0.5f - s * 0.5f + 0.10f);
                BL(qx, 0.7f + i * 1.3f, qz, s, 1.1f, s, quoinC, MAT_STONE);
            }
        }
    };
    // Exposed TIMBER FRAMING on the front face (studs, rails, posts, braces).
    auto timberFrame = [&](float cx, float cz, float d, float w, float sillY, float topY) {
        const float f = cz - d * 0.5f - 0.07f;
        const float h = topY - sillY, midY = (sillY + topY) * 0.5f;
        BL(cx, sillY, f, w, 0.36f, 0.12f, beam, MAT_FLAT);  // sill rail
        BL(cx, midY,  f, w, 0.36f, 0.12f, beam, MAT_FLAT);  // mid rail
        BL(cx, topY,  f, w, 0.40f, 0.12f, beam, MAT_FLAT);  // top plate
        const int ns = std::max(2, int(w / 2.6f));
        for (int i = 0; i <= ns; ++i)
            BL(cx - w * 0.5f + w * i / float(ns), midY, f, 0.30f, h, 0.12f, beam, MAT_FLAT); // studs
        BL(cx - w * 0.5f, midY, f, 0.44f, h, 0.14f, beam, MAT_FLAT); // corner posts
        BL(cx + w * 0.5f, midY, f, 0.44f, h, 0.14f, beam, MAT_FLAT);
        const float bl = std::sqrt(w * w * 0.06f + h * h * 0.16f);
        BLr(cx - w * 0.32f, sillY + h * 0.25f, f, bl, 0.28f, 0.12f, 0, 0,  0.7f, beam, MAT_FLAT); // braces
        BLr(cx + w * 0.32f, sillY + h * 0.25f, f, bl, 0.28f, 0.12f, 0, 0, -0.7f, beam, MAT_FLAT);
    };
    // A roof DORMER poking from the front slope: a little gabled box + window.
    auto dormer = [&](float cx, float cz, float baseY, float w, const float* wall) {
        BL(cx, baseY + 1.1f, cz, w, 2.2f, 1.6f, wall, 1.0f);
        const float hw = w * 0.5f + 0.3f, ris = 1.2f;
        const float sl = std::sqrt(hw * hw + ris * ris), ang = std::atan2(ris, hw);
        BLr(cx - hw * 0.5f, baseY + 2.2f + ris * 0.5f, cz, sl, 0.34f, 1.9f, 0, 0, -ang, darkRoof, MAT_FLAT);
        BLr(cx + hw * 0.5f, baseY + 2.2f + ris * 0.5f, cz, sl, 0.34f, 1.9f, 0, 0,  ang, darkRoof, MAT_FLAT);
        windowOn(cx, baseY + 1.2f, cz - 0.85f, 0, -1, 1.0f, 1.1f, false);
    };

    // A pitched GABLE roof on the current shelf (BLr version of gableRoof), with a
    // ridge cap + tapered gable-end fill; used by house() so the roof rides the
    // same shelf as the walls (no per-box terrain drift).
    auto gable = [&](float cx, float cz, float w, float d, float wallTopY, float rise, const float* rc, const float* wc, float wmat) {
        const float halfW = w * 0.5f + 0.05f, depth = d + 0.1f;   // roof stays WITHIN the footprint (no splay into neighbours)
        const float slope = std::sqrt(halfW * halfW + rise * rise), ang = std::atan2(rise, halfW);
        const float py = wallTopY + rise * 0.5f, pl = slope * 1.02f;
        BLr(cx - halfW * 0.5f, py, cz, pl, 0.42f, depth, 0, 0, -ang, rc, MAT_FLAT); // /\ peak (was inverted)
        BLr(cx + halfW * 0.5f, py, cz, pl, 0.42f, depth, 0, 0,  ang, rc, MAT_FLAT);
        const float ridge[3] = { rc[0] * 0.68f, rc[1] * 0.68f, rc[2] * 0.68f };
        BLr(cx, wallTopY + rise + 0.06f, cz, 0.9f, 0.42f, depth, 0, 0, 0, ridge, MAT_FLAT);
        BL(cx, wallTopY + 0.2f, cz, w + 0.15f, 0.5f, d + 0.15f, ridge, MAT_FLAT); // eave fascia (thickness, tight)
        // GABLE-END WALL: fill the triangle under each roof slope. The step widths
        // follow the actual roof line (half-width shrinks to nothing at the ridge)
        // and use enough steps that no sky shows through — getting this wrong left
        // open gaps you could see straight through the building.
        for (int e = -1; e <= 1; e += 2) {
            const float ez = cz + e * (d * 0.5f - 0.2f);
            const int N = 16;
            for (int s = 0; s < N; ++s) {
                const float tTop = float(s + 1) / float(N);         // width at the step's TOP edge,
                const float fw = 2.0f * halfW * (1.0f - tTop);      // so it never pokes out past the roof
                if (fw < 0.25f) continue;
                const float yb = wallTopY + rise * (float(s) / float(N));
                BLr(cx, yb + rise / (2.0f * N), ez, fw, rise / N * 1.35f, 0.45f, 0, 0, 0, wc, wmat);
            }
            // Bargeboards: trim laid along each gable slope, capping the stepped
            // edge so no sliver of sky shows between the fill and the roof.
            const float bez = cz + e * (d * 0.5f + 0.02f);
            BLr(cx - halfW * 0.5f, wallTopY + rise * 0.5f, bez, slope * 1.04f, 0.62f, 0.42f, 0, 0, -ang, ridge, MAT_FLAT);
            BLr(cx + halfW * 0.5f, wallTopY + rise * 0.5f, bez, slope * 1.04f, 0.62f, 0.42f, 0, 0,  ang, ridge, MAT_FLAT);
        }
    };

    // ===== house(): a composable, detailed colonial building on a flat shelf. =====
    // style 0 = whitewash daub + quoins, 1 = timber-framed upper storey, 2 = stone.
    auto house = [&](float cx, float cz, float w, float d, int stories, float storyH,
                     const float* wall, const float* rf, float wmat, int style,
                     int cols, const float* sign, bool chim, bool dorm, const float* shutC = nullptr) {
        // Seat the building's FRONT (the side the player sees from the sea) on the
        // terrain, so ground-floor windows sit on the ground — not stranded in the
        // air on a tall pedestal. The terrain rising behind is hidden by the walls
        // and the terrace's retaining wall; a modest plinth smooths the front edge.
        const float front = cz - d * 0.5f;
        float gh = island_gpu::heightAt(cx, front + d * 0.12f);
        gh = std::max(gh, island_gpu::heightAt(cx - w * 0.42f, front + d * 0.12f));
        gh = std::max(gh, island_gpu::heightAt(cx + w * 0.42f, front + d * 0.12f));
        curLift = std::max(0.0f, gh);
        curShutter = shutC;
        const float topY = stories * storyH;
        BL(cx, -1.5f, cz, w + 0.8f, 6.0f, d + 0.8f, sillC, MAT_STONE);   // modest stone plinth at the front
        // FOUR SEPARATE WALL PANELS (1 front / 2 right / 3 back / 4 left) rather
        // than one solid block, so each wall can be inspected — and tinted and
        // numbered in wall-debug mode. Front/back span the full width and the
        // sides span the full depth, so the panels overlap at every corner and
        // cannot leave a seam. Sunk deep so the rear grips the rising slope.
        {
            const float H = topY + 8.0f, wy = (topY - 8.0f) * 0.5f, tw = 0.9f;
            const float* w1 = wall; const float* w2 = wall;
            const float* w3 = wall; const float* w4 = wall;
            float m1 = wmat, m2 = wmat, m3 = wmat, m4 = wmat;
            const float dbg1[3] = { 0.85f, 0.20f, 0.18f }; // 1 front  red
            const float dbg2[3] = { 0.20f, 0.52f, 0.88f }; // 2 right  blue
            const float dbg3[3] = { 0.95f, 0.78f, 0.15f }; // 3 back   yellow
            const float dbg4[3] = { 0.24f, 0.70f, 0.32f }; // 4 left   green
            if (s_wallDebug) { w1 = dbg1; w2 = dbg2; w3 = dbg3; w4 = dbg4;
                               m1 = m2 = m3 = m4 = MAT_FLAT; }
            BL(cx, wy, cz - d * 0.5f + tw * 0.5f, w,  H, tw, w1, m1); // 1 front  (-z)
            BL(cx + w * 0.5f - tw * 0.5f, wy, cz, tw, H, d,  w2, m2); // 2 right  (+x)
            BL(cx, wy, cz + d * 0.5f - tw * 0.5f, w,  H, tw, w3, m3); // 3 back   (+z)
            BL(cx - w * 0.5f + tw * 0.5f, wy, cz, tw, H, d,  w4, m4); // 4 left   (-x)
            BL(cx, 0.25f, cz, w - 1.4f, 0.6f, d - 1.4f, sillC, MAT_STONE); // floor to stand on inside
            if (s_wallDebug) {   // wall numbers, floated clear of the wall so they stay legible
                const float lbl[3] = { 0.99f, 0.99f, 0.99f };
                const float ly = storyH * 1.15f, g = 1.9f, o = 3.0f;
                digitOn(1, cx, ly, cz - d * 0.5f - o,  0, -1, g, lbl);
                digitOn(2, cx + w * 0.5f + o, ly, cz,  1,  0, g, lbl);
                digitOn(3, cx, ly, cz + d * 0.5f + o,  0,  1, g, lbl);
                digitOn(4, cx - w * 0.5f - o, ly, cz, -1,  0, g, lbl);
            }
        }
        for (int s = 1; s < stories; ++s)                               // belt courses
            BL(cx, s * storyH, cz, w + 0.3f, 0.5f, d + 0.3f, sillC, MAT_STONE);
        if (style == 0) quoins(cx, cz, w, d, topY);
        if (style == 1 && stories >= 2) timberFrame(cx, cz, d, w * 0.98f, storyH + 0.4f, topY - 0.3f);
        for (int s = 0; s < stories; ++s) {
            const float wy = s * storyH + storyH * 0.56f;
            for (int c = 0; c < cols; ++c) {
                const float wx = cx + (cols > 1 ? (-w * 0.5f + w * (c + 0.5f) / cols) : 0.0f);
                if (s == 0 && cols % 2 == 1 && c == cols / 2) continue; // leave centre-ground for the door
                windowOn(wx, wy, front - 0.02f, 0, -1, 1.5f, 1.7f, true);
            }
            windowOn(cx + w * 0.5f + 0.02f, wy, cz, 1, 0, 1.3f, 1.6f, false); // east side
            windowOn(cx - w * 0.5f - 0.02f, wy, cz,-1, 0, 1.3f, 1.6f, false); // west side
        }
        doorOn(cx, front - 0.02f, 1.8f, 3.0f, sign != nullptr);
        const float rise = std::min(3.6f, std::max(2.3f, w * 0.22f));
        gable(cx, cz, w, d, topY, rise, rf, wall, wmat);
        if (dorm) { dormer(cx - w * 0.24f, front + 0.5f, topY, 2.2f, wall); dormer(cx + w * 0.24f, front + 0.5f, topY, 2.2f, wall); }
        if (chim) chimneyAt(cx + w * 0.32f, cz + d * 0.18f, topY - 1.0f, topY + 4.0f, false);
        if (sign) {
            const float bx2 = cx - w * 0.34f, sy = storyH * 0.85f;
            BL(bx2, sy + 0.9f, front - 0.6f, 0.22f, 0.22f, 1.3f, beam, MAT_FLAT);   // iron bracket
            BL(bx2, sy, front - 1.15f, 3.0f, 2.0f, 0.24f, sign, MAT_FLAT);          // big painted signboard over the door
            const float glyphC[3] = { 0.96f, 0.92f, 0.83f };
            BL(bx2, sy, front - 1.28f, 1.5f, 1.4f, 0.1f, glyphC, MAT_FLAT);         // trade glyph
        }
    };

    // ---- extra palette + kit for the upscaled port ----
    const float ochre[3]  = { 0.86f, 0.72f, 0.48f };   // pale ochre daub
    const float brickW[3] = { 0.76f, 0.46f, 0.37f };   // brick-red daub
    const float tealW[3]  = { 0.55f, 0.71f, 0.71f };   // colonial teal
    const float lemonW[3] = { 0.88f, 0.81f, 0.54f };   // pale lemon
    const float seagrn[3] = { 0.70f, 0.80f, 0.73f };   // pale sea-green daub
    const float thatch[3] = { 0.60f, 0.50f, 0.33f };   // palm thatch
    const float tarred[3] = { 0.31f, 0.28f, 0.25f };   // tarred industry timber
    const float ironC[3]  = { 0.20f, 0.20f, 0.22f };   // black iron (cannon)
    const float flagC[3]  = { 0.12f, 0.12f, 0.14f };   // the black colours
    const float cobble[3] = { 0.60f, 0.54f, 0.44f };   // warm cobbled/dirt street (less grey)
    const float wallStone[3] = { 0.56f, 0.54f, 0.50f };// fort/curtain stone

    auto retWall = [&](float x0, float x1, float z, float topY) {           // capstoned retaining wall
        const float cx = (x0 + x1) * 0.5f, w = x1 - x0;
        setShelf(cx, z);
        BL(cx, topY * 0.5f - 2.0f, z, w, topY + 4.0f, 1.7f, wallStone, MAT_STONE);
        BL(cx, topY + 0.25f, z, w + 0.5f, 0.5f, 2.2f, sillC, MAT_STONE);
    };
    auto stairUp = [&](float cx, float cz, float w, float dep, float rise) { // external stone stair up-slope
        setShelf(cx, cz);
        const int n = std::max(3, int(rise / 0.8f));
        for (int i = 0; i < n; ++i) { const float t = (i + 0.5f) / n;
            BL(cx, rise * t - 0.4f, cz + dep * t, w, 0.9f, dep / n + 0.6f, cobble, MAT_STONE); }
    };
    auto cannon = [&](float cx, float cz, float dir) {                       // cannon on a carriage (uses curLift)
        BL(cx, 1.2f, cz, 1.4f, 1.1f, 1.6f, ironC, MAT_FLAT);
        BL(cx - 0.75f, 0.65f, cz, 0.26f, 1.3f, 1.3f, wood, MAT_FLAT);
        BL(cx + 0.75f, 0.65f, cz, 0.26f, 1.3f, 1.3f, wood, MAT_FLAT);
        BL(cx, 1.9f, cz + dir * 1.2f, 0.66f, 0.66f, 3.2f, ironC, MAT_FLAT);   // barrel over the water
    };
    auto shotPile = [&](float cx, float cz) {                                // pyramid of shot (uses curLift)
        BL(cx, 0.35f, cz, 1.6f, 0.5f, 1.6f, ironC, MAT_FLAT);
        BL(cx, 0.85f, cz, 1.0f, 0.5f, 1.0f, ironC, MAT_FLAT);
        BL(cx, 1.3f, cz, 0.5f, 0.5f, 0.5f, ironC, MAT_FLAT);
    };
    auto flagstaff = [&](float cx, float cz, float h, const float* fc) {
        setShelf(cx, cz);
        BL(cx, h * 0.5f, cz, 0.28f, h, 0.28f, darkW, MAT_FLAT);
        BL(cx + 1.4f, h - 1.7f, cz, 2.6f, 1.7f, 0.1f, fc, MAT_FLAT);
    };
    auto verandah = [&](float cx, float front, float w, float storyH, int tiers) { // colonial gallery
        for (int t = 0; t < tiers; ++t) { const float y = storyH * (t + 1);
            BL(cx, y - 0.25f, front - 1.4f, w, 0.35f, 2.8f, plank, 1.0f);
            BL(cx, y + 0.9f, front - 2.7f, w, 1.2f, 0.16f, darkW, MAT_FLAT);
            const int np = std::max(3, int(w / 3.2f));
            for (int i = 0; i <= np; ++i) BL(cx - w * 0.5f + w * i / np, y + 1.1f, front - 2.7f, 0.28f, 2.3f, 0.28f, plank, MAT_FLAT); }
        BL(cx, storyH * tiers + 1.1f, front - 1.2f, w + 0.4f, 0.3f, 2.2f, darkRoof, MAT_FLAT); // shade eave (tight)
    };
    auto arcade = [&](float cx, float front, float w, float y, int n) {     // ground colonnade (loggia)
        for (int i = 0; i <= n; ++i) BL(cx - w * 0.5f + w * i / n, y * 0.5f, front, 0.9f, y, 1.0f, stone, MAT_STONE);
        BL(cx, y, front, w + 0.6f, 0.8f, 1.3f, sillC, MAT_STONE);
    };
    auto hoistBeam = [&](float cx, float front, float y) {                  // warehouse loading hoist
        BL(cx, y, front - 1.7f, 0.4f, 0.4f, 3.6f, beam, MAT_FLAT);
        BL(cx, y - 0.5f, front - 3.2f, 0.35f, 0.9f, 0.35f, ironC, MAT_FLAT);
        BL(cx, y - 2.1f, front - 3.2f, 0.14f, 3.0f, 0.14f, darkW, MAT_FLAT);
        BL(cx, y - 3.7f, front - 3.2f, 1.3f, 1.1f, 1.1f, crate, 1.0f);
    };
    auto wellAt = [&](float cx, float cz) {
        setShelf(cx, cz);
        BL(cx, 1.1f, cz, 3.0f, 2.4f, 3.0f, stone, MAT_STONE);
        BL(cx - 1.2f, 3.7f, cz, 0.3f, 3.2f, 0.3f, wood, MAT_FLAT);
        BL(cx + 1.2f, 3.7f, cz, 0.3f, 3.2f, 0.3f, wood, MAT_FLAT);
        BL(cx, 5.1f, cz, 4.0f, 0.4f, 2.0f, redRoof, MAT_FLAT);
    };
    auto marketCross = [&](float cx, float cz) {
        setShelf(cx, cz);
        BL(cx, 0.6f, cz, 3.4f, 1.2f, 3.4f, sillC, MAT_STONE);
        BL(cx, 1.7f, cz, 2.2f, 1.0f, 2.2f, sillC, MAT_STONE);
        BL(cx, 4.2f, cz, 0.5f, 5.0f, 0.5f, stone, MAT_STONE);
        BL(cx, 6.8f, cz, 1.8f, 0.5f, 0.5f, stone, MAT_STONE);
    };
    // Ground props (small, sit on the terrain via per-box lift B).
    auto crateAt = [&](float cx, float cz, float s) { B(cx, s * 0.5f, cz, s, s, s, crate[0], crate[1], crate[2], 1.0f); };
    auto ropeCoil = [&](float cx, float cz) { B(cx, 0.5f, cz, 1.6f, 0.6f, 1.6f, wood[0], wood[1], wood[2], MAT_FLAT); B(cx, 1.0f, cz, 1.1f, 0.5f, 1.1f, wood[0], wood[1], wood[2], MAT_FLAT); };
    auto tarBarrel = [&](float cx, float cz) { B(cx, 0.9f, cz, 1.3f, 1.8f, 1.3f, tarred[0], tarred[1], tarred[2], 1.0f); };
    auto dryRack = [&](float cx, float cz, float w) {
        B(cx - w * 0.5f, 1.4f, cz, 0.2f, 2.8f, 0.2f, wood[0], wood[1], wood[2], MAT_FLAT);
        B(cx + w * 0.5f, 1.4f, cz, 0.2f, 2.8f, 0.2f, wood[0], wood[1], wood[2], MAT_FLAT);
        B(cx, 2.6f, cz, w, 0.15f, 0.15f, wood[0], wood[1], wood[2], MAT_FLAT);
        for (int i = -1; i <= 1; ++i) B(cx + i * w * 0.3f, 1.9f, cz, 0.5f, 1.0f, 0.1f, 0.72f, 0.72f, 0.66f, MAT_FLAT);
    };
    // FORT San Cristóbal: keep + angled bastions + crenellated curtain + guns + flag.
    auto fort = [&](float cx, float cz) {
        setShelf(cx, cz);
        const float R = 14.0f, wt = 9.0f;
        BL(cx, -1.5f, cz, 2 * R + 4, 5.0f, 2 * R + 4, wallStone, MAT_STONE); // platform
        auto curtain = [&](float mx, float mz, float lx, float lz) {
            BL(mx, wt * 0.5f, mz, lx, wt, lz, wallStone, MAT_STONE);
            const bool ax = lx > lz; const int n = int((ax ? lx : lz) / 2.6f);
            for (int i = 0; i <= n; i += 2) { const float t = -0.5f + i / float(n);
                BL(mx + (ax ? t * lx : 0.0f), wt + 0.9f, mz + (ax ? 0.0f : t * lz), ax ? 1.4f : lz + 0.2f, 1.5f, ax ? lz + 0.2f : 1.4f, wallStone, MAT_STONE); }
        };
        curtain(cx, cz - R, 2 * R, 2.2f); curtain(cx, cz + R, 2 * R, 2.2f);
        curtain(cx - R, cz, 2.2f, 2 * R); curtain(cx + R, cz, 2.2f, 2 * R);
        for (int sx = -1; sx <= 1; sx += 2) for (int sz = -1; sz <= 1; sz += 2)
            BLr(cx + sx * R, wt * 0.5f, cz + sz * R, 6.0f, wt + 2.0f, 6.0f, 0, 0.785f, 0, wallStone, MAT_STONE); // diamond bastions
        BL(cx, 9.0f, cz + 2, 13, 20, 12, wallStone, MAT_STONE);            // keep
        BL(cx, 19.6f, cz + 2, 14, 1.6f, 13, darkRoof, MAT_FLAT);
        for (int i = -2; i <= 2; ++i) cannon(cx + i * 4.5f, cz - R + 1.6f, -1); // seaward guns
        BL(cx, 4.0f, cz + R, 3.2f, 8.0f, 2.6f, darkW, MAT_FLAT);           // landward gate
        flagstaff(cx, cz + 2, 21, flagC);
    };
    // SEA BASTION water battery on the west headland.
    auto battery = [&](float cx, float cz) {
        setShelf(cx, cz);
        BL(cx, 1.0f, cz, 24, 4.0f, 14, wallStone, MAT_STONE);             // gun platform
        for (int i = -4; i <= 4; i += 2) BL(cx + i * 2.6f, 4.2f, cz - 6.5f, 2.2f, 2.4f, 1.6f, wallStone, MAT_STONE); // merlons
        for (int i = -2; i <= 2; ++i) { cannon(cx + i * 5.0f, cz - 5.0f, -1); shotPile(cx + i * 5.0f + 2.3f, cz - 2.6f); }
        flagstaff(cx - 10, cz + 3, 16, redRoof);
    };

    // ===================== CAYO PERDIDO — the port town =====================
    // A tiered Caribbean free-port climbing the south hill: working quay -> Harbour
    // Street -> Grand Stair spine -> plaza -> church -> hilltop fort, defended from
    // the west point. Every building fronts a street; the skyline stacks three
    // landmarks (customs tower LOW, church MID, fort HIGH) on the central sightline.
    auto spire = [&](float cx, float cz, float w, float baseY, float rise, const float* rc) {
        const float half = w * 0.5f + 0.3f, sl = std::sqrt(half * half + rise * rise), ang = std::atan2(rise, half);
        const float py = baseY + rise * 0.5f;
        BLr(cx - half * 0.5f, py, cz, sl, 0.4f, w + 0.6f, 0, 0, -ang, rc, MAT_FLAT); // 4 panels to an apex (was inverted funnel)
        BLr(cx + half * 0.5f, py, cz, sl, 0.4f, w + 0.6f, 0, 0,  ang, rc, MAT_FLAT);
        BLr(cx, py, cz - half * 0.5f, w + 0.6f, 0.4f, sl,  ang, 0, 0, rc, MAT_FLAT);
        BLr(cx, py, cz + half * 0.5f, w + 0.6f, 0.4f, sl, -ang, 0, 0, rc, MAT_FLAT);
    };

    // Wall-audit character: a person standing in the town for scale, and to walk
    // around and inside the audited building.
    if (s_charOn) {
        const float cgh = std::max(0.0f, island_gpu::heightAt(s_charX, s_charZ));
        ship_mesh::renderCharacter(viewId, relX + s_charX, cgh + s_charY, relZ + s_charZ, s_charH, 0.0f);
    }

    // AUDIT MODE: render one isolated building on open ground (nothing else in the
    // way), so every wall can be inspected head-on along its whole length.
    if (s_auditOnly) {
        house(0, 0, 14, 12, 2, 4.0f, cream, redRoof, 1.0f, 0, 3, nullptr, false, false);
        return;
    }

    // ---- GROUND: paved terraces so the town steps up the hill on stone, not bare
    // grass. Cobble bands run wide along the contours (narrow in z) so each rides
    // its shelf flat; retaining walls hold the terrace edges; the Grand Stair spine
    // and Harbour Street tie it together. ----
    // Cobble stays well INLAND (narrower than the buildings' span) so no flat slab
    // overhangs the coast — the sloped dirt-toned terrain shows between/around the
    // buildings, so the town sits on the island's landform, not a floating tray.
    B(-4, -0.5f, -30, 76, 2.4f, 7, cobble[0], cobble[1], cobble[2], MAT_STONE);   // Harbour Street
    B(-4, -0.7f, -23, 72, 2.6f, 11, cobble[0], cobble[1], cobble[2], MAT_STONE);  // waterfront apron (inland)
    B(-2, -0.3f, -22, 9, 2.8f, 14, cobble[0], cobble[1], cobble[2], MAT_STONE);   // Grand Stair base run
    stairUp(-2, -16, 9, 24, 11);                                                  // the climbing stair
    retWall(-32, 14, -15, 4.5f);                                                  // plaza retaining wall
    B(-8, -0.2f, -9, 54, 3.0f, 13, cobble[0], cobble[1], cobble[2], MAT_STONE);   // plaza deck
    retWall(-32, 18, 3, 6.0f);                                                    // terrace wall 1
    B(-6, 0.4f, 4, 54, 3.0f, 12, cobble[0], cobble[1], cobble[2], MAT_STONE);     // trades terrace
    retWall(-28, 18, 13, 7.0f);                                                   // terrace wall 2
    B(-2, 1.0f, 13, 48, 3.0f, 11, cobble[0], cobble[1], cobble[2], MAT_STONE);    // residential terrace
    lamp(-34, -30); lamp(-16, -30); lamp(2, -30); lamp(22, -30); lamp(-2, -12);

    // ---- QUAY-FRONT BATTERY + cargo: the waterfront defence + working life the
    // player sees first. A low crenellated rampart along the seaward quay edge
    // carrying a row of cannon over the harbour, with shot, capstans and cargo. ----
    setShelf(0, -39);
    for (int i = -3; i <= 3; ++i) {
        cannon(3 + i * 8.5f, -41.0f, -1);                                          // gun on the quay edge, barrel over the water
        shotPile(7 + i * 8.5f, -39.5f);
        BL(7.25f + i * 8.5f, 3.0f, -42.0f, 3.2f, 3.4f, 1.5f, wallStone, MAT_STONE); // merlon between the guns (embrasure)
    }
    flagstaff(-28, -41, 14, flagC);                                                                    // the black colours over the battery
    setShelf(-26, -38); BL(-26, 1.7f, -38, 1.8f, 2.2f, 1.8f, darkW, MAT_FLAT); BL(-26, 2.9f, -38, 2.4f, 0.5f, 2.4f, darkW, MAT_FLAT); // capstan
    setShelf(24, -38); BL(24, 1.7f, -38, 1.8f, 2.2f, 1.8f, darkW, MAT_FLAT); BL(24, 2.9f, -38, 2.4f, 0.5f, 2.4f, darkW, MAT_FLAT);   // capstan
    crateAt(-14, -38, 2.4f); crateAt(-11, -39, 2.0f); crateAt(14, -38, 2.4f); ropeCoil(18, -39); ropeCoil(-18, -39); barrelAt(-8, -39);

    // ---- BAND A: HARBOURFRONT (west -> east) — six distinct blocks with clear
    // ~3-4m street gaps, each its own colour, tall fronts on Harbour Street. ----
    // FORGE / gunsmith — grey stone, slate.
    house(-42, -25, 12, 9, 2, 4.0f, smithyW, slate, MAT_STONE, 2, 3, red, false, false, shutDark);
    chimneyAt(-46, -22, 6.0f, 13.0f, true);
    BL(-46, 13.6f, -22, 1.1f, 1.3f, 1.1f, glow, MAT_FLAT);
    BL(-38, 1.4f, -30.5f, 2.2f, 1.7f, 1.5f, ironC, MAT_FLAT);     // anvil
    cannon(-35, -30, -1); shotPile(-32, -31);
    // KING'S BONDED WAREHOUSE — tall dark tarred-timber, hoist beam + cargo.
    house(-26, -24, 13, 11, 3, 4.0f, tarred, darkRoof, 1.0f, 2, 3, nullptr, false, false, shutDark);
    // Tall loading doors: timber surround + two leaves with a centre stile and iron
    // straps, so it reads as cargo doors rather than a flat black hole in the wall.
    BL(-26, 8.5f, -30.15f, 5.6f, 6.6f, 0.30f, beam, MAT_FLAT);   // surround
    BL(-26, 8.5f, -30.32f, 5.0f, 6.0f, 0.22f, wood, 1.0f);       // door leaves (planked)
    BL(-26, 8.5f, -30.46f, 0.22f, 6.0f, 0.10f, beam, MAT_FLAT);  // centre stile
    BL(-26, 10.6f, -30.46f, 4.6f, 0.18f, 0.10f, ironC, MAT_FLAT); // iron straps
    BL(-26, 6.4f, -30.46f, 4.6f, 0.18f, 0.10f, ironC, MAT_FLAT);
    crateAt(-20, -31, 2.4f); crateAt(-31, -31, 2.4f); tarBarrel(-33, -31.5f);
    // TRADING POST / general store — ochre, terracotta, awning.
    house(-11, -25, 11, 9, 2, 4.0f, ochre, redRoof, 1.0f, 0, 3, green, false, false, shutGreen);
    crateAt(-6, -31, 1.8f); barrelAt(-16, -31);
    // CUSTOM HOUSE — white civic, arcade loggia, LOOKOUT TOWER (LOW skyline tier).
    house(5, -25, 13, 10, 3, 4.0f, cream, slate, MAT_STONE, 0, 3, nullptr, false, false);
    arcade(5, -30.0f, 11, 3.6f, 4);
    setShelf(5, -25);
    BL(5, 15.5f, -23, 6, 7, 6, cream, MAT_STONE);                // lookout tower
    spire(5, -23, 6.5f, 19.0f, 4.0f, slate);
    flagstaff(5, -20, 25, flagC);
    // THE CHANDLERY — teal timber, hoist beam, rope + tar spilling out.
    house(20, -25, 10, 9, 2, 4.0f, tealW, redRoof, 1.0f, 0, 2, wood, false, false, shutDark);
    ropeCoil(24, -31); tarBarrel(16, -31); crateAt(23, -32, 1.8f);
    // THE SALT KRAKEN TAVERN — biggest block: terracotta-red timber-frame, two-tier
    // gallery, dormers, two chimneys, amber windows.
    house(36, -24, 15, 12, 3, 4.0f, tavW, darkRoof, 1.0f, 1, 3, amberC, false, true, shutDark);
    verandah(36, -30.0f, 14, 4.0f, 2);
    setShelf(36, -24);
    chimneyAt(31, -20, 12.0f, 16.5f, true); chimneyAt(41, -20, 12.0f, 16.5f, true);
    barrelAt(45, -31); crateAt(43, -32, 1.6f);

    // ---- BAND B: PLAZA DE ARMAS (one terrace up) — church + counting house frame an
    // open square with the market hall, well and cross; wide gaps between blocks. ----
    // CHURCH OF ST. ELMO — white nave + buttresses + tall campanile (MID skyline tier).
    setShelf(-3, -6);
    BL(-3, 4.0f, -6, 13, 11, 9, cream, MAT_STONE);               // nave (kept compact in z)
    gable(-3, -6, 13, 9, 11, 5.2f, redRoof, cream, MAT_STONE);
    for (int i = -1; i <= 1; i += 2) BL(-3 + i * 6.7f, 3.2f, -6, 1.4f, 8, 7.0f, cream, MAT_STONE); // buttressed flanks
    windowOn(-9.6f, 6, -6, -1, 0, 1.6f, 3.2f, false); windowOn(3.6f, 6, -6, 1, 0, 1.6f, 3.2f, false);
    BL(-3, 13, -11.5f, 5.6f, 26, 5.6f, cream, MAT_STONE);        // slender campanile — the dominant landmark
    BL(-3, 26.5f, -11.5f, 6.6f, 3.6f, 6.6f, tailorW, MAT_STONE); // belfry
    windowOn(-3, 26.5f, -14.85f, 0, -1, 1.8f, 2.6f, false);      // belfry arch
    BL(-3, 26.5f, -12.0f, 1.3f, 1.7f, 0.4f, darkW, MAT_FLAT);    // the bell
    spire(-3, -11.5f, 6.2f, 28.3f, 7.5f, slate);                 // slate spire (contrasts the terracotta roofs)
    BL(-3, 36.5f, -11.5f, 0.3f, 2.6f, 0.3f, darkW, MAT_FLAT); BL(-3, 37.3f, -11.5f, 1.7f, 0.3f, 0.3f, darkW, MAT_FLAT); // cross finial
    // MERCHANTS' ARCADE & COUNTING HOUSE — brick-red, ground colonnade, west of the square.
    house(-24, -7, 12, 9, 2, 4.0f, brickW, slate, MAT_STONE, 0, 3, cloth, false, false, shutDark);
    arcade(-24, -11.7f, 10, 3.6f, 4);
    // COVERED MARKET HALL (open, on piers) east of the square + well + cross out front.
    setShelf(15, -9);
    for (int i = -1; i <= 1; ++i) for (int j = 0; j <= 1; ++j) BL(15 + i * 4.5f, 3.0f, -9 + j * 3.6f, 0.8f, 6, 0.8f, wood, MAT_FLAT);
    gable(15, -9, 12, 8, 6.0f, 2.6f, thatch, wood, 1.0f);
    stall(11, -10, cloth); stall(19, -10, green);
    wellAt(-10, -9); marketCross(-1, -9);
    B(6, 1.4f, -10, 0.3f, 3.0f, 0.3f, wood[0], wood[1], wood[2], MAT_FLAT); B(6, 3.2f, -10, 2.2f, 1.6f, 0.2f, cream[0], cream[1], cream[2], MAT_FLAT); // notice board

    // ---- RUM ROW (east warren, behind the tavern) ----
    // THE DROWNED MAN boarding inn — tall timber-frame, jettied.
    house(26, -12, 9, 8, 3, 4.0f, tailorW, darkRoof, 1.0f, 1, 3, amberC, true, false);
    // THE FENCE — dark, crooked, barred, no sign, one red lantern.
    house(37, -11, 8, 7, 2, 3.6f, fenceWall, darkRoof, 1.0f, 2, 2, nullptr, false, false);
    BL(37, 3.0f, -14.6f, 1.5f, 1.5f, 0.2f, darkW, MAT_FLAT);
    for (int b = -1; b <= 1; ++b) BL(37 + b * 0.5f, 3.0f, -14.75f, 0.14f, 1.5f, 0.14f, metal, MAT_FLAT); // bars
    setShelf(37, -11); BL(41, 2.6f, -14.5f, 0.6f, 0.6f, 0.6f, red, MAT_FLAT);   // red lantern

    // ---- BAND C: TRADES TERRACE (behind retaining wall 1) — signed craft shops ----
    house(-28, 6, 8, 7, 2, 4.2f, seagrn, redRoof,  1.0f, 0, 2, cloth, true, false);  // APOTHECARY
    house(-10, 6, 10, 7, 2, 4.0f, lemonW, darkRoof, 1.0f, 0, 3, amberC, false, false, shutGreen); // OUTFITTER
    house(12, 6, 9, 7, 2, 3.8f, ochre, redRoof, 1.0f, 0, 2, amberC, false, false, shutRed);    // BAKEHOUSE
    chimneyAt(16, 8, 8.0f, 13.0f, true);
    setShelf(12, 6); BL(16, 3.4f, 2.6f, 3.0f, 3.0f, 2.0f, stone, MAT_STONE); BL(16, 3.4f, 1.5f, 1.4f, 1.4f, 0.4f, glow, MAT_FLAT); // oven
    // COOPER open work-shed + casks + saw-pit (west end of the terrace).
    setShelf(-44, 6);
    for (int i = -1; i <= 1; ++i) BL(-44 + i * 3.5f, 2.4f, 7.5f, 0.6f, 5, 0.6f, wood, MAT_FLAT);
    BLr(-44, 5.2f, 6.0f, 9.0f, 0.4f, 6.5f, -0.35f, 0, 0, darkRoof, MAT_FLAT);
    for (int i = 0; i < 3; ++i) barrelAt(-47 + i * 1.8f, 8.5f);

    // ---- BAND D: RESIDENTIAL TERRACE (behind retaining wall 2) — pastel row ----
    house(-28, 16, 9, 7, 2, 3.8f, brickW, redRoof,  1.0f, 0, 3, nullptr, true, false, shutGreen);
    house(-16, 16, 9, 7, 2, 4.0f, tealW,  darkRoof, 1.0f, 1, 3, nullptr, true, false, shutRed);
    house(-4,  16, 9, 7, 2, 3.8f, ochre,  redRoof,  1.0f, 0, 3, nullptr, true, false, shutTeal);
    // CAPTAIN'S VILLA — fine white + full verandah, upper terrace east.
    house(14, 15, 12, 10, 2, 4.2f, tailorW, redRoof, MAT_STONE, 0, 3, nullptr, true, false, shutTeal);
    verandah(14, 10.0f, 11, 4.2f, 1);
    setShelf(14, 15); flagstaff(21, 16, 12, flagC);

    // ---- BAND E: GOVERNOR'S HILL / THE CITADEL (summit) ----
    fort(-8, 26);
    // GOVERNOR'S RESIDENCE — white mansion + two-tier verandah + cupola, beside the fort.
    house(12, 22, 14, 11, 2, 4.4f, tailorW, redRoof, MAT_STONE, 0, 3, nullptr, true, false);
    verandah(12, 16.5f, 13, 4.4f, 2);
    setShelf(12, 22);
    BL(12, 11.0f, 22, 3.5f, 2.5f, 3.5f, tailorW, MAT_STONE); spire(12, 22, 3.8f, 13.0f, 2.2f, slate); // cupola
    flagstaff(20, 23, 16, flagC);
    cannon(6, 17.5f, -1); cannon(18, 17.5f, -1);                 // trophy cannon
    // POWDER MAGAZINE — squat windowless stone, slab roof, set apart west.
    setShelf(-24, 24);
    BL(-24, 3.0f, 24, 9, 7, 8, wallStone, MAT_STONE);
    BL(-24, 6.8f, 24, 10, 1.4f, 9, stone, MAT_STONE);
    for (int i = -1; i <= 1; i += 2) BLr(-24 + i * 5.0f, 3.0f, 24, 2.0f, 6.5f, 8.0f, 0, 0, i * 0.18f, wallStone, MAT_STONE);

    // ---- WEST POINT: sea battery + fishermen's cottages ----
    battery(-54, -30);
    house(-48, 5, 7, 6, 1, 4.0f, fenceWall, thatch, 1.0f, 2, 2, nullptr, false, false);
    house(-40, 4, 6, 6, 1, 4.0f, tarred,   thatch, 1.0f, 2, 2, nullptr, false, false);
    dryRack(-46, 0, 4); dryRack(-40, -1, 4); B(-50, 0.6f, -2, 3.0f, 1.2f, 1.4f, wood[0], wood[1], wood[2], 1.0f); // skiff

    // ---- SHIPYARD edge: a long low SAIL LOFT & ROPEWALK (east, by the hall) ----
    house(48, -16, 9, 8, 2, 4.0f, tarred, darkRoof, 1.0f, 2, 2, wood, false, false);
    setShelf(58, -14);
    BL(58, 3.0f, -14, 20, 6, 6, tarred, 1.0f);                   // long ropewalk shed
    BLr(58, 6.6f, -14, 21, 0.4f, 5.0f, -0.28f, 0, 0, darkRoof, MAT_FLAT);
    BLr(58, 6.6f, -14, 21, 0.4f, 5.0f, 0.28f, 0, 0, darkRoof, MAT_FLAT);

    // --- Palm trees: the Caribbean signature — a leaning trunk and a drooping
    // green crown, clustered on the sandy shore and dotting the green slopes. ---
    auto palm = [&](float cx, float cz, float ht) {
        const float tr[3] = { 0.56f, 0.43f, 0.27f };
        const float fr[3] = { 0.18f, 0.48f, 0.20f };
        B(cx, ht * 0.35f, cz, 0.55f, ht * 0.72f, 0.55f, tr[0], tr[1], tr[2], 1.0f);      // lower trunk
        B(cx + 0.5f, ht * 0.82f, cz + 0.25f, 0.5f, ht * 0.5f, 0.5f, tr[0], tr[1], tr[2], 1.0f); // leaning upper trunk
        const float tx = cx + 0.8f, ty = ht + 0.2f, tz = cz + 0.4f;
        B(tx, ty, tz, 1.5f, 0.8f, 1.5f, fr[0], fr[1], fr[2], MAT_FLAT);                   // crown core
        const float fo = 2.7f, fy = ty - 0.6f;
        B(tx + fo, fy, tz, 3.6f, 0.28f, 1.2f, fr[0], fr[1], fr[2], MAT_FLAT);             // 4 long fronds
        B(tx - fo, fy, tz, 3.6f, 0.28f, 1.2f, fr[0], fr[1], fr[2], MAT_FLAT);
        B(tx, fy, tz + fo, 1.2f, 0.28f, 3.6f, fr[0], fr[1], fr[2], MAT_FLAT);
        B(tx, fy, tz - fo, 1.2f, 0.28f, 3.6f, fr[0], fr[1], fr[2], MAT_FLAT);
        const float fd = 1.7f, fyd = fy - 0.4f;
        B(tx + fd, fyd, tz + fd, 2.4f, 0.26f, 2.4f, fr[0], fr[1], fr[2], MAT_FLAT);       // 4 diagonal fronds (drooping)
        B(tx - fd, fyd, tz - fd, 2.4f, 0.26f, 2.4f, fr[0], fr[1], fr[2], MAT_FLAT);
        B(tx + fd, fyd, tz - fd, 2.4f, 0.26f, 2.4f, fr[0], fr[1], fr[2], MAT_FLAT);
        B(tx - fd, fyd, tz + fd, 2.4f, 0.26f, 2.4f, fr[0], fr[1], fr[2], MAT_FLAT);
    };
    // Shore-line palms flanking the town, and a grove dotting the green slopes.
    palm(-56, -28, 10); palm(-52, -36, 9);  palm(54, -30, 11); palm(58, -22, 9);
    palm(-44, -8, 12);  palm(46, -6, 10);
    palm(-40, 12, 11);  palm(-22, 22, 10);  palm(-6, 30, 12);  palm(16, 24, 11);
    palm(34, 18, 10);   palm(44, 26, 9);    palm(8, 34, 11);    palm(-30, 30, 10);

    // --- Shipyard (large, on the east shore, kept inside the shore line) ---
    B(38, 7, 6, 36, 22, 32, timber[0], timber[1], timber[2]);      // great ship hall (base sunk to the ground)
    gableRoof(38, 6, 36, 32, 18.0f, 9.0f, roof, timber, 1.0f);     // big pitched roof over the hall
    B(24, 1.4f, -46, 11, 0.8f, 34, wood[0], wood[1], wood[2]);     // slipway 1
    B(40, 1.4f, -44, 11, 0.8f, 32, wood[0], wood[1], wood[2]);     // slipway 2
    B(54, 1.4f, -40, 9, 0.8f, 28, wood[0], wood[1], wood[2]);      // slipway 3
    B(30, 9, -48, 2, 18, 2, metal[0], metal[1], metal[2], MAT_FLAT);  // gantry crane 1
    B(30, 17.5f, -40, 2, 2, 20, metal[0], metal[1], metal[2], MAT_FLAT);
    B(46, 9, -46, 2, 18, 2, metal[0], metal[1], metal[2], MAT_FLAT);  // gantry crane 2
    B(46, 17.5f, -38, 2, 2, 20, metal[0], metal[1], metal[2], MAT_FLAT);
    // scaffolding frame around slipway 1
    B(19, 4, -38, 1, 8, 1, wood[0], wood[1], wood[2]);
    B(29, 4, -38, 1, 8, 1, wood[0], wood[1], wood[2]);
    B(19, 4, -54, 1, 8, 1, wood[0], wood[1], wood[2]);
    B(29, 4, -54, 1, 8, 1, wood[0], wood[1], wood[2]);
    B(24, 8, -38, 11, 0.7f, 0.7f, wood[0], wood[1], wood[2]);
    B(24, 8, -54, 11, 0.7f, 0.7f, wood[0], wood[1], wood[2]);
    B(46, 2, -14, 4, 2, 16, wood[0], wood[1], wood[2]);            // timber stacks
    B(52, 2, -14, 4, 2, 16, timber[0], timber[1], timber[2]);
}

float deckStandHeight(const sea::Ship& ship) {
    // Matches renderBuildScene: keel rests on blocks (top 2.9) -> hull origin at
    // +depth*0.55 -> deck piece at +depth*0.03 -> stand on its top.
    const float depth = float(ship.bounds.depth);
    return 2.9f + depth * 0.55f + depth * 0.03f + 0.06f;
}

void renderBuildScene(uint16_t viewId, const sea::Ship& ship,
                      const std::vector<sea::Wave>& waves, float timeSec,
                      float orbitAngle, int width, int height,
                      bool walk, float cx, float cy, float cz, float cheading, float walkPhase) {
    // Camera: orbit the stocks to look the hull over, OR (walk mode) a third-person
    // camera following the character on foot in the yard.
    const bx::Vec3 up = { 0.0f, 1.0f, 0.0f };
    bx::Vec3 eye = { 0.0f, 0.0f, 0.0f }, at = { 0.0f, 0.0f, 0.0f };
    if (walk) {
        const float fx = bx::sin(cheading), fz = bx::cos(cheading);
        eye = { cx - fx * 7.0f, cy + 4.2f, cz - fz * 7.0f };
        at  = { cx + fx * 3.0f, cy + 1.4f, cz + fz * 3.0f };
    } else {
        const float dist = 24.0f, camH = 12.0f;
        eye = { bx::sin(orbitAngle) * dist, camH, bx::cos(orbitAngle) * dist };
        at  = { 0.0f, 3.2f, 0.0f };
    }
    float view[16], proj[16];
    bx::mtxLookAt(view, eye, at, up);
    const bgfx::Caps* caps = bgfx::getCaps();
    const float aspect = float(width) / float(height > 0 ? height : 1);
    bx::mtxProj(proj, 55.0f, aspect, 0.1f, 600.0f, caps->homogeneousDepth);
    bgfx::setViewTransform(viewId, view, proj);
    bgfx::setViewRect(viewId, 0, 0, uint16_t(width), uint16_t(height));

    auto box = [&](float x, float y, float z, float sx, float sy, float sz,
                   float r, float g, float b, float mat = 1.0f) {
        ship_mesh::renderBoxSized(viewId, x, y, z, sx, sy, sz, r, g, b, mat);
    };
    const float FLAT = 0.0f;
    const float sand[3]  = { 0.82f, 0.74f, 0.52f };
    const float grass[3] = { 0.30f, 0.46f, 0.22f };
    const float dark[3]  = { 0.34f, 0.24f, 0.16f };
    const float wood[3]  = { 0.44f, 0.30f, 0.18f };
    const float timber[3]= { 0.52f, 0.40f, 0.27f };
    const float roof[3]  = { 0.38f, 0.22f, 0.15f };
    const float metal[3] = { 0.40f, 0.37f, 0.33f };

    // The sea, then the coastal yard: an island in the distance behind, a stone
    // dock the stocks sit on, a great ship hall + flanking gantry cranes behind
    // the berth (so the hull stays the clear foreground focus).
    sky_gpu::render(viewId, kSkyTop, kSkyHorizon);
    // Carve the sea out under the dock berth so the ground reads as land, not a
    // box on the water. Dock centre scene (0,6) + water offset (24,76) = world (24,82).
    water_gpu::render(viewId, waves, timeSec, eye.x, eye.y, eye.z, 24.0f, 76.0f,
                      24.0f, 82.0f, 30.0f);
    box(0.0f, 5.0f, 96.0f, 180.0f, 16.0f, 74.0f, grass[0], grass[1], grass[2], FLAT); // island (distance, deep base)
    box(0.0f, 0.0f, 58.0f, 200.0f, 5.0f, 20.0f, sand[0], sand[1], sand[2], FLAT);     // shore/beach: top ~2.5
    box(0.0f, -0.6f, 6.0f, 56.0f, 4.8f, 46.0f, 0.56f, 0.43f, 0.28f, 1.0f);            // planked dry-dock deck (timber, seams)
    box(0.0f, 10.0f, 40.0f, 48.0f, 20.0f, 22.0f, timber[0], timber[1], timber[2]); // great ship hall
    box(0.0f, 20.6f, 40.0f, 50.0f, 1.6f, 24.0f, roof[0], roof[1], roof[2]);
    for (int s = -1; s <= 1; s += 2) {                                            // gantry cranes (set wide so they frame, not block, the berth)
        box(float(s) * 23.0f, 10.0f, -2.0f, 1.8f, 20.0f, 1.8f, metal[0], metal[1], metal[2], FLAT);
        box(float(s) * 23.0f, 19.0f, 4.0f, 1.8f, 1.8f, 22.0f, metal[0], metal[1], metal[2], FLAT);
    }
    box(28.0f, 2.0f, 8.0f, 4.0f, 3.0f, 16.0f, wood[0], wood[1], wood[2]);          // timber stacks
    box(-28.0f, 2.0f, 8.0f, 4.0f, 3.0f, 16.0f, timber[0], timber[1], timber[2]);
    // A spar hoisted on the port crane: rope from the arm -> block (pulley) -> spar.
    box(-23.0f, 12.5f, 6.0f, 0.12f, 12.0f, 0.12f, dark[0], dark[1], dark[2], FLAT); // rope
    box(-23.0f, 6.2f, 6.0f, 0.5f, 0.7f, 0.5f, dark[0], dark[1], dark[2], FLAT);     // block
    box(-23.0f, 5.2f, 6.0f, 0.45f, 0.45f, 7.0f, wood[0], wood[1], wood[2]);        // suspended spar

    // The building stand: two ground ways, keel blocks along the spine, and a row
    // of shoring posts each side.
    const float len = float(ship.bounds.length);
    const float wid = float(ship.bounds.width);
    box(-0.9f, 1.1f, 0.0f, 0.8f, 1.0f, len * 0.95f, wood[0], wood[1], wood[2]);
    box( 0.9f, 1.1f, 0.0f, 0.8f, 1.0f, len * 0.95f, wood[0], wood[1], wood[2]);
    const int nb = 6;
    const float blockTop = 2.9f;
    for (int i = 0; i < nb; ++i) {
        const float z = -len * 0.42f + (len * 0.84f) * i / float(nb - 1);
        box(0.0f, 1.9f, z, 1.7f, 2.0f, 1.1f, timber[0], timber[1], timber[2]); // keel blocks (top at 2.9)
    }
    // Shoring cradle: a row of posts each side, tied together by a fore-and-aft
    // scaffold rail and topped by angled shores bearing against the hull, so the
    // berth reads as a ship under construction on the stocks.
    const int np = 5;
    for (int i = 0; i < np; ++i) {
        const float z = -len * 0.34f + (len * 0.68f) * i / float(np - 1);
        box(-wid * 0.66f, 2.7f, z, 0.45f, 4.6f, 0.45f, wood[0], wood[1], wood[2]);
        box( wid * 0.66f, 2.7f, z, 0.45f, 4.6f, 0.45f, wood[0], wood[1], wood[2]);
        // angled shore bracing inward toward the hull
        box(-wid * 0.5f, 4.4f, z, 0.35f, 0.35f, wid * 0.42f, timber[0], timber[1], timber[2]);
        box( wid * 0.5f, 4.4f, z, 0.35f, 0.35f, wid * 0.42f, timber[0], timber[1], timber[2]);
    }
    // Fore-and-aft scaffold rails tying the shore posts together.
    box(-wid * 0.66f, 4.9f, 0.0f, 0.28f, 0.28f, len * 0.7f, timber[0], timber[1], timber[2]);
    box( wid * 0.66f, 4.9f, 0.0f, 0.28f, 0.28f, len * 0.7f, timber[0], timber[1], timber[2]);
    // Ground ways running out toward the water (the slipway the hull launches down).
    box(-1.6f, 0.7f, -len * 0.2f, 0.9f, 0.6f, len * 1.35f, wood[0], wood[1], wood[2]);
    box( 1.6f, 0.7f, -len * 0.2f, 0.9f, 0.6f, len * 1.35f, wood[0], wood[1], wood[2]);

    // Gangplank: a stair of timber steps up to the deck at the stern.
    {
        const float deckY = deckStandHeight(ship);
        const float hl = len * 0.5f;
        const int steps = 6;
        for (int i = 0; i < steps; ++i) {
            const float t = i / float(steps - 1);
            const float sy = 1.0f + t * (deckY - 1.0f);      // top of this step
            const float sz = -hl - 3.4f + t * 3.4f;          // dock (-hl-3.4) up to deck edge (-hl)
            box(0.0f, sy * 0.5f, sz, 1.8f, sy, 1.1f, wood[0], wood[1], wood[2]);
        }
    }

    // The hull, keel resting on the blocks (with a contact shadow on the dock).
    sea::FloatPose bp;
    bp.heaveY = blockTop + float(ship.bounds.depth) * 0.55; // keel local y = -depth*0.55
    ship_mesh::render(viewId, ship, bp, 0.0f, 0.0f, 0.0f, timeSec, 0.0f, 0.0f);

    if (walk) ship_mesh::renderCharacter(viewId, cx, cy, cz, cheading, walkPhase);
}

} // namespace ship_view
