// Sea Trial — 3D scene view (Milestone 0+1). Public domain (Unlicense).
//
// Orchestrates the 3D scene: camera, GPU water surface, and the lit ship
// meshes. Everything now goes through the bgfx shader pipeline (debugdraw is
// gone).
#include "ship_view.h"

#include "ship_model.hpp"
#include "water_gpu.h"
#include "ship_mesh.h"

#include <bgfx/bgfx.h>
#include <bx/math.h>

namespace ship_view {

void init() {
    water_gpu::init();
    ship_mesh::init();
}

void shutdown() {
    ship_mesh::shutdown();
    water_gpu::shutdown();
}

void render(uint16_t viewId, const sea::Ship& ship, const std::vector<sea::Wave>& waves,
            const sea::FloatPose& pose, float timeSec, float heading,
            float worldX, float worldZ, float windDir, float sailFullness,
            int width, int height) {
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

    water_gpu::render(viewId, waves, timeSec, eye.x, eye.y, eye.z, worldX, worldZ);
    ship_mesh::render(viewId, ship, pose, heading, windDir, sailFullness, timeSec);
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
    auto B = [&](float cx, float cy, float cz, float sx, float sy, float sz,
                 float r, float g, float b) {
        ship_mesh::renderBoxSized(viewId, relX + cx, cy, relZ + cz, sx, sy, sz, r, g, b);
    };
    // Palette
    const float sand[3]  = { 0.82f, 0.74f, 0.52f };
    const float grass[3] = { 0.30f, 0.46f, 0.22f };
    const float grassD[3]= { 0.22f, 0.37f, 0.16f };
    const float rock[3]  = { 0.46f, 0.44f, 0.40f };
    const float wood[3]  = { 0.42f, 0.28f, 0.15f };
    const float timber[3]= { 0.52f, 0.40f, 0.27f };
    const float roof[3]  = { 0.35f, 0.20f, 0.14f };
    const float stone[3] = { 0.58f, 0.55f, 0.50f };
    const float white[3] = { 0.90f, 0.90f, 0.85f };
    const float red[3]   = { 0.70f, 0.16f, 0.13f };
    const float crate[3] = { 0.55f, 0.42f, 0.24f };
    const float metal[3] = { 0.25f, 0.22f, 0.20f };

    // --- Landmass (beach shelf, grass plateau, hill, rocky peak) ---
    B(0, 0.5f, -2, 130, 1.6f, 108, sand[0], sand[1], sand[2]);
    B(0, 3.0f, 8, 104, 6.4f, 78, grass[0], grass[1], grass[2]);
    B(10, 6.5f, 24, 54, 8, 44, grassD[0], grassD[1], grassD[2]);
    B(-14, 8.5f, 30, 26, 10, 24, rock[0], rock[1], rock[2]);

    // --- Port (south shore) ---
    B(-6, 1.2f, -40, 78, 1.4f, 6, stone[0], stone[1], stone[2]);   // stone quay
    B(-26, 1.1f, -54, 4.5f, 0.8f, 30, wood[0], wood[1], wood[2]);  // pier 1
    B(-2, 1.1f, -56, 4.5f, 0.8f, 34, wood[0], wood[1], wood[2]);   // pier 2
    B(22, 1.1f, -52, 4.5f, 0.8f, 26, wood[0], wood[1], wood[2]);   // pier 3
    B(-34, 4.0f, -26, 16, 8, 13, timber[0], timber[1], timber[2]); // warehouse A
    B(-34, 8.4f, -26, 17, 1.2f, 14, roof[0], roof[1], roof[2]);
    B(-14, 4.5f, -24, 15, 9, 12, timber[0], timber[1], timber[2]); // warehouse B
    B(-14, 9.3f, -24, 16, 1.2f, 13, roof[0], roof[1], roof[2]);
    B(-50, 7, -34, 6, 14, 6, stone[0], stone[1], stone[2]);        // harbourmaster tower
    B(-50, 14.5f, -34, 7, 1.5f, 7, roof[0], roof[1], roof[2]);
    B(40, 10, -62, 5, 20, 5, white[0], white[1], white[2]);        // lighthouse
    B(40, 20.5f, -62, 6, 2, 6, red[0], red[1], red[2]);
    B(-8, 2.1f, -40, 2.4f, 2, 2.4f, crate[0], crate[1], crate[2]); // crates on the quay
    B(-4, 2.1f, -41, 2.4f, 2, 2.4f, crate[0], crate[1], crate[2]);
    B(6, 2.6f, -39, 3, 3, 3, crate[0], crate[1], crate[2]);
    B(-26, 1.7f, -60, 3, 1.2f, 8, wood[0], wood[1], wood[2]);      // moored fishing boat

    // --- Shipyard (large, on the east shore, kept inside the shore line) ---
    B(38, 9, 6, 36, 18, 32, timber[0], timber[1], timber[2]);      // great ship hall
    B(38, 18.6f, 6, 38, 1.6f, 34, roof[0], roof[1], roof[2]);
    B(24, 1.4f, -46, 11, 0.8f, 34, wood[0], wood[1], wood[2]);     // slipway 1
    B(40, 1.4f, -44, 11, 0.8f, 32, wood[0], wood[1], wood[2]);     // slipway 2
    B(54, 1.4f, -40, 9, 0.8f, 28, wood[0], wood[1], wood[2]);      // slipway 3
    B(30, 9, -48, 2, 18, 2, metal[0], metal[1], metal[2]);         // gantry crane 1
    B(30, 17.5f, -40, 2, 2, 20, metal[0], metal[1], metal[2]);
    B(46, 9, -46, 2, 18, 2, metal[0], metal[1], metal[2]);         // gantry crane 2
    B(46, 17.5f, -38, 2, 2, 20, metal[0], metal[1], metal[2]);
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

void renderBuildScene(uint16_t viewId, const sea::Ship& ship,
                      const std::vector<sea::Wave>& waves, float timeSec,
                      float orbitAngle, int width, int height) {
    // Orbit camera around the stocks (scene origin) so the hull can be looked over
    // from every side as it takes shape.
    const float dist = 24.0f, camH = 12.0f;
    const bx::Vec3 eye = { bx::sin(orbitAngle) * dist, camH, bx::cos(orbitAngle) * dist };
    const bx::Vec3 at = { 0.0f, 3.2f, 0.0f };
    const bx::Vec3 up = { 0.0f, 1.0f, 0.0f };
    float view[16], proj[16];
    bx::mtxLookAt(view, eye, at, up);
    const bgfx::Caps* caps = bgfx::getCaps();
    const float aspect = float(width) / float(height > 0 ? height : 1);
    bx::mtxProj(proj, 55.0f, aspect, 0.1f, 600.0f, caps->homogeneousDepth);
    bgfx::setViewTransform(viewId, view, proj);
    bgfx::setViewRect(viewId, 0, 0, uint16_t(width), uint16_t(height));

    auto box = [&](float x, float y, float z, float sx, float sy, float sz,
                   float r, float g, float b) {
        ship_mesh::renderBoxSized(viewId, x, y, z, sx, sy, sz, r, g, b);
    };
    const float sand[3]  = { 0.82f, 0.74f, 0.52f };
    const float grass[3] = { 0.30f, 0.46f, 0.22f };
    const float dark[3]  = { 0.24f, 0.16f, 0.10f };
    const float wood[3]  = { 0.42f, 0.28f, 0.16f };
    const float timber[3]= { 0.52f, 0.40f, 0.27f };
    const float roof[3]  = { 0.35f, 0.20f, 0.14f };
    const float metal[3] = { 0.25f, 0.22f, 0.20f };

    // The sea, then the coastal yard: an island in the distance behind, a stone
    // dock the stocks sit on, a great ship hall + flanking gantry cranes behind
    // the berth (so the hull stays the clear foreground focus).
    water_gpu::render(viewId, waves, timeSec, eye.x, eye.y, eye.z, 24.0f, 76.0f);
    box(0.0f, 6.0f, 96.0f, 180.0f, 12.0f, 74.0f, grass[0], grass[1], grass[2]);   // island (distance)
    box(0.0f, 0.8f, 58.0f, 200.0f, 2.0f, 20.0f, sand[0], sand[1], sand[2]);       // shore/beach
    box(0.0f, 0.5f, 6.0f, 56.0f, 1.0f, 46.0f, sand[0], sand[1], sand[2]);         // dock ground
    box(0.0f, 10.0f, 40.0f, 48.0f, 20.0f, 22.0f, timber[0], timber[1], timber[2]); // great ship hall
    box(0.0f, 20.6f, 40.0f, 50.0f, 1.6f, 24.0f, roof[0], roof[1], roof[2]);
    for (int s = -1; s <= 1; s += 2) {                                            // gantry cranes
        box(float(s) * 15.0f, 10.0f, -2.0f, 1.8f, 20.0f, 1.8f, metal[0], metal[1], metal[2]);
        box(float(s) * 15.0f, 19.0f, 4.0f, 1.8f, 1.8f, 22.0f, metal[0], metal[1], metal[2]);
    }
    box(26.0f, 2.0f, 8.0f, 4.0f, 3.0f, 16.0f, wood[0], wood[1], wood[2]);          // timber stacks
    box(-26.0f, 2.0f, 8.0f, 4.0f, 3.0f, 16.0f, timber[0], timber[1], timber[2]);

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
        box(0.0f, 1.9f, z, 1.6f, 2.0f, 1.0f, dark[0], dark[1], dark[2]); // keel blocks (top at 2.9)
    }
    for (int i = 0; i < 4; ++i) {
        const float z = -len * 0.30f + (len * 0.60f) * i / 3.0f;
        box(-wid * 0.64f, 2.7f, z, 0.5f, 4.6f, 0.5f, wood[0], wood[1], wood[2]);
        box( wid * 0.64f, 2.7f, z, 0.5f, 4.6f, 0.5f, wood[0], wood[1], wood[2]);
    }

    // The hull, keel resting on the blocks.
    sea::FloatPose bp;
    bp.heaveY = blockTop + float(ship.bounds.depth) * 0.55; // keel local y = -depth*0.55
    ship_mesh::render(viewId, ship, bp, 0.0f, 0.0f, 0.0f, timeSec, 0.0f, 0.0f);
}

} // namespace ship_view
