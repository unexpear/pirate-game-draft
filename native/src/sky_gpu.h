// Sea Trial — gradient sky backdrop. Public domain (Unlicense).
//
// A fullscreen vertical gradient (warm pale horizon -> deep navy zenith) drawn
// as the scene backdrop, so the sky reads as atmosphere instead of a dead flat
// clear colour. The horizon colour is shared with the water's distance fog so
// the sea dissolves into the sky at the horizon.
#pragma once

#include <cstdint>

namespace sky_gpu {

void init();
void shutdown();

// Draw the gradient sky on `viewId` (drawn behind everything via far depth).
void render(uint16_t viewId, const float top[3], const float horizon[3]);

} // namespace sky_gpu
