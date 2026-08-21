// Sea Trial — shadow-mapped cast shadows. Public domain (Unlicense).
//
// A directional-light shadow map focused on the ship: the sun renders the
// casters' depth into an R32F target from an orthographic view, and the main
// pass compares each fragment's light-space depth against it (PCF) to darken
// what the ship occludes — real projected shadows on the sea, plus self-shadow.
#pragma once

#include <cstdint>

namespace bgfx { struct ProgramHandle; }

namespace shadow {

void init(int size);
void shutdown();

// Set the shadow view's camera to the sun's orthographic view over (cx,cz) with
// the given radius, target the shadow framebuffer, and clear it. Stores the
// light view-proj for the main pass. Call before submitting casters.
void beginPass(uint16_t viewId, float sunX, float sunY, float sunZ,
               float cx, float cz, float radius);

// The depth-only program to submit casters with (to the shadow view).
bgfx::ProgramHandle program();

// Bind the shadow map + light matrix for a RECEIVING draw (call before submit).
void bindRead(uint8_t stage);

// True once init() has created the resources (so callers can no-op before then).
bool ready();

} // namespace shadow
