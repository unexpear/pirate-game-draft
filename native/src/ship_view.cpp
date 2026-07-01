// Sea Trial — 3D scene view (Milestone 0+1). Public domain (Unlicense).
#include "ship_view.h"

#include "ship_model.hpp"

#include <bgfx/bgfx.h>
#include <bx/math.h>
#include <bx/bounds.h>

#include <debugdraw/debugdraw.h>

namespace {

// --- Water grid (wireframe, CPU-displaced by Gerstner) ---
constexpr int kCells = 52;          // grid cells per side
constexpr float kExtent = 52.0f;    // world size of the grid
constexpr int kVerts = kCells + 1;  // vertices per side

std::vector<DdVertex> s_grid;       // kVerts * kVerts positions
std::vector<uint16_t> s_gridLines;  // index pairs for the line segments

void buildGridTopology() {
    s_grid.resize(kVerts * kVerts);
    s_gridLines.clear();
    // Horizontal segments (along +x).
    for (int r = 0; r < kVerts; ++r)
        for (int c = 0; c < kCells; ++c) {
            s_gridLines.push_back(uint16_t(r * kVerts + c));
            s_gridLines.push_back(uint16_t(r * kVerts + c + 1));
        }
    // Vertical segments (along +z).
    for (int c = 0; c < kVerts; ++c)
        for (int r = 0; r < kCells; ++r) {
            s_gridLines.push_back(uint16_t(r * kVerts + c));
            s_gridLines.push_back(uint16_t((r + 1) * kVerts + c));
        }
}

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

void drawWater(DebugDrawEncoder& dde, const std::vector<sea::Wave>& waves, float t) {
    for (int r = 0; r < kVerts; ++r) {
        for (int c = 0; c < kVerts; ++c) {
            const float x = (c / float(kCells) - 0.5f) * kExtent;
            const float z = (r / float(kCells) - 0.5f) * kExtent;
            const float y = float(sea::sampleWater(waves, x, z, t).height);
            s_grid[r * kVerts + c] = { x, y, z };
        }
    }
    dde.setColor(abgr(60, 150, 205)); // ocean blue
    dde.drawLineList(uint32_t(s_grid.size()), s_grid.data(),
                     uint32_t(s_gridLines.size()), s_gridLines.data());
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
    buildGridTopology();
}

void shutdown() {
    s_grid.clear();
    s_gridLines.clear();
    ddShutdown();
}

void render(uint16_t viewId, const sea::Ship& ship, const std::vector<sea::Wave>& waves,
            float timeSec, int width, int height) {
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

    DebugDrawEncoder dde;
    dde.begin(viewId);
    drawWater(dde, waves, timeSec);
    drawShip(dde, ship);
    dde.end();
}

} // namespace ship_view
