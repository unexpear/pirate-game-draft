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
    const bx::Vec3 eye = { -fwdX * dist, 9.0f, -fwdZ * dist };
    const bx::Vec3 at = { fwdX * 5.0f, -0.4f, fwdZ * 5.0f };
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

    // --- Landmass: a real procedural island (coloured heightfield), rising from
    // the sea with an irregular coastline, beach, meadow and rocky heights. It
    // occludes the ocean where it stands above the waves; the sea is also carved
    // out under it (water_gpu land cut) so nothing floats on a flat sheet. ---
    island_gpu::render(viewId, relX, relZ);

    // --- Port (south shore) ---
    B(-6, 1.2f, -40, 78, 1.4f, 6, stone[0], stone[1], stone[2], MAT_STONE);   // stone quay
    B(-26, 1.1f, -54, 4.5f, 0.8f, 30, wood[0], wood[1], wood[2]);  // pier 1
    B(-2, 1.1f, -56, 4.5f, 0.8f, 34, wood[0], wood[1], wood[2]);   // pier 2
    B(22, 1.1f, -52, 4.5f, 0.8f, 26, wood[0], wood[1], wood[2]);   // pier 3
    B(-34, 4.0f, -26, 16, 8, 13, timber[0], timber[1], timber[2]); // warehouse A
    B(-34, 8.4f, -26, 17, 1.2f, 14, roof[0], roof[1], roof[2]);
    B(-14, 4.5f, -24, 15, 9, 12, timber[0], timber[1], timber[2]); // warehouse B
    B(-14, 9.3f, -24, 16, 1.2f, 13, roof[0], roof[1], roof[2]);
    B(-50, 7, -22, 6, 14, 6, stone[0], stone[1], stone[2], MAT_STONE); // harbourmaster tower (on land, west of town)
    B(-50, 14.5f, -22, 7, 1.5f, 7, roof[0], roof[1], roof[2]);
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
    const float smithyW[3] = { 0.37f, 0.37f, 0.40f };  // grey stone smithy
    const float tailorW[3] = { 0.87f, 0.84f, 0.75f };  // bright whitewashed daub
    const float grocerW[3] = { 0.52f, 0.38f, 0.23f };  // warm brown timber
    const float tavW[3]    = { 0.56f, 0.24f, 0.20f };  // deep-red tavern
    const float fenceWall[3]= { 0.23f, 0.21f, 0.20f };  // black-market dark
    const float cottW[3]   = { 0.72f, 0.60f, 0.44f };  // cottage daub
    const float darkW[3]   = { 0.24f, 0.21f, 0.18f };  // doors / trim / brackets
    const float redRoof[3] = { 0.52f, 0.19f, 0.13f };  // red tile
    const float tealRoof[3]= { 0.22f, 0.40f, 0.42f };  // weathered teal
    const float goldRoof[3]= { 0.70f, 0.56f, 0.26f };  // golden thatch
    const float darkRoof[3]= { 0.20f, 0.18f, 0.21f };  // dark shingle
    const float slate[3]   = { 0.42f, 0.43f, 0.47f };  // slate
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
        B(cx, hgt * 0.5f, cz, w, hgt, d, wall[0], wall[1], wall[2], mat);
        B(cx, hgt + 0.35f, cz, w + 1.4f, 0.9f, d + 1.4f, rf[0], rf[1], rf[2], MAT_FLAT);
        B(cx, 1.2f, front - 0.05f, 1.6f, 2.4f, 0.25f, darkW[0], darkW[1], darkW[2], MAT_FLAT);
        if (sc) {
            const float bx2 = cx + w * 0.30f;
            B(bx2, hgt - 0.6f, front - 0.5f, 0.2f, 0.2f, 1.3f, darkW[0], darkW[1], darkW[2], MAT_FLAT); // bracket
            B(bx2, hgt - 1.0f, front - 1.1f, 2.6f, 1.7f, 0.22f, sc[0], sc[1], sc[2], MAT_FLAT);          // board
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

    // Waterfront shop row (facing the sea, south), west -> east — each distinct:
    // Blacksmith / WEAPON SHOP — grey stone, slate roof, chimney + forge glow + a
    // drifting smoke plume, and an anvil out front.
    building(-38, -14, 13, 7, 10, smithyW, slate, MAT_STONE, red);
    B(-43.0f, 6.5f, -14, 2.2f, 9, 2.2f, smithyW[0], smithyW[1], smithyW[2], MAT_STONE); // chimney
    B(-43.0f, 11.2f, -14, 1.0f, 1.2f, 1.0f, glow[0], glow[1], glow[2], MAT_FLAT);        // forge glow
    for (int s = 0; s < 4; ++s) B(-43.0f + s * 0.9f, 12.6f + s * 2.1f, -14 + s * 0.4f, 1.7f - s * 0.25f, 1.8f, 1.7f - s * 0.25f, smoke[0], smoke[1], smoke[2], MAT_FLAT); // smoke
    B(-34, 1.0f, -8.5f, 2.0f, 1.6f, 1.4f, darkW[0], darkW[1], darkW[2], MAT_FLAT);       // anvil
    // CLOTHING SHOP / tailor — bright whitewash, teal roof, a purple cloth awning.
    building(-20, -13, 11, 6, 9, tailorW, tealRoof, 1.0f, cloth);
    B(-20, 3.6f, -18.2f, 10.0f, 0.25f, 2.4f, cloth[0], cloth[1], cloth[2], MAT_FLAT);    // awning
    // GENERAL STORE — warm brown timber, golden thatch, barrels of goods out front.
    building(-2, -14, 13, 6.5f, 10, grocerW, goldRoof, 1.0f, green);
    barrelAt(5, -8.6f); barrelAt(6.7f, -7.8f); barrelAt(5.8f, -10.0f);
    // TAVERN — biggest, deep-red walls, dark shingle roof, lit windows + lanterns.
    building(18, -15, 16, 8.5f, 12, tavW, darkRoof, 1.0f, amberC);
    B(11.5f, 3.4f, -9.05f, 1.6f, 1.8f, 0.3f, amberC[0], amberC[1], amberC[2], MAT_FLAT); // lit window
    B(24.5f, 3.4f, -9.05f, 1.6f, 1.8f, 0.3f, amberC[0], amberC[1], amberC[2], MAT_FLAT);
    lamp(9.5f, -10); barrelAt(27, -8.5f); barrelAt(28.7f, -9.6f);
    // FENCE / black-market — small, dark, tucked east, barred window, no bright sign.
    building(37, -21, 9, 5, 8, fenceWall, darkRoof, MAT_FLAT, fenceWall);
    B(37, 3.0f, -25.2f, 1.4f, 1.4f, 0.2f, darkW[0], darkW[1], darkW[2], MAT_FLAT);       // barred window
    for (int b = -1; b <= 1; ++b) B(37 + b * 0.45f, 3.0f, -25.35f, 0.12f, 1.4f, 0.12f, metal[0], metal[1], metal[2], MAT_FLAT);

    // Market square behind the row: striped stalls + a stone well.
    stall(-14, -3, cloth);
    stall(2, -3, green);
    stall(-26, -1, red);
    B(-6, 1.1f, -2, 3.0f, 2.2f, 3.0f, stone[0], stone[1], stone[2], MAT_STONE);          // well
    B(-6, 3.4f, -2, 0.25f, 3.0f, 0.25f, wood[0], wood[1], wood[2], MAT_FLAT);
    B(-6, 4.9f, -2, 3.6f, 0.3f, 1.6f, redRoof[0], redRoof[1], redRoof[2], MAT_FLAT);
    // Cottages up the slope, distinct from the shops.
    building(-32, 2, 9, 5.5f, 8, cottW, redRoof, 1.0f, nullptr);
    building(30, 2, 10, 5.5f, 8, cottW, goldRoof, 1.0f, nullptr);

    // Fences around the square + a pen, and street lamps along the waterfront.
    fenceX(-40, 10, 4);
    fenceZ(-3, 4, 12);
    fenceX(18, 40, 4);
    lamp(-28, -20); lamp(-9, -20); lamp(9, -21); lamp(33, -13);

    // --- Shipyard (large, on the east shore, kept inside the shore line) ---
    B(38, 9, 6, 36, 18, 32, timber[0], timber[1], timber[2]);      // great ship hall
    B(38, 18.6f, 6, 38, 1.6f, 34, roof[0], roof[1], roof[2]);
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
