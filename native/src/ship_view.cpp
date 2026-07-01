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
            const sea::FloatPose& pose, float timeSec, int width, int height) {
    // Slow orbit around the hull.
    const float dist = 24.0f;
    const float angle = timeSec * 0.3f;
    const bx::Vec3 eye = { bx::sin(angle) * dist, 8.0f, bx::cos(angle) * dist };
    const bx::Vec3 at = { 0.0f, -0.4f, 0.0f };
    const bx::Vec3 up = { 0.0f, 1.0f, 0.0f };

    float view[16];
    float proj[16];
    bx::mtxLookAt(view, eye, at, up);
    const bgfx::Caps* caps = bgfx::getCaps();
    const float aspect = float(width) / float(height > 0 ? height : 1);
    bx::mtxProj(proj, 60.0f, aspect, 0.1f, 500.0f, caps->homogeneousDepth);
    bgfx::setViewTransform(viewId, view, proj);
    bgfx::setViewRect(viewId, 0, 0, uint16_t(width), uint16_t(height));

    water_gpu::render(viewId, waves, timeSec, eye.x, eye.y, eye.z);
    ship_mesh::render(viewId, ship, pose);
}

} // namespace ship_view
