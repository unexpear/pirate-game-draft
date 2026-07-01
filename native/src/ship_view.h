// Sea Trial — 3D scene view (Milestone 0+1). Public domain (Unlicense).
//
// Renders the animated Gerstner water surface (a wireframe grid displaced by
// the model's own sampleWater — so it matches the buoyancy math exactly) and
// the ship's pieces as solid oriented boxes, via bgfx's debugdraw (precompiled
// shaders — no shaderc pipeline needed yet; that arrives if/when water goes
// GPU). Orbit camera.
#pragma once

#include <cstdint>
#include <vector>

namespace sea { struct Ship; struct Wave; struct FloatPose; }

namespace ship_view {

void init();
void shutdown();

// Chase camera behind `heading`, scrolling ocean at the ship's virtual world
// position (worldX/worldZ), and the ship riding the surface at `pose`, yawed to
// `heading`. The sail trims toward `windDir` and is reefed to `sailFullness`
// (0 = furled .. 1 = full canvas). `timeSec` drives the wave animation.
void render(uint16_t viewId, const sea::Ship& ship, const std::vector<sea::Wave>& waves,
            const sea::FloatPose& pose, float timeSec, float heading,
            float worldX, float worldZ, float windDir, float sailFullness,
            int width, int height);

} // namespace ship_view
