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

// Set up the camera on `viewId`, draw the water grid, and draw the ship riding
// the surface at `pose`. `timeSec` drives both the wave animation and the orbit.
void render(uint16_t viewId, const sea::Ship& ship, const std::vector<sea::Wave>& waves,
            const sea::FloatPose& pose, float timeSec, int width, int height);

} // namespace ship_view
