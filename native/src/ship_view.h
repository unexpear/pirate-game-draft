// Sea Trial — 3D ship view (Milestone 0+1). Public domain (Unlicense).
//
// Renders a ship's pieces as solid oriented boxes via bgfx's debugdraw
// (precompiled lit shaders — no shaderc pipeline needed yet; that arrives with
// water). Orbit camera. This is the first step of turning the data model into
// something you can look at.
#pragma once

#include <cstdint>

namespace sea { struct Ship; }

namespace ship_view {

void init();
void shutdown();

// Set up the camera on `viewId` and draw the ship. `timeSec` drives the orbit.
void render(uint16_t viewId, const sea::Ship& ship, float timeSec, int width, int height);

} // namespace ship_view
