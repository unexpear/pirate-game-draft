// Sea Trial — shadow cascade focus (pure math, no GPU). Public domain (Unlicense).
#include "shadow_focus.h"

#include <cmath>

namespace shadow {
namespace {

struct V3 { float x, y, z; };

V3 vnormalize(V3 v) {
    const float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (len < 1.0e-8f) return { 0.0f, 1.0f, 0.0f };
    return { v.x / len, v.y / len, v.z / len };
}

V3 vcross(V3 a, V3 b) {
    return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}

float vdot3(const float a[3], float x, float y, float z) {
    return a[0] * x + a[1] * y + a[2] * z;
}

} // namespace

float texelMeters(float radius) {
    return (2.0f * radius) / float(kCascadeResolution);
}

LightBasis lightBasis(float sunX, float sunY, float sunZ) {
    const V3 sun = vnormalize({ sunX, sunY, sunZ });
    const V3 view = { -sun.x, -sun.y, -sun.z };          // at - eye, left-handed
    const V3 right = vnormalize(vcross({ 0.0f, 1.0f, 0.0f }, view));
    const V3 up = vcross(view, right);
    LightBasis b{};
    b.right[0] = right.x; b.right[1] = right.y; b.right[2] = right.z;
    b.up[0] = up.x;       b.up[1] = up.y;       b.up[2] = up.z;
    b.view[0] = view.x;   b.view[1] = view.y;   b.view[2] = view.z;
    return b;
}

FrameFocus planFrame(float islandRelX, float islandRelZ, float isoRadius) {
    FrameFocus f;
    if (isoRadius > 0.0f) {
        f.ship = { 0.0f, 0.0f, isoRadius, true };
        f.island = { 0.0f, 0.0f, 0.0f, false };
        f.townInShipMap = true;   // the one building is the ship map's caster
        f.hullsInIslandMap = false;
        return f;
    }
    f.ship = { 0.0f, 0.0f, kShipRadius, true };
    const float dist = std::sqrt(islandRelX * islandRelX + islandRelZ * islandRelZ);
    const bool visible = dist <= kIslandVisibleRange;
    f.island = { islandRelX, islandRelZ, kIslandRadius, visible };
    const float reach = kShipRadius + kIslandRadius + kCasterMargin;
    f.townInShipMap = visible && dist <= reach;
    f.hullsInIslandMap = visible && dist <= kShipRadius + kIslandRadius;
    return f;
}

bool inMap(const LightBasis& basis, const MapFocus& map, float x, float y, float z) {
    if (!map.active || map.radius <= 0.0f) return false;
    const float dx = x - map.cx;
    const float dy = y;
    const float dz = z - map.cz;
    const float lx = vdot3(basis.right, dx, dy, dz);
    const float ly = vdot3(basis.up, dx, dy, dz);
    if (std::fabs(lx) > map.radius || std::fabs(ly) > map.radius) return false;
    const float eyeDist = map.radius * kLightDistScale;
    const float lz = eyeDist + vdot3(basis.view, dx, dy, dz);
    const float farPlane = eyeDist * 2.0f;
    return lz >= 0.1f && lz <= farPlane;
}

} // namespace shadow
