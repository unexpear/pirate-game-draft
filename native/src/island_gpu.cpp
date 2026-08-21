// Sea Trial — GPU island terrain. Public domain (Unlicense).
#include "island_gpu.h"
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
    // Steep faces above the beach turn to exposed rock.
    if (h > 3.0f) {
        float steep = 1.0f - ss(0.55f, 0.78f, slopeUp);
        r = r + (rock[0]-r)*steep*0.7f; g = g + (rock[1]-g)*steep*0.7f; b = b + (rock[2]-b)*steep*0.7f;
    }
    const float tint = fbm(x * 0.09f, z * 0.09f) - 0.5f; // ±0.5 mottling
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
    // Irregular coastline: periodic harmonics + a non-periodic noise sampled on a
    // circle, giving real inlets and points instead of a smooth oval. Clamped to
    // stay outside the water-cut radius (no dry moat).
    float coastR = kMeanCoast + 6.0f * std::sin(3.0f * ang + 0.5f)
                 + 4.0f * std::sin(5.0f * ang + 2.0f)
                 + 3.0f * std::cos(2.0f * ang - 1.0f)
                 + 4.0f * std::sin(7.0f * ang + 1.3f)
                 + (fbm(std::cos(ang) * 2.6f + 4.0f, std::sin(ang) * 2.6f + 7.0f) - 0.5f) * 9.0f;
    coastR = std::max(43.0f, coastR);
    const float d = coastR - r; // metres inland of the shore (negative = offshore)
    // Smooth base: a beach that KEEPS RISING inland (a continuous slope, not a
    // flat shelf) up into the hills, plus a steeper skirt continuing underwater
    // so the land reads as rising FROM the sea, not sitting ON it.
    float base;
    if (d < 0.0f) base = d * 0.60f;                                       // steeper submerged skirt
    // A wide, gentle SAND BEACH for the first ~9 units inland, then the land
    // climbs steadily into the hills — so there's a real beach ring at the shore.
    else          base = 2.4f * (1.0f - std::exp(-d / 6.0f)) + std::max(0.0f, d - 9.0f) * 0.17f;
    base += hill(x, z, 4.0f, 28.0f, 24.0f, 26.0f);   // main peak
    base += hill(x, z, -22.0f, 12.0f, 16.0f, 16.0f); // western shoulder
    base += hill(x, z, 26.0f, 36.0f, 15.0f, 15.0f);  // northern secondary summit
    // Roughness: rolling + RIDGED noise so the hills read as sculpted ground, not
    // a smooth dome. Scaled up with height so the low shore shelf stays flat
    // enough to build the town on, while the interior gets broken relief.
    const float landFrac = std::min(1.0f, std::max(0.0f, d / 8.0f));
    const float roughAmt = std::min(1.0f, std::max(0.0f, (base - 3.0f) / 9.0f));
    const float rolling = (fbm(x * 0.06f, z * 0.06f) - 0.5f) * 3.0f;
    const float ridged = (1.0f - std::fabs(2.0f * fbm(x * 0.11f, z * 0.11f) - 1.0f)) * 4.5f - 1.5f;
    return base + (rolling + ridged) * roughAmt * landFrac + (fbm(x * 0.05f, z * 0.05f) - 0.5f) * 1.5f * landFrac;
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
    shadow::bindRead(4);
    bgfx::setTransform(m);
    bgfx::setVertexBuffer(0, s_vbh);
    bgfx::setIndexBuffer(s_ibh);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z
                   | BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_MSAA); // no cull: seen from all sides
    bgfx::submit(viewId, s_prog);
}

} // namespace island_gpu
