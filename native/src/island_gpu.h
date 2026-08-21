// Sea Trial — GPU island terrain. Public domain (Unlicense).
//
// A real island instead of stacked boxes: a procedural heightfield baked once at
// init — an irregular coastline, a beach that slopes up from the waterline, a
// grassy interior and rocky heights, coloured per-vertex and lit like the rest
// of the scene. It rises above the sea (so it OCCLUDES the water) and dips below
// it at the edges (a submerged shelf under the waves).
#pragma once

#include <cstdint>

namespace island_gpu {

void init();
void shutdown();

// The mean above-water radius of the land (structures/collision reference).
float landRadius();

// Draw the island with its centre at scene (relX,relZ) relative to the camera's
// ship at the origin. The heightfield is static; only the translation changes.
void render(uint16_t viewId, float relX, float relZ);

// Ground height (feet) at island-local (lx,lz) — for seating structures on the
// terrain instead of guessing a flat y.
float heightAt(float lx, float lz);

} // namespace island_gpu
