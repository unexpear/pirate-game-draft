// Sea Trial — shadow-mapped cast shadows. Public domain (Unlicense).
#include "shadow_gpu.h"

#include "shadow_focus.h"

#include <bgfx/bgfx.h>
#include <bx/math.h>

#include "dxbc/vs_shadow.sc.bin.h" // vs_shadow_dxbc[]
#include "dxbc/fs_shadow.sc.bin.h" // fs_shadow_dxbc[]

namespace {

constexpr int kCascades = 2;

int s_size = 0;
bgfx::TextureHandle s_colorTex[kCascades] = { BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE };
bgfx::TextureHandle s_depthTex[kCascades] = { BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE };
bgfx::FrameBufferHandle s_fb[kCascades] = { BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE };
bgfx::ProgramHandle s_prog = BGFX_INVALID_HANDLE;
bgfx::UniformHandle s_shadowMap = BGFX_INVALID_HANDLE;
bgfx::UniformHandle s_shadowMapFar = BGFX_INVALID_HANDLE;
bgfx::UniformHandle u_lightMtx0 = BGFX_INVALID_HANDLE;
bgfx::UniformHandle u_lightMtx1 = BGFX_INVALID_HANDLE;
bgfx::UniformHandle u_shadowParams = BGFX_INVALID_HANDLE;
float s_lightViewProj[kCascades][16];
bool s_active[kCascades] = { false, false };

void destroyUniform(bgfx::UniformHandle& h) {
    if (bgfx::isValid(h)) { bgfx::destroy(h); h = BGFX_INVALID_HANDLE; }
}

} // namespace

namespace shadow {

void init(int size) {
    s_size = size;
    const uint64_t rtFlags = BGFX_TEXTURE_RT | BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT
                           | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
    for (int i = 0; i < kCascades; ++i) {
        s_colorTex[i] = bgfx::createTexture2D(uint16_t(size), uint16_t(size), false, 1,
                                              bgfx::TextureFormat::R32F, rtFlags);
        s_depthTex[i] = bgfx::createTexture2D(uint16_t(size), uint16_t(size), false, 1,
                                              bgfx::TextureFormat::D16, BGFX_TEXTURE_RT);
        bgfx::TextureHandle atts[2] = { s_colorTex[i], s_depthTex[i] };
        s_fb[i] = bgfx::createFrameBuffer(2, atts, true);
        bx::mtxIdentity(s_lightViewProj[i]);
        s_active[i] = false;
    }
    s_prog = bgfx::createProgram(
        bgfx::createShader(bgfx::copy(vs_shadow_dxbc, sizeof(vs_shadow_dxbc))),
        bgfx::createShader(bgfx::copy(fs_shadow_dxbc, sizeof(fs_shadow_dxbc))),
        true);
    s_shadowMap = bgfx::createUniform("s_shadowMap", bgfx::UniformType::Sampler);
    s_shadowMapFar = bgfx::createUniform("s_shadowMapFar", bgfx::UniformType::Sampler);
    u_lightMtx0 = bgfx::createUniform("u_lightMtx0", bgfx::UniformType::Mat4);
    u_lightMtx1 = bgfx::createUniform("u_lightMtx1", bgfx::UniformType::Mat4);
    u_shadowParams = bgfx::createUniform("u_shadowParams", bgfx::UniformType::Vec4);
}

void shutdown() {
    destroyUniform(u_shadowParams);
    destroyUniform(u_lightMtx1);
    destroyUniform(u_lightMtx0);
    destroyUniform(s_shadowMapFar);
    destroyUniform(s_shadowMap);
    if (bgfx::isValid(s_prog)) { bgfx::destroy(s_prog); s_prog = BGFX_INVALID_HANDLE; }
    for (int i = 0; i < kCascades; ++i) {
        if (bgfx::isValid(s_fb[i])) { bgfx::destroy(s_fb[i]); s_fb[i] = BGFX_INVALID_HANDLE; }
        s_colorTex[i] = BGFX_INVALID_HANDLE; // owned by the framebuffer
        s_depthTex[i] = BGFX_INVALID_HANDLE;
        s_active[i] = false;
    }
    s_size = 0;
}

bool ready() { return bgfx::isValid(s_fb[0]) && bgfx::isValid(s_fb[1]); }

void prepareFrame() {
    s_active[0] = false;
    s_active[1] = false;
}

void beginCascade(int index, uint16_t viewId, float sunX, float sunY, float sunZ,
                  float cx, float cz, float radius) {
    if (index < 0 || index >= kCascades || !bgfx::isValid(s_fb[index]) || radius <= 0.0f) return;
    const bx::Vec3 sun = bx::normalize({ sunX, sunY, sunZ }); // direction TO the sun
    const float dist = radius * kLightDistScale;
    const bx::Vec3 at = { cx, 0.0f, cz };
    const bx::Vec3 eye = { cx + sun.x * dist, sun.y * dist, cz + sun.z * dist };
    const bx::Vec3 up = { 0.0f, 1.0f, 0.0f };
    float view[16];
    bx::mtxLookAt(view, eye, at, up);
    float proj[16];
    const bgfx::Caps* caps = bgfx::getCaps();
    bx::mtxOrtho(proj, -radius, radius, -radius, radius, 0.1f, dist * 2.0f, 0.0f, caps->homogeneousDepth);
    bx::mtxMul(s_lightViewProj[index], view, proj); // usable as mul(mtx, worldPos) in the FS

    bgfx::setViewName(viewId, index == 0 ? "ShadowShip" : "ShadowIsland");
    bgfx::setViewFrameBuffer(viewId, s_fb[index]);
    bgfx::setViewRect(viewId, 0, 0, uint16_t(s_size), uint16_t(s_size));
    bgfx::setViewClear(viewId, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0xffffffff, 1.0f, 0);
    bgfx::setViewTransform(viewId, view, proj);
    s_active[index] = true;
}

bgfx::ProgramHandle program() { return s_prog; }

void bindRead(uint8_t stage) {
    if (!bgfx::isValid(s_fb[0])) return;
    bgfx::setUniform(u_lightMtx0, s_lightViewProj[0]);
    bgfx::setUniform(u_lightMtx1, s_lightViewProj[1]);
    // z matches the old constant bias (0.0006). y is the shared texel: both
    // cascades are the same resolution, so one 3×3 kernel fits both.
    const float params[4] = {
        s_active[1] ? 1.0f : 0.0f,
        s_size > 0 ? 1.0f / float(s_size) : 1.0f / 2048.0f,
        0.0006f,
        0.0f
    };
    bgfx::setUniform(u_shadowParams, params);
    bgfx::setTexture(stage, s_shadowMap, s_colorTex[0]);
    bgfx::setTexture(uint8_t(stage + 1), s_shadowMapFar, s_colorTex[1]);
}

} // namespace shadow
