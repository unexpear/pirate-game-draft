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

// Draw the ship on `viewId` (camera already set), riding at `pose`, yawed to
// `heading` (radians). The sail trims to the wind (`windDir`), luffs in irons
// (animated by `timeSec`), and reefs with `sailFullness` (0 = furled .. 1 = full
// canvas); the hull heels to leeward under sail. `posX`/`posZ` place the hull in
// the scene (0,0 = our ship at the scrolling-ocean origin).
void render(uint16_t viewId, const sea::Ship& ship, const sea::FloatPose& pose,
            float heading, float windDir, float sailFullness, float timeSec,
            float posX = 0.0f, float posZ = 0.0f);

// Draw a single lit unit-cube marker (e.g. a cannonball tracer) at world (x,y,z).
void renderBox(uint16_t viewId, float x, float y, float z, float size,
               float r, float g, float b);

// Draw a lit box with independent x/y/z dimensions at world (x,y,z) — the
// building block for scenery (land, docks, warehouses, shipyard).
void renderBoxSized(uint16_t viewId, float x, float y, float z,
                    float sx, float sy, float sz, float r, float g, float b);

// Draw a simple box-figure human standing at (x,y,z) (y = feet on the ground),
// facing `heading` (radians); `walkPhase` animates the stride when moving.
void renderCharacter(uint16_t viewId, float x, float y, float z,
                     float heading, float walkPhase);

} // namespace ship_mesh
