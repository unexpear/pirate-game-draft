// Sea Trial — shadow-mapped cast shadows. Public domain (Unlicense).
#include "shadow_gpu.h"

#include <bgfx/bgfx.h>
#include <bx/math.h>

#include "dxbc/vs_shadow.sc.bin.h" // vs_shadow_dxbc[]
#include "dxbc/fs_shadow.sc.bin.h" // fs_shadow_dxbc[]

namespace {

int s_size = 0;
bgfx::TextureHandle s_colorTex = BGFX_INVALID_HANDLE;
bgfx::TextureHandle s_depthTex = BGFX_INVALID_HANDLE;
bgfx::FrameBufferHandle s_fb = BGFX_INVALID_HANDLE;
bgfx::ProgramHandle s_prog = BGFX_INVALID_HANDLE;
bgfx::UniformHandle s_shadowMap = BGFX_INVALID_HANDLE;
bgfx::UniformHandle u_lightMtx = BGFX_INVALID_HANDLE;
float s_lightViewProj[16];

} // namespace

namespace shadow {

void init(int size) {
    s_size = size;
    const uint64_t rtFlags = BGFX_TEXTURE_RT | BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT
                           | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
    s_colorTex = bgfx::createTexture2D(uint16_t(size), uint16_t(size), false, 1, bgfx::TextureFormat::R32F, rtFlags);
    s_depthTex = bgfx::createTexture2D(uint16_t(size), uint16_t(size), false, 1, bgfx::TextureFormat::D16, BGFX_TEXTURE_RT);
    bgfx::TextureHandle atts[2] = { s_colorTex, s_depthTex };
    s_fb = bgfx::createFrameBuffer(2, atts, true);
    s_prog = bgfx::createProgram(
        bgfx::createShader(bgfx::copy(vs_shadow_dxbc, sizeof(vs_shadow_dxbc))),
        bgfx::createShader(bgfx::copy(fs_shadow_dxbc, sizeof(fs_shadow_dxbc))),
        true);
    s_shadowMap = bgfx::createUniform("s_shadowMap", bgfx::UniformType::Sampler);
    u_lightMtx = bgfx::createUniform("u_lightMtx", bgfx::UniformType::Mat4);
    bx::mtxIdentity(s_lightViewProj);
}

void shutdown() {
    if (bgfx::isValid(u_lightMtx)) { bgfx::destroy(u_lightMtx); u_lightMtx = BGFX_INVALID_HANDLE; }
    if (bgfx::isValid(s_shadowMap)) { bgfx::destroy(s_shadowMap); s_shadowMap = BGFX_INVALID_HANDLE; }
    if (bgfx::isValid(s_prog)) { bgfx::destroy(s_prog); s_prog = BGFX_INVALID_HANDLE; }
    if (bgfx::isValid(s_fb)) { bgfx::destroy(s_fb); s_fb = BGFX_INVALID_HANDLE; }
    s_colorTex = BGFX_INVALID_HANDLE; // owned by the framebuffer (destroyed with it)
    s_depthTex = BGFX_INVALID_HANDLE;
    s_size = 0;
}

bool ready() { return bgfx::isValid(s_fb); }

void beginPass(uint16_t viewId, float sunX, float sunY, float sunZ,
               float cx, float cz, float radius) {
    const bx::Vec3 sun = bx::normalize({ sunX, sunY, sunZ }); // direction TO the sun
    const float dist = radius * 2.5f;
    const bx::Vec3 at = { cx, 0.0f, cz };
    const bx::Vec3 eye = { cx + sun.x * dist, sun.y * dist, cz + sun.z * dist };
    const bx::Vec3 up = { 0.0f, 1.0f, 0.0f };
    float view[16];
    bx::mtxLookAt(view, eye, at, up);
    float proj[16];
    const bgfx::Caps* caps = bgfx::getCaps();
    bx::mtxOrtho(proj, -radius, radius, -radius, radius, 0.1f, dist * 2.0f, 0.0f, caps->homogeneousDepth);
    bx::mtxMul(s_lightViewProj, view, proj); // usable as mul(mtx, worldPos) in the FS

    bgfx::setViewFrameBuffer(viewId, s_fb);
    bgfx::setViewRect(viewId, 0, 0, uint16_t(s_size), uint16_t(s_size));
    bgfx::setViewClear(viewId, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0xffffffff, 1.0f, 0);
    bgfx::setViewTransform(viewId, view, proj);
}

bgfx::ProgramHandle program() { return s_prog; }

void bindRead(uint8_t stage) {
    if (!bgfx::isValid(s_fb)) return;
    bgfx::setUniform(u_lightMtx, s_lightViewProj);
    bgfx::setTexture(stage, s_shadowMap, s_colorTex);
}

} // namespace shadow
