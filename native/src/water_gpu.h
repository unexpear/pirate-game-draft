// Sea Trial — GPU water (Milestone 0+1). Public domain (Unlicense).
//
// A solid, lit ocean surface: a dense flat grid displaced in the vertex shader
// by the same multi-frequency wave sum the CPU model uses (sea::sampleWater),
// so the drawn surface still matches the buoyancy math. Compiled shaders live
// in the build dir (bgfx_compile_shaders, D3D11 profile).
#pragma once

#include <cstdint>
#include <vector>

namespace sea { struct Wave; }

namespace water_gpu {

void init();
void shutdown();

// Submit the water surface on `viewId` (which must already have its camera set).
// `eye*` feeds the fresnel/specular term.
void render(uint16_t viewId, const std::vector<sea::Wave>& waves, float timeSec,
            float eyeX, float eyeY, float eyeZ);

} // namespace water_gpu
