// Sea Trial — shadow-mapped cast shadows. Public domain (Unlicense).
//
// Two directional shadow maps (see shadow_focus.h for the split). The sun
// renders caster depth into an R32F target from an orthographic view; the main
// pass compares each fragment (3×3 PCF) and darkens what the casters occlude.
#pragma once

#include <cstdint>

namespace bgfx { struct ProgramHandle; }

namespace shadow {

void init(int size);
void shutdown();

// Drop per-frame cascade state. Call once before beginCascade.
void prepareFrame();

// Set cascade 0 (ship) or 1 (island): orthographic sun view over (cx, cz) with
// the given radius, target that cascade's framebuffer, and clear it. Stores the
// light view-proj for the main pass. Call before submitting casters.
void beginCascade(int index, uint16_t viewId, float sunX, float sunY, float sunZ,
                  float cx, float cz, float radius);

// The depth-only program to submit casters with (to a shadow view).
bgfx::ProgramHandle program();

// Bind both shadow maps + light matrices for a RECEIVING draw. `stage` is the
// register of cascade 0 (the shaders pin it to 4; cascade 1 uses stage+1).
void bindRead(uint8_t stage);

// True once init() has created the resources (so callers can no-op before then).
bool ready();

} // namespace shadow
