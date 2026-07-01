// Sea Trial — lit ship meshes (Milestone 0+1). Public domain (Unlicense).
//
// Renders each hull piece as a lit box on the GPU shader pipeline (replacing
// the debugdraw wireframe/fill boxes). A single unit cube is reused; each piece
// is a scaled/rotated/translated instance under the ship's float pose.
#pragma once

#include <cstdint>

namespace sea { struct Ship; struct FloatPose; }

namespace ship_mesh {

void init();
void shutdown();

// Draw the ship on `viewId` (camera already set), riding at `pose`.
void render(uint16_t viewId, const sea::Ship& ship, const sea::FloatPose& pose);

} // namespace ship_mesh
