// Sea Trial — GPU island terrain. Public domain (Unlicense).
#include "island_gpu.h"
#include "island_shape.hpp"
#include "shadow_gpu.h"

#include <bgfx/bgfx.h>
#include <bx/math.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "dxbc/vs_terrain.sc.bin.h" // vs_terrain_dxbc[]
#include "dxbc/fs_terrain.sc.bin.h" // fs_terrain_dxbc[]

namespace {

struct TVert { float x, y, z, nx, ny, nz; uint32_t rgba; };

constexpr float kR = 92.0f;   // heightfield half-extent (reaches out past the shore)
constexpr int   kN = 140;     // grid resolution (kN+1 verts per side)

bgfx::VertexLayout s_layout;
bgfx::VertexBufferHandle s_vbh = BGFX_INVALID_HANDLE;
bgfx::IndexBufferHandle s_ibh = BGFX_INVALID_HANDLE;
bgfx::ProgramHandle s_prog = BGFX_INVALID_HANDLE;
bgfx::UniformHandle u_lightDir = BGFX_INVALID_HANDLE;

// Bake color for a surface point. Colours are BLENDED continuously by height
// (and slope) rather than stepped, so the beach->grass->rock read as one rising
// slope instead of three flat stacked shelves.
uint32_t colorFor(float x, float z, float h, float slopeUp) {
    auto ss = [](float e0, float e1, float t) { t = std::min(1.0f, std::max(0.0f, (t - e0) / (e1 - e0))); return t * t * (3.0f - 2.0f * t); };
    // Height ramp: wet sand -> dry sand -> grass -> upland grass -> rock crown.
    // Black Flag / Caribbean: golden-white sand, lush tropical greens, warm rock.
    const float sand[3]  = { 0.90f, 0.84f, 0.64f };
    const float wet[3]   = { 0.78f, 0.76f, 0.58f };
    const float grass[3] = { 0.26f, 0.58f, 0.24f };
    const float up[3]    = { 0.20f, 0.46f, 0.20f };
    const float rock[3]  = { 0.55f, 0.50f, 0.42f };
    float r = wet[0], g = wet[1], b = wet[2];
    float t;
    t = ss(0.2f, 1.4f, h);  r = r + (sand[0]-r)*t;  g = g + (sand[1]-g)*t;  b = b + (sand[2]-b)*t;  // wet->dry sand
    t = ss(2.8f, 5.5f, h);  r = r + (grass[0]-r)*t; g = g + (grass[1]-g)*t; b = b + (grass[2]-b)*t; // wide sand->grass
    t = ss(9.0f, 17.0f, h); r = r + (up[0]-r)*t;    g = g + (up[1]-g)*t;    b = b + (up[2]-b)*t;    // grass->upland
    t = ss(19.0f, 28.0f, h); r = r + (rock[0]-r)*t; g = g + (rock[1]-g)*t;  b = b + (rock[2]-b)*t;  // upland->rock
    // Trodden TOWN GROUND: inside the built south-shore footprint the grass reads
    // as packed earth / cobbled dirt, so the gaps between buildings aren't bright
    // lawn. Fades out at the town edges and reverts to grass up on the bare hill.
    {
        const float dirt[3] = { 0.50f, 0.44f, 0.34f };
        const float mx = ss(-60.0f, -54.0f, x) * (1.0f - ss(56.0f, 62.0f, x));
        const float mz = ss(-44.0f, -38.0f, z) * (1.0f - ss(22.0f, 28.0f, z));
        const float townM = mx * mz * ss(1.4f, 3.2f, h) * (1.0f - ss(15.0f, 20.0f, h)) * 0.85f;
        r = r + (dirt[0]-r)*townM; g = g + (dirt[1]-g)*townM; b = b + (dirt[2]-b)*townM;
    }
    // Steep faces above the beach turn to exposed rock.
    if (h > 3.0f) {
        float steep = 1.0f - ss(0.55f, 0.78f, slopeUp);
        r = r + (rock[0]-r)*steep*0.7f; g = g + (rock[1]-g)*steep*0.7f; b = b + (rock[2]-b)*steep*0.7f;
    }
    const float tint = island_shape::fbm(x * 0.09f, z * 0.09f) - 0.5f; // ±0.5 mottling
    r = std::min(1.0f, std::max(0.0f, r + tint * 0.10f));
    g = std::min(1.0f, std::max(0.0f, g + tint * 0.10f));
    b = std::min(1.0f, std::max(0.0f, b + tint * 0.08f));
    const uint32_t R = uint32_t(r * 255.0f), G = uint32_t(g * 255.0f), B = uint32_t(b * 255.0f);
    return R | (G << 8) | (B << 16) | (0xFFu << 24);
}

} // namespace

namespace island_gpu {

float heightAt(float x, float z) { return island_shape::heightAt(x, z); }

float landRadius() { return island_shape::kMeanCoast; }

void init() {
    s_layout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
        .end();

    const int vn = kN + 1;
    std::vector<TVert> verts(size_t(vn) * vn);
    const float step = (2.0f * kR) / float(kN);
    const float eps = step * 0.75f;
    for (int r = 0; r < vn; ++r) {
        for (int c = 0; c < vn; ++c) {
            const float x = -kR + c * step;
            const float z = -kR + r * step;
            const float h = heightAt(x, z);
            // Analytic-ish normal from central differences of the field.
            const float hx = heightAt(x + eps, z) - heightAt(x - eps, z);
            const float hz = heightAt(x, z + eps) - heightAt(x, z - eps);
            bx::Vec3 n = bx::normalize({ -hx, 2.0f * eps, -hz });
            verts[size_t(r) * vn + c] = { x, h, z, n.x, n.y, n.z, colorFor(x, z, h, n.y) };
        }
    }
    std::vector<uint32_t> idx;
    idx.reserve(size_t(kN) * kN * 6);
    for (int r = 0; r < kN; ++r) {
        for (int c = 0; c < kN; ++c) {
            const uint32_t a = uint32_t(r * vn + c);
            const uint32_t b = uint32_t(r * vn + c + 1);
            const uint32_t d = uint32_t((r + 1) * vn + c);
            const uint32_t e = uint32_t((r + 1) * vn + c + 1);
            idx.push_back(a); idx.push_back(d); idx.push_back(b);
            idx.push_back(b); idx.push_back(d); idx.push_back(e);
        }
    }

    s_vbh = bgfx::createVertexBuffer(bgfx::copy(verts.data(), uint32_t(verts.size() * sizeof(TVert))), s_layout);
    s_ibh = bgfx::createIndexBuffer(bgfx::copy(idx.data(), uint32_t(idx.size() * sizeof(uint32_t))), BGFX_BUFFER_INDEX32);
    s_prog = bgfx::createProgram(
        bgfx::createShader(bgfx::copy(vs_terrain_dxbc, sizeof(vs_terrain_dxbc))),
        bgfx::createShader(bgfx::copy(fs_terrain_dxbc, sizeof(fs_terrain_dxbc))),
        true);
    u_lightDir = bgfx::createUniform("u_lightDir", bgfx::UniformType::Vec4);
}

void shutdown() {
    if (bgfx::isValid(u_lightDir)) { bgfx::destroy(u_lightDir); u_lightDir = BGFX_INVALID_HANDLE; }
    if (bgfx::isValid(s_prog)) { bgfx::destroy(s_prog); s_prog = BGFX_INVALID_HANDLE; }
    if (bgfx::isValid(s_ibh)) { bgfx::destroy(s_ibh); s_ibh = BGFX_INVALID_HANDLE; }
    if (bgfx::isValid(s_vbh)) { bgfx::destroy(s_vbh); s_vbh = BGFX_INVALID_HANDLE; }
}

void render(uint16_t viewId, float relX, float relZ) {
    const float lightV[4] = { 0.42f, 0.68f, -0.55f, 0.0f };
    bgfx::setUniform(u_lightDir, lightV);
    float m[16];
    bx::mtxTranslate(m, relX, 0.0f, relZ);
    shadow::bindRead(4);
    bgfx::setTransform(m);
    bgfx::setVertexBuffer(0, s_vbh);
    bgfx::setIndexBuffer(s_ibh);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z
                   | BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_MSAA); // no cull: seen from all sides
    bgfx::submit(viewId, s_prog);
}

} // namespace island_gpu
