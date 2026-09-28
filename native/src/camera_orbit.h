// Sea Trial — mouse orbit rig. Public domain (Unlicense).
//
// Drag-orbit / scroll-zoom for the sail chase camera and the build-berth orbit.
// Pure math (no SDL, bgfx, or ImGui) so the defaults can be locked to the old
// fixed cameras: yaw 0 / default pitch / default distance must not move a
// --shot frame. Sail yaw is an offset from the ship's heading (0 = astern),
// which is the bearing a later camera-direction weapon pass can read.
#pragma once

#include <cmath>

namespace ship_cam {

struct Vec3 { float x, y, z; };
struct Pose { Vec3 eye; Vec3 at; };

struct Rig {
    float yaw;    // radians. Sail: offset from heading, 0 = chase from astern.
    float pitch;  // radians above the horizontal, looking down at the target.
    float dist;   // metres from the look-at point to the eye.
};

// Historical sail chase: eye 24 m astern at y = 9, looking at a point 5 m ahead
// at y = -0.4. The orbit is around that look-at, so the eye-to-target vector
// is (0, 9.4, -29) and the defaults below reproduce that pose exactly.
constexpr float kSailLookAhead = 5.0f;
constexpr float kSailLookY     = -0.4f;
constexpr float kSailHoriz     = 29.0f;
constexpr float kSailVert      = 9.4f;

// Historical build berth: eye on a 24 m ring at y = 12, looking at (0, 3.2, 0).
constexpr float kBuildAtY   = 3.2f;
constexpr float kBuildHoriz = 24.0f;
constexpr float kBuildVert  = 8.8f;

constexpr float kMinPitch = 0.05f;   // ~3 deg — just above the horizon
constexpr float kMaxPitch = 1.20f;   // ~69 deg — short of the straight-down flip
constexpr float kMinDist  = 12.0f;
constexpr float kMaxDist  = 80.0f;

constexpr float kYawSens   = 0.0055f; // radians per pixel; drag right orbits to port
constexpr float kPitchSens = 0.0040f; // drag up raises the eye
constexpr float kZoomRate  = 0.10f;   // fractional zoom per wheel notch; +wheel zooms in

// Sail eye stays at least this far above the water (look-at sits at y = -0.4,
// and the sea crests about a metre).
constexpr float kSailEyeMinY = 1.6f;

inline float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

inline float sailDefaultPitch() { return std::atan2(kSailVert, kSailHoriz); }
inline float sailDefaultDist()  { return std::sqrt(kSailHoriz * kSailHoriz + kSailVert * kSailVert); }
inline float buildDefaultPitch() { return std::atan2(kBuildVert, kBuildHoriz); }
inline float buildDefaultDist()  { return std::sqrt(kBuildHoriz * kBuildHoriz + kBuildVert * kBuildVert); }

inline Rig defaultSailRig()  { return { 0.0f, sailDefaultPitch(), sailDefaultDist() }; }
inline Rig defaultBuildRig() { return { 0.0f, buildDefaultPitch(), buildDefaultDist() }; }

// Close zoom on a long hull would put the eye inside the timbers. The 12 m
// sloop lands near 13 m; a 24 m hull near 19 m.
inline float minDistForLength(float lengthMeters) {
    const float d = lengthMeters * 0.45f + 8.0f;
    return d < kMinDist ? kMinDist : d;
}

inline float sailMinPitch(float dist) {
    const float d = dist < 1.0f ? 1.0f : dist;
    const float rise = kSailEyeMinY - kSailLookY;
    const float s = rise / d;
    float p = std::asin(s > 1.0f ? 1.0f : (s < 0.0f ? 0.0f : s));
    if (p < kMinPitch) p = kMinPitch;
    if (p > kMaxPitch) p = kMaxPitch;
    return p;
}

inline void wrapYaw(float& yaw) {
    const float pi = 3.14159265f;
    const float turn = 6.28318531f;
    while (yaw > pi) yaw -= turn;
    while (yaw <= -pi) yaw += turn;
}

inline void clampOrbit(Rig& r, bool keepAboveWater, float minDist) {
    const float lo = minDist > kMinDist ? minDist : kMinDist;
    const float hi = lo < kMaxDist ? kMaxDist : lo;
    r.dist = clampf(r.dist, lo, hi);
    const float minP = keepAboveWater ? sailMinPitch(r.dist) : kMinPitch;
    r.pitch = clampf(r.pitch, minP, kMaxPitch);
    wrapYaw(r.yaw);
}

// dx/dy are pixels, y growing downward. wheel notches, positive zooms in.
inline void applyOrbitInput(Rig& r, float dx, float dy, float wheel,
                            bool keepAboveWater, float minDist) {
    r.yaw += dx * kYawSens;
    r.pitch -= dy * kPitchSens;
    if (wheel != 0.0f) r.dist *= std::exp(-wheel * kZoomRate);
    clampOrbit(r, keepAboveWater, minDist);
}

// Rotate a ship-local pose into the scene. Heading 0: forward is +Z, starboard +X.
inline Pose toScene(const Pose& local, float heading) {
    const float fx = std::sin(heading), fz = std::cos(heading);
    const float rx = std::cos(heading), rz = -std::sin(heading);
    auto map = [&](Vec3 v) {
        return Vec3{ rx * v.x + fx * v.z, v.y, rz * v.x + fz * v.z };
    };
    return { map(local.eye), map(local.at) };
}

// Ship-local sail pose. Heading 0: forward is +Z, starboard is +X.
// yaw 0 reproduces eye (0, 9, -24), at (0, -0.4, 5).
inline Pose sailLocal(const Rig& r) {
    const float horiz = r.dist * std::cos(r.pitch);
    const float vert  = r.dist * std::sin(r.pitch);
    const float s = std::sin(r.yaw);
    const float c = std::cos(r.yaw);
    Pose p;
    p.at  = { s * kSailLookAhead, kSailLookY, c * kSailLookAhead };
    p.eye = { p.at.x - s * horiz, p.at.y + vert, p.at.z - c * horiz };
    return p;
}

// Build-berth orbit. angle 0 reproduces eye (0, 12, 24), at (0, 3.2, 0).
inline Pose buildLocal(float angle, float pitch, float dist) {
    const float horiz = dist * std::cos(pitch);
    const float vert  = dist * std::sin(pitch);
    Pose p;
    p.at  = { 0.0f, kBuildAtY, 0.0f };
    p.eye = { std::sin(angle) * horiz, kBuildAtY + vert, std::cos(angle) * horiz };
    return p;
}

} // namespace ship_cam
