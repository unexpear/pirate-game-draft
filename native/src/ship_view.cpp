// Sea Trial — 3D scene view (Milestone 0+1). Public domain (Unlicense).
#include "ship_view.h"

#include "ship_model.hpp"
#include "water_gpu.h"

#include <bgfx/bgfx.h>
#include <bx/math.h>
#include <bx/bounds.h>

#include <debugdraw/debugdraw.h>

namespace {

uint32_t abgr(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    return (uint32_t(a) << 24) | (uint32_t(b) << 16) | (uint32_t(g) << 8) | uint32_t(r);
}

uint32_t pieceColor(const sea::Piece& p) {
    if (p.damage >= 1.0) return abgr(28, 28, 34);     // destroyed — near black
    if (p.damage > 0.0) return abgr(170, 55, 45);     // damaged — red
    if (p.type == "keel" || p.type == "rib") return abgr(74, 44, 22); // dark structural wood
    if (p.type == "deck") return abgr(158, 112, 64);  // lighter deck
    return abgr(139, 92, 46);                          // plank — oak
}

void drawShip(DebugDrawEncoder& dde, const sea::Ship& ship) {
    const bx::Aabb unit = { { -0.5f, -0.5f, -0.5f }, { 0.5f, 0.5f, 0.5f } };
    for (const auto& p : ship.pieces) {
        float mtx[16];
        bx::mtxSRT(mtx,
            float(p.bounds.x), float(p.bounds.y), float(p.bounds.z),
            float(p.rotation.x), float(p.rotation.y), float(p.rotation.z),
            float(p.position.x), float(p.position.y), float(p.position.z));
        dde.pushTransform(mtx);
        dde.setColor(pieceColor(p));
        dde.setWireframe(false);
        dde.draw(unit);
        dde.popTransform();
    }
}

} // namespace

namespace ship_view {

void init() {
    ddInit();
    water_gpu::init();
}

void shutdown() {
    water_gpu::shutdown();
    ddShutdown();
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

    // GPU water surface (uses the camera just set on this view).
    water_gpu::render(viewId, waves, timeSec, eye.x, eye.y, eye.z);

    // Ship: a root transform (heave + pitch + heel) above the per-piece boxes.
    DebugDrawEncoder dde;
    dde.begin(viewId);
    float shipRoot[16];
    bx::mtxSRT(shipRoot, 1.0f, 1.0f, 1.0f,
        float(pose.pitch), 0.0f, float(pose.heel),
        0.0f, float(pose.heaveY), 0.0f);
    dde.pushTransform(shipRoot);
    drawShip(dde, ship);
    dde.popTransform();
    dde.end();
}

} // namespace ship_view
