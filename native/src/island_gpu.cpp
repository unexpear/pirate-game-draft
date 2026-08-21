// Sea Trial — GPU island terrain. Public domain (Unlicense).
#include "island_gpu.h"

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

constexpr float kR = 78.0f;   // heightfield half-extent (reaches out past the shore)
constexpr int   kN = 120;     // grid resolution (kN+1 verts per side)
constexpr float kMeanCoast = 50.0f; // mean above-water radius

bgfx::VertexLayout s_layout;
bgfx::VertexBufferHandle s_vbh = BGFX_INVALID_HANDLE;
bgfx::IndexBufferHandle s_ibh = BGFX_INVALID_HANDLE;
bgfx::ProgramHandle s_prog = BGFX_INVALID_HANDLE;
bgfx::UniformHandle u_lightDir = BGFX_INVALID_HANDLE;

// --- deterministic value noise (no rand(): the island is the same every run) ---
float hash01(int x, int y) {
    uint32_t h = uint32_t(x) * 374761393u + uint32_t(y) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return float(h & 0xFFFFFFu) / float(0x1000000);
}
float valueNoise(float x, float y) {
    const float fxi = std::floor(x), fyi = std::floor(y);
    const int xi = int(fxi), yi = int(fyi);
    const float fx = x - fxi, fy = y - fyi;
    const float u = fx * fx * (3.0f - 2.0f * fx);
    const float v = fy * fy * (3.0f - 2.0f * fy);
    const float a = hash01(xi, yi), b = hash01(xi + 1, yi);
    const float c = hash01(xi, yi + 1), d = hash01(xi + 1, yi + 1);
    return a * (1 - u) * (1 - v) + b * u * (1 - v) + c * (1 - u) * v + d * u * v;
}
float fbm(float x, float y) {
    return 0.6f * valueNoise(x, y) + 0.3f * valueNoise(x * 2.03f, y * 2.03f)
         + 0.1f * valueNoise(x * 4.01f, y * 4.01f);
}
float hill(float x, float z, float cx, float cz, float sig, float amp) {
    const float dx = x - cx, dz = z - cz;
    return amp * std::exp(-(dx * dx + dz * dz) / (2.0f * sig * sig));
}

// Bake color for a surface point from its height and steepness.
uint32_t colorFor(float x, float z, float h, float slopeUp) {
    float r, g, b;
    const float tint = fbm(x * 0.09f, z * 0.09f) - 0.5f; // ±0.5 mottling
    if (h < 0.3f) {                 // wet sand at the waterline
        r = 0.60f; g = 0.55f; b = 0.42f;
    } else if (h < 2.6f) {          // dry beach
        r = 0.80f; g = 0.72f; b = 0.51f;
    } else if (slopeUp < 0.62f) {   // steep faces -> exposed rock
        r = 0.44f; g = 0.41f; b = 0.37f;
    } else if (h < 12.0f) {         // meadow
        r = 0.31f; g = 0.47f; b = 0.22f;
    } else if (h < 22.0f) {         // upland grass
        r = 0.24f; g = 0.38f; b = 0.17f;
    } else {                        // rocky crown
        r = 0.48f; g = 0.46f; b = 0.42f;
    }
    r = std::min(1.0f, std::max(0.0f, r + tint * 0.10f));
    g = std::min(1.0f, std::max(0.0f, g + tint * 0.10f));
    b = std::min(1.0f, std::max(0.0f, b + tint * 0.08f));
    const uint32_t R = uint32_t(r * 255.0f), G = uint32_t(g * 255.0f), B = uint32_t(b * 255.0f);
    return R | (G << 8) | (B << 16) | (0xFFu << 24);
}

} // namespace

namespace island_gpu {

float heightAt(float x, float z) {
    const float r = std::sqrt(x * x + z * z);
    const float ang = std::atan2(z, x);
    // Irregular coastline (all terms 2*pi-periodic -> no seam at +-pi).
    const float coastR = kMeanCoast + 8.0f * std::sin(3.0f * ang + 0.5f)
                       + 4.0f * std::sin(5.0f * ang + 2.0f)
                       + 3.0f * std::cos(2.0f * ang - 1.0f);
    const float d = coastR - r; // metres inland of the shore (negative = offshore)
    float h;
    if (d < 0.0f) h = d * 0.45f;                          // submerged shelf sloping away
    else          h = 4.5f * (1.0f - std::exp(-d / 9.0f)); // beach rising to a ~4.5 ft shelf
    // Inland relief: real hills toward the interior (rising well above the
    // shore-line buildings so the land reads as having mass, not a flat pad).
    h += hill(x, z, 4.0f, 28.0f, 24.0f, 30.0f);   // main peak
    h += hill(x, z, -22.0f, 12.0f, 16.0f, 16.0f); // western shoulder
    h += hill(x, z, 26.0f, 36.0f, 15.0f, 15.0f);  // northern secondary summit
    // Ground texture, faded out below the waterline so the shelf stays smooth.
    const float landFrac = std::min(1.0f, std::max(0.0f, d / 8.0f));
    h += (fbm(x * 0.05f, z * 0.05f) - 0.5f) * 5.5f * landFrac;
    return h;
}

float landRadius() { return kMeanCoast; }

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
    const float lightV[4] = { 0.5f, 0.6f, 0.62f, 0.0f };
    bgfx::setUniform(u_lightDir, lightV);
    float m[16];
    bx::mtxTranslate(m, relX, 0.0f, relZ);
    bgfx::setTransform(m);
    bgfx::setVertexBuffer(0, s_vbh);
    bgfx::setIndexBuffer(s_ibh);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z
                   | BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_MSAA); // no cull: seen from all sides
    bgfx::submit(viewId, s_prog);
}

} // namespace island_gpu
