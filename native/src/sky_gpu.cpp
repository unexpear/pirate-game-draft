// Sea Trial — gradient sky backdrop. Public domain (Unlicense).
#include "sky_gpu.h"

#include <bgfx/bgfx.h>

#include "dxbc/vs_sky.sc.bin.h" // vs_sky_dxbc[]
#include "dxbc/fs_sky.sc.bin.h" // fs_sky_dxbc[]

namespace {

struct SkyVertex { float x, y, z; };
// A single fullscreen triangle in clip space (covers [-1,1] with margin).
const SkyVertex kTri[3] = { { -1.0f, -1.0f, 0.0f }, { 3.0f, -1.0f, 0.0f }, { -1.0f, 3.0f, 0.0f } };

bgfx::VertexLayout s_layout;
bgfx::VertexBufferHandle s_vbh = BGFX_INVALID_HANDLE;
bgfx::ProgramHandle s_prog = BGFX_INVALID_HANDLE;
bgfx::UniformHandle u_skyTop = BGFX_INVALID_HANDLE;
bgfx::UniformHandle u_skyHorizon = BGFX_INVALID_HANDLE;

} // namespace

namespace sky_gpu {

void init() {
    s_layout.begin().add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float).end();
    s_vbh = bgfx::createVertexBuffer(bgfx::copy(kTri, sizeof(kTri)), s_layout);
    s_prog = bgfx::createProgram(
        bgfx::createShader(bgfx::copy(vs_sky_dxbc, sizeof(vs_sky_dxbc))),
        bgfx::createShader(bgfx::copy(fs_sky_dxbc, sizeof(fs_sky_dxbc))),
        true);
    u_skyTop = bgfx::createUniform("u_skyTop", bgfx::UniformType::Vec4);
    u_skyHorizon = bgfx::createUniform("u_skyHorizon", bgfx::UniformType::Vec4);
}

void shutdown() {
    if (bgfx::isValid(u_skyHorizon)) { bgfx::destroy(u_skyHorizon); u_skyHorizon = BGFX_INVALID_HANDLE; }
    if (bgfx::isValid(u_skyTop)) { bgfx::destroy(u_skyTop); u_skyTop = BGFX_INVALID_HANDLE; }
    if (bgfx::isValid(s_prog)) { bgfx::destroy(s_prog); s_prog = BGFX_INVALID_HANDLE; }
    if (bgfx::isValid(s_vbh)) { bgfx::destroy(s_vbh); s_vbh = BGFX_INVALID_HANDLE; }
}

void render(uint16_t viewId, const float top[3], const float horizon[3]) {
    const float t[4] = { top[0], top[1], top[2], 1.0f };
    const float h[4] = { horizon[0], horizon[1], horizon[2], 1.0f };
    bgfx::setUniform(u_skyTop, t);
    bgfx::setUniform(u_skyHorizon, h);
    bgfx::setVertexBuffer(0, s_vbh);
    // Far-plane depth + LESS test: draws behind everything, order-independent.
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS);
    bgfx::submit(viewId, s_prog);
}

} // namespace sky_gpu
