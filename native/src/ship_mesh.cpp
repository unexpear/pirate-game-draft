// Sea Trial — lit ship meshes (Milestone 0+1). Public domain (Unlicense).
#include "ship_mesh.h"

#include "ship_model.hpp"

#include <bgfx/bgfx.h>
#include <bx/math.h>

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

void pieceColor(const sea::Piece& p, float out[4]) {
    auto set = [&](float r, float g, float b) { out[0] = r; out[1] = g; out[2] = b; out[3] = 1.0f; };
    if (p.damage >= 1.0) set(0.11f, 0.11f, 0.13f);          // destroyed
    else if (p.damage > 0.0) set(0.67f, 0.22f, 0.18f);      // damaged
    else if (p.type == "keel" || p.type == "rib") set(0.29f, 0.17f, 0.09f); // structural
    else if (p.type == "deck") set(0.62f, 0.44f, 0.25f);    // deck
    else set(0.55f, 0.36f, 0.18f);                          // plank
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
}

void shutdown() {
    if (bgfx::isValid(u_lightDir)) bgfx::destroy(u_lightDir);
    if (bgfx::isValid(u_color)) bgfx::destroy(u_color);
    if (bgfx::isValid(s_prog)) bgfx::destroy(s_prog);
    if (bgfx::isValid(s_ibh)) bgfx::destroy(s_ibh);
    if (bgfx::isValid(s_vbh)) bgfx::destroy(s_vbh);
    u_lightDir = BGFX_INVALID_HANDLE;
    u_color = BGFX_INVALID_HANDLE;
    s_prog = BGFX_INVALID_HANDLE;
    s_ibh = BGFX_INVALID_HANDLE;
    s_vbh = BGFX_INVALID_HANDLE;
}

void render(uint16_t viewId, const sea::Ship& ship, const sea::FloatPose& pose,
            float heading, float windDir, float sailFullness,
            float posX, float posZ) {
    // Ship root: yaw (heading) + heave + pitch + heel + world position, above each
    // piece's local transform.
    float shipRoot[16];
    bx::mtxSRT(shipRoot, 1.0f, 1.0f, 1.0f,
        float(pose.pitch), heading, float(pose.heel),
        posX, float(pose.heaveY), posZ);

    const float lightV[4] = { 0.4f, 0.85f, 0.35f, 0.0f };
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
        bgfx::setTransform(model);
        bgfx::setVertexBuffer(0, s_vbh);
        bgfx::setIndexBuffer(s_ibh);
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z
                       | BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_CULL_CW | BGFX_STATE_MSAA);
        bgfx::submit(viewId, s_prog);
    }

    // Mast + sail (data-driven), composed under the same ship root.
    const uint64_t baseState = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z
                             | BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_MSAA;
    const float depth = float(ship.bounds.depth);
    const float len = float(ship.bounds.length);
    const float wid = float(ship.bounds.width);

    if (ship.systems.mast_count > 0) {
        float local[16];
        bx::mtxSRT(local, 0.16f, depth * 0.9f + 4.6f, 0.16f, 0.0f, 0.0f, 0.0f,
                   0.0f, depth * 0.45f + 2.1f, -len * 0.05f);
        float model[16];
        bx::mtxMul(model, local, shipRoot);
        const float mastCol[4] = { 0.29f, 0.17f, 0.09f, 1.0f };
        bgfx::setUniform(u_color, mastCol);
        bgfx::setTransform(model);
        bgfx::setVertexBuffer(0, s_vbh);
        bgfx::setIndexBuffer(s_ibh);
        bgfx::setState(baseState | BGFX_STATE_CULL_CW);
        bgfx::submit(viewId, s_prog);
    }

    if (ship.systems.sail_count > 0 && sailFullness > 0.02f) {
        // The sail yaws partway toward the wind (trim) and reefs with the sail
        // state, furling up toward the yard as sailFullness -> 0.
        float rel = windDir - heading;
        while (rel > 3.14159265f) rel -= 6.28318531f;
        while (rel < -3.14159265f) rel += 6.28318531f;
        const float trim = rel * 0.4f;
        const float fullH = depth * 0.75f + 2.6f;
        const float yardTop = depth * 0.5f + 2.5f + fullH * 0.5f; // top edge sits on the yard
        const float h = fullH * sailFullness;
        float local[16];
        bx::mtxSRT(local, wid * 1.35f, h, 0.06f, 0.0f, trim, 0.0f,
                   0.0f, yardTop - h * 0.5f, -len * 0.05f);
        float model[16];
        bx::mtxMul(model, local, shipRoot);
        const float sailCol[4] = { 0.90f, 0.87f, 0.80f, 1.0f };
        bgfx::setUniform(u_color, sailCol);
        bgfx::setTransform(model);
        bgfx::setVertexBuffer(0, s_vbh);
        bgfx::setIndexBuffer(s_ibh);
        bgfx::setState(baseState); // no cull: sail visible from both sides
        bgfx::submit(viewId, s_prog);
    }
}

void renderBox(uint16_t viewId, float x, float y, float z, float size,
               float r, float g, float b) {
    const float lightV[4] = { 0.4f, 0.85f, 0.35f, 0.0f };
    bgfx::setUniform(u_lightDir, lightV);
    float m[16];
    bx::mtxSRT(m, size, size, size, 0.0f, 0.0f, 0.0f, x, y, z);
    const float col[4] = { r, g, b, 1.0f };
    bgfx::setUniform(u_color, col);
    bgfx::setTransform(m);
    bgfx::setVertexBuffer(0, s_vbh);
    bgfx::setIndexBuffer(s_ibh);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z
                   | BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_CULL_CW | BGFX_STATE_MSAA);
    bgfx::submit(viewId, s_prog);
}

} // namespace ship_mesh
