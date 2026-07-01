// Sea Trial — GPU water (Milestone 0+1). Public domain (Unlicense).
#include "water_gpu.h"

#include "ship_model.hpp" // sea::Wave

#include <bgfx/bgfx.h>

#include <cmath>

// Compiled by bgfx_compile_shaders (D3D11 / dxbc profile) into the build dir.
#include "dxbc/vs_water.sc.bin.h" // vs_water_dxbc[]
#include "dxbc/fs_water.sc.bin.h" // fs_water_dxbc[]

namespace {

struct PosVertex { float x, y, z; };

constexpr int kCells = 120;       // grid resolution (GPU can afford dense)
constexpr float kExtent = 120.0f; // world size of the surface

bgfx::VertexLayout s_layout;
bgfx::VertexBufferHandle s_vbh = BGFX_INVALID_HANDLE;
bgfx::IndexBufferHandle s_ibh = BGFX_INVALID_HANDLE;
bgfx::ProgramHandle s_program = BGFX_INVALID_HANDLE;
bgfx::UniformHandle u_waveA = BGFX_INVALID_HANDLE;
bgfx::UniformHandle u_waveB = BGFX_INVALID_HANDLE;
bgfx::UniformHandle u_waveTime = BGFX_INVALID_HANDLE;
bgfx::UniformHandle u_waveOffset = BGFX_INVALID_HANDLE;
bgfx::UniformHandle u_lightDir = BGFX_INVALID_HANDLE;
bgfx::UniformHandle u_camPos = BGFX_INVALID_HANDLE;

void destroyIfValid(bgfx::UniformHandle& h) { if (bgfx::isValid(h)) { bgfx::destroy(h); h = BGFX_INVALID_HANDLE; } }

} // namespace

namespace water_gpu {

void init() {
    s_layout.begin().add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float).end();

    const int vn = kCells + 1;
    std::vector<PosVertex> verts(size_t(vn) * vn);
    for (int r = 0; r < vn; ++r) {
        for (int c = 0; c < vn; ++c) {
            const float x = (c / float(kCells) - 0.5f) * kExtent;
            const float z = (r / float(kCells) - 0.5f) * kExtent;
            verts[size_t(r) * vn + c] = { x, 0.0f, z };
        }
    }
    std::vector<uint32_t> idx;
    idx.reserve(size_t(kCells) * kCells * 6);
    for (int r = 0; r < kCells; ++r) {
        for (int c = 0; c < kCells; ++c) {
            const uint32_t a = uint32_t(r * vn + c);
            const uint32_t b = uint32_t(r * vn + c + 1);
            const uint32_t d = uint32_t((r + 1) * vn + c);
            const uint32_t e = uint32_t((r + 1) * vn + c + 1);
            idx.push_back(a); idx.push_back(d); idx.push_back(b);
            idx.push_back(b); idx.push_back(d); idx.push_back(e);
        }
    }

    s_vbh = bgfx::createVertexBuffer(bgfx::copy(verts.data(), uint32_t(verts.size() * sizeof(PosVertex))), s_layout);
    s_ibh = bgfx::createIndexBuffer(bgfx::copy(idx.data(), uint32_t(idx.size() * sizeof(uint32_t))), BGFX_BUFFER_INDEX32);

    s_program = bgfx::createProgram(
        bgfx::createShader(bgfx::copy(vs_water_dxbc, sizeof(vs_water_dxbc))),
        bgfx::createShader(bgfx::copy(fs_water_dxbc, sizeof(fs_water_dxbc))),
        true);

    u_waveA = bgfx::createUniform("u_waveA", bgfx::UniformType::Vec4, 3);
    u_waveB = bgfx::createUniform("u_waveB", bgfx::UniformType::Vec4, 3);
    u_waveTime = bgfx::createUniform("u_waveTime", bgfx::UniformType::Vec4);
    u_waveOffset = bgfx::createUniform("u_waveOffset", bgfx::UniformType::Vec4);
    u_lightDir = bgfx::createUniform("u_lightDir", bgfx::UniformType::Vec4);
    u_camPos = bgfx::createUniform("u_camPos", bgfx::UniformType::Vec4);
}

void shutdown() {
    destroyIfValid(u_waveA);
    destroyIfValid(u_waveB);
    destroyIfValid(u_waveTime);
    destroyIfValid(u_waveOffset);
    destroyIfValid(u_lightDir);
    destroyIfValid(u_camPos);
    if (bgfx::isValid(s_program)) { bgfx::destroy(s_program); s_program = BGFX_INVALID_HANDLE; }
    if (bgfx::isValid(s_ibh)) { bgfx::destroy(s_ibh); s_ibh = BGFX_INVALID_HANDLE; }
    if (bgfx::isValid(s_vbh)) { bgfx::destroy(s_vbh); s_vbh = BGFX_INVALID_HANDLE; }
}

void render(uint16_t viewId, const std::vector<sea::Wave>& waves, float t,
            float eyeX, float eyeY, float eyeZ, float offsetX, float offsetZ) {
    float waveA[3][4] = {};
    float waveB[3][4] = {};
    const int n = int(waves.size() < 3 ? waves.size() : 3);
    for (int i = 0; i < n; ++i) {
        const sea::Wave& w = waves[size_t(i)];
        waveA[i][0] = std::cos(float(w.direction));
        waveA[i][1] = std::sin(float(w.direction));
        waveA[i][2] = float(w.amplitude);
        waveA[i][3] = float(w.wavelength);
        waveB[i][0] = float(w.speed);
        waveB[i][1] = float(w.phase);
    }
    bgfx::setUniform(u_waveA, waveA, 3);
    bgfx::setUniform(u_waveB, waveB, 3);
    const float timeV[4] = { t, 0.0f, 0.0f, 0.0f };
    bgfx::setUniform(u_waveTime, timeV);
    const float offV[4] = { offsetX, offsetZ, 0.0f, 0.0f };
    bgfx::setUniform(u_waveOffset, offV);
    const float lightV[4] = { 0.4f, 0.85f, 0.35f, 0.0f };
    bgfx::setUniform(u_lightDir, lightV);
    const float camV[4] = { eyeX, eyeY, eyeZ, 0.0f };
    bgfx::setUniform(u_camPos, camV);

    bgfx::setVertexBuffer(0, s_vbh);
    bgfx::setIndexBuffer(s_ibh);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z
                   | BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_MSAA);
    bgfx::submit(viewId, s_program);
}

} // namespace water_gpu
