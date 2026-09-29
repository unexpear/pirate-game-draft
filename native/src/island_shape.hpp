// Sea Trial — island coastline and height, pure C++. Public domain (Unlicense).
//
// The same field the terrain mesh is baked from. Swimming and the beach walk
// read it directly so the sand you climb out onto is the sand you see.
// island_gpu::heightAt is a wrapper around heightAt() here — keep them one
// function, or the shore and the mesh drift apart.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace island_shape {

constexpr float kMeanCoast = 56.0f; // mean above-water radius

inline float hash01(int x, int y) {
    uint32_t h = uint32_t(x) * 374761393u + uint32_t(y) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return float(h & 0xFFFFFFu) / float(0x1000000);
}

inline float valueNoise(float x, float y) {
    const float fxi = std::floor(x), fyi = std::floor(y);
    const int xi = int(fxi), yi = int(fyi);
    const float fx = x - fxi, fy = y - fyi;
    const float u = fx * fx * (3.0f - 2.0f * fx);
    const float v = fy * fy * (3.0f - 2.0f * fy);
    const float a = hash01(xi, yi), b = hash01(xi + 1, yi);
    const float c = hash01(xi, yi + 1), d = hash01(xi + 1, yi + 1);
    return a * (1 - u) * (1 - v) + b * u * (1 - v) + c * (1 - u) * v + d * u * v;
}

inline float fbm(float x, float y) {
    return 0.6f * valueNoise(x, y) + 0.3f * valueNoise(x * 2.03f, y * 2.03f)
         + 0.1f * valueNoise(x * 4.01f, y * 4.01f);
}

inline float hill(float x, float z, float cx, float cz, float sig, float amp) {
    const float dx = x - cx, dz = z - cz;
    return amp * std::exp(-(dx * dx + dz * dz) / (2.0f * sig * sig));
}

// Irregular coastline radius (metres) at island-local (x, z). Clamped so the
// shore stays outside the water-cut radius (no dry moat).
inline float coastRadius(float x, float z) {
    const float ang = std::atan2(z, x);
    float coastR = kMeanCoast + 6.0f * std::sin(3.0f * ang + 0.5f)
                 + 4.0f * std::sin(5.0f * ang + 2.0f)
                 + 3.0f * std::cos(2.0f * ang - 1.0f)
                 + 4.0f * std::sin(7.0f * ang + 1.3f)
                 + (fbm(std::cos(ang) * 2.6f + 4.0f, std::sin(ang) * 2.6f + 7.0f) - 0.5f) * 9.0f;
    coastR += 12.0f * std::max(0.0f, std::sin(ang)); // grow northward, away from the harbour
    return std::max(48.0f, coastR);
}

// Metres inland of the waterline. Positive on the land, negative offshore.
inline float metresInland(float x, float z) {
    const float r = std::sqrt(x * x + z * z);
    return coastRadius(x, z) - r;
}

// Ground height (metres) at island-local (x, z). Negative on the submerged skirt.
inline float heightAt(float x, float z) {
    const float d = metresInland(x, z);
    float base;
    if (d < 0.0f) base = d * 0.55f;                                       // submerged skirt
    else          base = 2.2f * (1.0f - std::exp(-d / 7.0f)) + std::max(0.0f, d - 10.0f) * 0.10f;
    base += hill(x, z, 6.0f, 34.0f, 30.0f, 15.0f);   // main peak
    base += hill(x, z, -26.0f, 16.0f, 22.0f, 9.0f);  // western shoulder
    base += hill(x, z, 32.0f, 44.0f, 24.0f, 9.0f);   // northern secondary summit
    const float landFrac = std::min(1.0f, std::max(0.0f, d / 9.0f));
    const float roughAmt = std::min(1.0f, std::max(0.0f, (base - 3.0f) / 12.0f));
    const float rolling = (fbm(x * 0.055f, z * 0.055f) - 0.5f) * 2.2f;
    const float ridged = (1.0f - std::fabs(2.0f * fbm(x * 0.10f, z * 0.10f) - 1.0f)) * 2.6f - 0.9f;
    float h = base + (rolling + ridged) * roughAmt * landFrac + (fbm(x * 0.05f, z * 0.05f) - 0.5f) * 1.2f * landFrac;
    struct Pad { float x0, z0, x1, z1, level, margin; };
    static const Pad kPads[] = {
        { 46.0f, -17.0f, 68.0f, 13.0f, 3.0f, 7.0f },   // shipyard pad
    };
    for (const Pad& p : kPads) {
        const float dx = std::max(std::max(p.x0 - x, 0.0f), x - p.x1);
        const float dz = std::max(std::max(p.z0 - z, 0.0f), z - p.z1);
        const float t = std::min(1.0f, std::sqrt(dx * dx + dz * dz) / p.margin);
        const float w = 1.0f - t * t * (3.0f - 2.0f * t);
        h = h + (p.level - h) * w;
    }
    return h;
}

} // namespace island_shape
