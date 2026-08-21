// Sea Trial — lit ship meshes (Milestone 0+1). Public domain (Unlicense).
#include "ship_mesh.h"

#include "ship_model.hpp"
#include "shadow_gpu.h"

#include <bgfx/bgfx.h>
#include <bx/math.h>

#include <algorithm>
#include <cmath>

#include "dxbc/vs_mesh.sc.bin.h" // vs_mesh_dxbc[]
#include "dxbc/fs_mesh.sc.bin.h" // fs_mesh_dxbc[]

namespace {

struct MeshVertex { float x, y, z, nx, ny, nz; };

// Unit cube [-0.5, 0.5], 4 verts per face with the face normal (flat shading).
const MeshVertex kCube[24] = {
    // +X
    { 0.5f,-0.5f,-0.5f, 1,0,0}, { 0.5f, 0.5f,-0.5f, 1,0,0}, { 0.5f, 0.5f, 0.5f, 1,0,0}, { 0.5f,-0.5f, 0.5f, 1,0,0},
    // -X
    {-0.5f,-0.5f, 0.5f,-1,0,0}, {-0.5f, 0.5f, 0.5f,-1,0,0}, {-0.5f, 0.5f,-0.5f,-1,0,0}, {-0.5f,-0.5f,-0.5f,-1,0,0},
    // +Y
    {-0.5f, 0.5f,-0.5f, 0,1,0}, {-0.5f, 0.5f, 0.5f, 0,1,0}, { 0.5f, 0.5f, 0.5f, 0,1,0}, { 0.5f, 0.5f,-0.5f, 0,1,0},
    // -Y
    {-0.5f,-0.5f, 0.5f, 0,-1,0},{-0.5f,-0.5f,-0.5f, 0,-1,0},{ 0.5f,-0.5f,-0.5f, 0,-1,0},{ 0.5f,-0.5f, 0.5f, 0,-1,0},
    // +Z
    {-0.5f,-0.5f, 0.5f, 0,0,1}, { 0.5f,-0.5f, 0.5f, 0,0,1}, { 0.5f, 0.5f, 0.5f, 0,0,1}, {-0.5f, 0.5f, 0.5f, 0,0,1},
    // -Z
    { 0.5f,-0.5f,-0.5f, 0,0,-1},{-0.5f,-0.5f,-0.5f, 0,0,-1},{-0.5f, 0.5f,-0.5f, 0,0,-1},{ 0.5f, 0.5f,-0.5f, 0,0,-1},
};
const uint16_t kIdx[36] = {
    0,1,2, 0,2,3,      4,5,6, 4,6,7,
    8,9,10, 8,10,11,   12,13,14, 12,14,15,
    16,17,18, 16,18,19, 20,21,22, 20,22,23,
};

bgfx::VertexLayout s_layout;
bgfx::VertexBufferHandle s_vbh = BGFX_INVALID_HANDLE;
bgfx::IndexBufferHandle s_ibh = BGFX_INVALID_HANDLE;
bgfx::ProgramHandle s_prog = BGFX_INVALID_HANDLE;
bgfx::UniformHandle u_color = BGFX_INVALID_HANDLE;
bgfx::UniformHandle u_lightDir = BGFX_INVALID_HANDLE;
bgfx::UniformHandle u_mat = BGFX_INVALID_HANDLE;

void setMat(float m) { const float v[4] = { m, 0.0f, 0.0f, 0.0f }; bgfx::setUniform(u_mat, v); }

// Which procedural surface a hull piece wears: rope/blocks stay flat, the rest
// is planked timber.
float pieceMat(const sea::Piece& p) {
    if (p.type == "line" || p.type == "block") return 0.0f;
    return 1.0f;
}

void pieceColor(const sea::Piece& p, float out[4]) {
    auto set = [&](float r, float g, float b) { out[0] = r; out[1] = g; out[2] = b; out[3] = 1.0f; };
    if (p.damage >= 1.0) set(0.11f, 0.11f, 0.13f);          // destroyed
    else if (p.damage > 0.0) set(0.67f, 0.22f, 0.18f);      // damaged
    else if (p.type == "stem" || p.type == "sternpost") set(0.48f, 0.33f, 0.19f); // backbone posts
    else if (p.type == "mast" || p.type == "bowsprit") set(0.62f, 0.49f, 0.32f);  // pine spars
    else if (p.type == "line") set(0.16f, 0.12f, 0.09f);    // rope / rigging
    else if (p.type == "block") set(0.24f, 0.18f, 0.12f);  // blocks / pulleys
    else if (p.type == "helm" || p.type == "capstan") set(0.44f, 0.32f, 0.20f);   // fittings
    else if (p.type == "keel" || p.type == "rib") set(0.50f, 0.34f, 0.20f); // structural
    else if (p.type == "deck") set(0.66f, 0.48f, 0.29f);    // deck
    else set(0.66f, 0.47f, 0.28f);                          // plank (warm oak, lifted off near-black)
}

} // namespace

namespace ship_mesh {

void init() {
    s_layout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
        .end();
    s_vbh = bgfx::createVertexBuffer(bgfx::copy(kCube, sizeof(kCube)), s_layout);
    s_ibh = bgfx::createIndexBuffer(bgfx::copy(kIdx, sizeof(kIdx)));
    s_prog = bgfx::createProgram(
        bgfx::createShader(bgfx::copy(vs_mesh_dxbc, sizeof(vs_mesh_dxbc))),
        bgfx::createShader(bgfx::copy(fs_mesh_dxbc, sizeof(fs_mesh_dxbc))),
        true);
    u_color = bgfx::createUniform("u_color", bgfx::UniformType::Vec4);
    u_lightDir = bgfx::createUniform("u_lightDir", bgfx::UniformType::Vec4);
    u_mat = bgfx::createUniform("u_mat", bgfx::UniformType::Vec4);
}

void shutdown() {
    if (bgfx::isValid(u_mat)) bgfx::destroy(u_mat);
    if (bgfx::isValid(u_lightDir)) bgfx::destroy(u_lightDir);
    if (bgfx::isValid(u_color)) bgfx::destroy(u_color);
    if (bgfx::isValid(s_prog)) bgfx::destroy(s_prog);
    if (bgfx::isValid(s_ibh)) bgfx::destroy(s_ibh);
    if (bgfx::isValid(s_vbh)) bgfx::destroy(s_vbh);
    u_mat = BGFX_INVALID_HANDLE;
    u_lightDir = BGFX_INVALID_HANDLE;
    u_color = BGFX_INVALID_HANDLE;
    s_prog = BGFX_INVALID_HANDLE;
    s_ibh = BGFX_INVALID_HANDLE;
    s_vbh = BGFX_INVALID_HANDLE;
}

void render(uint16_t viewId, const sea::Ship& ship, const sea::FloatPose& pose,
            float heading, float windDir, float sailFullness, float timeSec,
            float posX, float posZ, float heelScale) {
    // Real sailing visuals: the wind angle off the bow drives sail trim, luffing,
    // and how far the hull heels to leeward under sail pressure.
    const float align = std::cos(heading - windDir);
    const float awa = std::acos(std::max(-1.0f, std::min(1.0f, -align))); // 0=in irons .. pi=astern
    const float lee = (std::sin(windDir - heading) >= 0.0f) ? 1.0f : -1.0f; // leeward side
    const float power = float(sea::sailPower(double(awa) * 57.2957795));
    const float windHeel = lee * 0.34f * power * std::sin(awa) * sailFullness * heelScale; // lean to leeward

    // Ship root: yaw (heading) + heave + pitch + (wave heel + wind heel) + world
    // position, above each piece's local transform.
    float shipRoot[16];
    bx::mtxSRT(shipRoot, 1.0f, 1.0f, 1.0f,
        float(pose.pitch), heading, float(pose.heel) + windHeel,
        posX, float(pose.heaveY), posZ);

    const float lightV[4] = { 0.5f, 0.6f, 0.62f, 0.0f }; // lower warm sun: lights vertical faces
    bgfx::setUniform(u_lightDir, lightV);

    for (const auto& p : ship.pieces) {
        float local[16];
        bx::mtxSRT(local,
            float(p.bounds.x), float(p.bounds.y), float(p.bounds.z),
            float(p.rotation.x), float(p.rotation.y), float(p.rotation.z),
            float(p.position.x), float(p.position.y), float(p.position.z));
        float model[16];
        bx::mtxMul(model, local, shipRoot); // world = local composed under ship root

        float col[4];
        pieceColor(p, col);
        bgfx::setUniform(u_color, col);
        setMat(pieceMat(p));
        shadow::bindRead(4);
        bgfx::setTransform(model);
        bgfx::setVertexBuffer(0, s_vbh);
        bgfx::setIndexBuffer(s_ibh);
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z
                       | BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_CULL_CW | BGFX_STATE_MSAA);
        bgfx::submit(viewId, s_prog);
    }

    // The sail, composed under the same ship root. (The mast, bowsprit, fittings
    // and rigging are real hull pieces, drawn in the piece loop above.)
    const uint64_t baseState = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z
                             | BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_MSAA;
    const float depth = float(ship.bounds.depth);
    const float len = float(ship.bounds.length);
    const float wid = float(ship.bounds.width);

    if (ship.systems.sail_count > 0 && sailFullness > 0.02f) {
        // Trim: the sail sets from athwartships (running) toward fore-and-aft
        // (close-hauled). In the no-go zone it luffs — shivers and goes slack.
        const bool luffing = awa < 0.75f || power < 0.08f; // ~43 deg no-go
        const float flap = luffing ? std::sin(timeSec * 20.0f) * 0.30f : 0.0f;
        const float trim = lee * (1.57079633f - awa * 0.5f) + flap;
        const float mastH = depth * 0.9f + 6.0f;              // matches ship_model's mast
        const float fullH = mastH * 0.76f;                    // spans yard down to just above the deck
        const float yardTop = depth * 0.03f + mastH * 0.90f;  // top edge below the masthead
        const float h = fullH * sailFullness * (luffing ? 0.85f : 1.0f); // slack when luffing
        const float sailW = wid * 1.45f;                      // square-rig width (not oversized)
        // Sail root: trimmed + positioned; strips billow within this frame.
        float sailRoot[16];
        bx::mtxSRT(sailRoot, 1.0f, 1.0f, 1.0f, 0.0f, trim, 0.0f,
                   0.0f, yardTop - h * 0.5f, -len * 0.05f);
        float sailModel[16];
        bx::mtxMul(sailModel, sailRoot, shipRoot);
        const float sailCol[4] = { 0.95f, 0.90f, 0.76f, 1.0f }; // warm canvas cream
        bgfx::setUniform(u_color, sailCol);
        setMat(3.0f); // canvas: bright warm cloth shading
        // Billow: draw the canvas as vertical strips bellied to leeward in a
        // parabola, so it reads as a filled, curved sail instead of a flat slab.
        const int kStrips = 7;
        const float belly = lee * (0.16f * sailW) * sailFullness * (luffing ? 0.35f : 1.0f);
        for (int s = 0; s < kStrips; ++s) {
            const float sc = (s + 0.5f) / float(kStrips); // 0..1 across the width
            const float lx = (sc - 0.5f) * sailW;
            const float t01 = 2.0f * sc - 1.0f;
            const float bz = belly * (1.0f - t01 * t01); // max belly mid-sail
            float strip[16];
            bx::mtxSRT(strip, sailW / float(kStrips) * 1.06f, h, 0.05f, 0.0f, 0.0f, 0.0f, lx, 0.0f, bz);
            float model[16];
            bx::mtxMul(model, strip, sailModel);
            shadow::bindRead(4);
            bgfx::setTransform(model);
            bgfx::setVertexBuffer(0, s_vbh);
            bgfx::setIndexBuffer(s_ibh);
            bgfx::setState(baseState); // no cull: sail visible from both sides
            bgfx::submit(viewId, s_prog);
        }
    }
}

void renderDepth(uint16_t viewId, const sea::Ship& ship, const sea::FloatPose& pose,
                 float heading, float windDir, float sailFullness, float posX, float posZ, float heelScale) {
    // Same ship root as render(), so the shadow matches the visible hull. Submits
    // the hull/mast/rig pieces with the depth-only program to the shadow view.
    const float align = std::cos(heading - windDir);
    const float awa = std::acos(std::max(-1.0f, std::min(1.0f, -align)));
    const float lee = (std::sin(windDir - heading) >= 0.0f) ? 1.0f : -1.0f;
    const float power = float(sea::sailPower(double(awa) * 57.2957795));
    const float windHeel = lee * 0.34f * power * std::sin(awa) * sailFullness * heelScale;
    float shipRoot[16];
    bx::mtxSRT(shipRoot, 1.0f, 1.0f, 1.0f,
        float(pose.pitch), heading, float(pose.heel) + windHeel, posX, float(pose.heaveY), posZ);
    for (const auto& p : ship.pieces) {
        float local[16];
        bx::mtxSRT(local,
            float(p.bounds.x), float(p.bounds.y), float(p.bounds.z),
            float(p.rotation.x), float(p.rotation.y), float(p.rotation.z),
            float(p.position.x), float(p.position.y), float(p.position.z));
        float model[16];
        bx::mtxMul(model, local, shipRoot);
        bgfx::setTransform(model);
        bgfx::setVertexBuffer(0, s_vbh);
        bgfx::setIndexBuffer(s_ibh);
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_CULL_CW);
        bgfx::submit(viewId, shadow::program());
    }

    // The sail is the biggest occluder — cast it too (matches render()'s billow).
    if (ship.systems.sail_count > 0 && sailFullness > 0.02f) {
        const float depth = float(ship.bounds.depth);
        const float len = float(ship.bounds.length);
        const float wid = float(ship.bounds.width);
        const bool luffing = awa < 0.75f || power < 0.08f;
        const float trim = lee * (1.57079633f - awa * 0.5f);
        const float mastH = depth * 0.9f + 6.0f;
        const float fullH = mastH * 0.76f;
        const float yardTop = depth * 0.03f + mastH * 0.90f;
        const float h = fullH * sailFullness * (luffing ? 0.85f : 1.0f);
        const float sailW = wid * 1.45f;
        float sailRoot[16];
        bx::mtxSRT(sailRoot, 1.0f, 1.0f, 1.0f, 0.0f, trim, 0.0f, 0.0f, yardTop - h * 0.5f, -len * 0.05f);
        float sailModel[16];
        bx::mtxMul(sailModel, sailRoot, shipRoot);
        const int kStrips = 7;
        const float belly = lee * (0.16f * sailW) * sailFullness * (luffing ? 0.35f : 1.0f);
        for (int s = 0; s < kStrips; ++s) {
            const float sc = (s + 0.5f) / float(kStrips);
            const float lx = (sc - 0.5f) * sailW;
            const float t01 = 2.0f * sc - 1.0f;
            const float bz = belly * (1.0f - t01 * t01);
            float strip[16];
            bx::mtxSRT(strip, sailW / float(kStrips) * 1.06f, h, 0.05f, 0.0f, 0.0f, 0.0f, lx, 0.0f, bz);
            float model[16];
            bx::mtxMul(model, strip, sailModel);
            bgfx::setTransform(model);
            bgfx::setVertexBuffer(0, s_vbh);
            bgfx::setIndexBuffer(s_ibh);
            bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS);
            bgfx::submit(viewId, shadow::program());
        }
    }
}

void renderBoxSized(uint16_t viewId, float x, float y, float z,
                    float sx, float sy, float sz, float r, float g, float b,
                    float mat) {
    const float lightV[4] = { 0.5f, 0.6f, 0.62f, 0.0f };
    bgfx::setUniform(u_lightDir, lightV);
    float m[16];
    bx::mtxSRT(m, sx, sy, sz, 0.0f, 0.0f, 0.0f, x, y, z);
    const float col[4] = { r, g, b, 1.0f };
    bgfx::setUniform(u_color, col);
    setMat(mat);
    shadow::bindRead(4);
    bgfx::setTransform(m);
    bgfx::setVertexBuffer(0, s_vbh);
    bgfx::setIndexBuffer(s_ibh);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z
                   | BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_CULL_CW | BGFX_STATE_MSAA);
    bgfx::submit(viewId, s_prog);
}

void renderBox(uint16_t viewId, float x, float y, float z, float size,
               float r, float g, float b) {
    renderBoxSized(viewId, x, y, z, size, size, size, r, g, b, 0.0f); // markers: flat
}

void renderBoxRot(uint16_t viewId, float x, float y, float z, float sx, float sy, float sz,
                  float rx, float ry, float rz, float r, float g, float b, float mat) {
    const float lightV[4] = { 0.5f, 0.6f, 0.62f, 0.0f };
    bgfx::setUniform(u_lightDir, lightV);
    float m[16];
    bx::mtxSRT(m, sx, sy, sz, rx, ry, rz, x, y, z);
    const float col[4] = { r, g, b, 1.0f };
    bgfx::setUniform(u_color, col);
    setMat(mat);
    shadow::bindRead(4);
    bgfx::setTransform(m);
    bgfx::setVertexBuffer(0, s_vbh);
    bgfx::setIndexBuffer(s_ibh);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z
                   | BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_CULL_CW | BGFX_STATE_MSAA);
    bgfx::submit(viewId, s_prog);
}

void renderShadow(uint16_t viewId, float x, float y, float z, float sx, float sz, float alpha) {
    const float lightV[4] = { 0.5f, 0.6f, 0.62f, 0.0f };
    bgfx::setUniform(u_lightDir, lightV);
    float m[16];
    bx::mtxSRT(m, sx, 0.2f, sz, 0.0f, 0.0f, 0.0f, x, y, z);
    const float col[4] = { 0.03f, 0.05f, 0.08f, alpha }; // dark, translucent
    bgfx::setUniform(u_color, col);
    setMat(0.0f);
    shadow::bindRead(4);
    bgfx::setTransform(m);
    bgfx::setVertexBuffer(0, s_vbh);
    bgfx::setIndexBuffer(s_ibh);
    // Alpha-blended, no depth write, so it darkens the surface without occluding.
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_DEPTH_TEST_LESS
                   | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA));
    bgfx::submit(viewId, s_prog);
}

void renderCharacter(uint16_t viewId, float x, float y, float z, float heading, float walkPhase) {
    const float lightV[4] = { 0.5f, 0.6f, 0.62f, 0.0f };
    bgfx::setUniform(u_lightDir, lightV);
    // Character root: stands at (x,y,z) (y = feet on the ground), faces `heading`.
    float root[16];
    bx::mtxSRT(root, 1.0f, 1.0f, 1.0f, 0.0f, heading, 0.0f, x, y, z);
    auto part = [&](float lx, float ly, float lz, float sx, float sy, float sz,
                    float r, float g, float b) {
        float local[16];
        bx::mtxSRT(local, sx, sy, sz, 0.0f, 0.0f, 0.0f, lx, ly, lz);
        float model[16];
        bx::mtxMul(model, local, root);
        const float col[4] = { r, g, b, 1.0f };
        bgfx::setUniform(u_color, col);
        setMat(0.0f); // person: flat, no plank texture
        shadow::bindRead(4);
        bgfx::setTransform(model);
        bgfx::setVertexBuffer(0, s_vbh);
        bgfx::setIndexBuffer(s_ibh);
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z
                       | BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_CULL_CW | BGFX_STATE_MSAA);
        bgfx::submit(viewId, s_prog);
    };
    const float sw = std::sin(walkPhase) * 0.18f;             // leg/arm swing (fore-aft)
    const float bob = std::fabs(std::sin(walkPhase)) * 0.04f; // step bob
    const float skin = 0.80f, coatR = 0.45f, coatG = 0.28f, coatB = 0.20f;
    part(0.0f, 1.55f + bob, 0.0f, 0.28f, 0.30f, 0.28f, skin, 0.62f, 0.48f);      // head
    part(0.0f, 1.08f + bob, 0.0f, 0.52f, 0.66f, 0.30f, coatR, coatG, coatB);      // torso (coat)
    part(-0.14f, 0.42f,  sw, 0.22f, 0.82f, 0.24f, 0.26f, 0.22f, 0.30f);           // left leg
    part( 0.14f, 0.42f, -sw, 0.22f, 0.82f, 0.24f, 0.26f, 0.22f, 0.30f);           // right leg
    part(-0.34f, 1.08f + bob, -sw, 0.16f, 0.62f, 0.20f, coatR, coatG, coatB);     // left arm
    part( 0.34f, 1.08f + bob,  sw, 0.16f, 0.62f, 0.20f, coatR, coatG, coatB);     // right arm
}

} // namespace ship_mesh
