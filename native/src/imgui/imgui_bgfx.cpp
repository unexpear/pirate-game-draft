// Minimal self-contained Dear ImGui (v1.92) renderer for bgfx. Public domain.
//
// The bgfx calls (texture protocol, transient buffers, embedded shaders, view
// setup) are adapted from bgfx's own example ImGui backend, which targets this
// exact ImGui version — but here it's decoupled from the example framework and
// driven by stock Dear ImGui + app-fed input.
#include "imgui_bgfx.h"

#include <bgfx/bgfx.h>
#include <bgfx/embedded_shader.h>
#include <bx/math.h>

#include <imgui.h>

#include "vs_ocornut_imgui.bin.h"
#include "fs_ocornut_imgui.bin.h"

#include <cstring>

namespace {

constexpr uint8_t FLAGS_ALPHA_BLEND = 0x01;

const bgfx::EmbeddedShader s_embeddedShaders[] = {
    BGFX_EMBEDDED_SHADER(vs_ocornut_imgui),
    BGFX_EMBEDDED_SHADER(fs_ocornut_imgui),
    BGFX_EMBEDDED_SHADER_END()
};

// 8-byte reference to a bgfx texture, stored in an ImTextureID (ImU64).
struct TextureBgfx {
    bgfx::TextureHandle handle;
    uint8_t flags;
    uint8_t mip;
    uint32_t unused;
};

template <typename To, typename From>
To bitCast(const From& from) {
    static_assert(sizeof(To) == sizeof(From), "bitCast size mismatch");
    To to;
    std::memcpy(&to, &from, sizeof(To));
    return to;
}

struct Context {
    ImGuiContext* imgui = nullptr;
    bgfx::VertexLayout layout;
    bgfx::ProgramHandle program = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle texUniform = BGFX_INVALID_HANDLE;
} g;

// ImGui 1.92 dynamic-texture protocol: create / update / destroy GPU textures
// as ImGui requests them (the font atlas is one of these).
void syncTextures(ImDrawData* drawData) {
    if (drawData->Textures == nullptr) return;
    for (ImTextureData* tex : *drawData->Textures) {
        switch (tex->Status) {
        case ImTextureStatus_WantCreate: {
            bgfx::TextureHandle h = bgfx::createTexture2D(
                (uint16_t)tex->Width, (uint16_t)tex->Height, false, 1, bgfx::TextureFormat::BGRA8, 0);
            bgfx::setName(h, "ImGui Texture");
            bgfx::updateTexture2D(h, 0, 0, 0, 0, (uint16_t)tex->Width, (uint16_t)tex->Height,
                bgfx::copy(tex->GetPixels(), tex->GetSizeInBytes()));
            TextureBgfx t{ h, FLAGS_ALPHA_BLEND, 0, 0 };
            tex->SetTexID(bitCast<ImTextureID>(t));
            tex->SetStatus(ImTextureStatus_OK);
            break;
        }
        case ImTextureStatus_WantUpdates: {
            TextureBgfx t = bitCast<TextureBgfx>(tex->GetTexID());
            const int bpp = tex->BytesPerPixel;
            const int srcPitch = tex->GetPitch();
            for (ImTextureRect& r : tex->Updates) {
                const int dstPitch = r.w * bpp;
                const bgfx::Memory* mem = bgfx::alloc(uint32_t(r.h * dstPitch));
                const uint8_t* src = (const uint8_t*)tex->GetPixelsAt(r.x, r.y);
                for (int row = 0; row < r.h; ++row)
                    std::memcpy(mem->data + row * dstPitch, src + row * srcPitch, dstPitch);
                bgfx::updateTexture2D(t.handle, 0, 0, r.x, r.y, r.w, r.h, mem);
            }
            break;
        }
        case ImTextureStatus_WantDestroy: {
            TextureBgfx t = bitCast<TextureBgfx>(tex->GetTexID());
            if (bgfx::isValid(t.handle)) bgfx::destroy(t.handle);
            tex->SetTexID(ImTextureID_Invalid);
            tex->SetStatus(ImTextureStatus_Destroyed);
            break;
        }
        default:
            break;
        }
    }
}

void renderDrawData(ImDrawData* drawData, uint16_t viewId) {
    const int fbWidth = (int)(drawData->DisplaySize.x * drawData->FramebufferScale.x);
    const int fbHeight = (int)(drawData->DisplaySize.y * drawData->FramebufferScale.y);
    if (fbWidth <= 0 || fbHeight <= 0) return;

    bgfx::setViewName(viewId, "ImGui");
    bgfx::setViewMode(viewId, bgfx::ViewMode::Sequential);

    const bgfx::Caps* caps = bgfx::getCaps();
    {
        float ortho[16];
        const float x = drawData->DisplayPos.x;
        const float y = drawData->DisplayPos.y;
        const float w = drawData->DisplaySize.x;
        const float h = drawData->DisplaySize.y;
        bx::mtxOrtho(ortho, x, x + w, y + h, y, 0.0f, 1000.0f, 0.0f, caps->homogeneousDepth);
        bgfx::setViewTransform(viewId, nullptr, ortho);
        bgfx::setViewRect(viewId, 0, 0, uint16_t(w), uint16_t(h));
    }

    const ImVec2 clipPos = drawData->DisplayPos;
    const ImVec2 clipScale = drawData->FramebufferScale;

    for (int n = 0; n < drawData->CmdListsCount; ++n) {
        const ImDrawList* dl = drawData->CmdLists[n];
        const uint32_t numV = (uint32_t)dl->VtxBuffer.size();
        const uint32_t numI = (uint32_t)dl->IdxBuffer.size();

        const bool idx32 = sizeof(ImDrawIdx) == 4;
        if (bgfx::getAvailTransientVertexBuffer(numV, g.layout) != numV
         || bgfx::getAvailTransientIndexBuffer(numI, idx32) != numI) {
            break; // out of transient space this frame; drop the rest
        }

        bgfx::TransientVertexBuffer tvb;
        bgfx::TransientIndexBuffer tib;
        bgfx::allocTransientVertexBuffer(&tvb, numV, g.layout);
        bgfx::allocTransientIndexBuffer(&tib, numI, idx32);
        std::memcpy(tvb.data, dl->VtxBuffer.Data, numV * sizeof(ImDrawVert));
        std::memcpy(tib.data, dl->IdxBuffer.Data, numI * sizeof(ImDrawIdx));

        bgfx::Encoder* enc = bgfx::begin();
        for (const ImDrawCmd& cmd : dl->CmdBuffer) {
            if (cmd.UserCallback) { cmd.UserCallback(dl, &cmd); continue; }
            if (cmd.ElemCount == 0) continue;

            const uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_MSAA
                | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA);

            bgfx::TextureHandle th = BGFX_INVALID_HANDLE;
            const ImTextureID texId = cmd.GetTexID();
            if (texId != ImTextureID_Invalid) th = bitCast<TextureBgfx>(texId).handle;

            // Project scissor into framebuffer space.
            const float x0 = (cmd.ClipRect.x - clipPos.x) * clipScale.x;
            const float y0 = (cmd.ClipRect.y - clipPos.y) * clipScale.y;
            const float x1 = (cmd.ClipRect.z - clipPos.x) * clipScale.x;
            const float y1 = (cmd.ClipRect.w - clipPos.y) * clipScale.y;
            if (x1 <= x0 || y1 <= y0) continue;

            const uint16_t xx = uint16_t(x0 < 0 ? 0 : x0);
            const uint16_t yy = uint16_t(y0 < 0 ? 0 : y0);
            enc->setScissor(xx, yy,
                uint16_t((x1 > 65535.0f ? 65535.0f : x1) - xx),
                uint16_t((y1 > 65535.0f ? 65535.0f : y1) - yy));
            enc->setState(state);
            enc->setTexture(0, g.texUniform, th);
            enc->setVertexBuffer(0, &tvb, cmd.VtxOffset, numV);
            enc->setIndexBuffer(&tib, cmd.IdxOffset, cmd.ElemCount);
            enc->submit(viewId, g.program);
        }
        bgfx::end(enc);
    }
}

} // namespace

namespace imgui_bgfx {

void init() {
    IMGUI_CHECKVERSION();
    g.imgui = ImGui::CreateContext();
    ImGui::SetCurrentContext(g.imgui);

    ImGuiIO& io = ImGui::GetIO();
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures;
    io.IniFilename = nullptr; // don't write imgui.ini
    io.Fonts->AddFontDefault();
    ImGui::StyleColorsDark();
    ImGui::GetStyle().FrameRounding = 4.0f;

    const bgfx::RendererType::Enum type = bgfx::getRendererType();
    g.program = bgfx::createProgram(
        bgfx::createEmbeddedShader(s_embeddedShaders, type, "vs_ocornut_imgui"),
        bgfx::createEmbeddedShader(s_embeddedShaders, type, "fs_ocornut_imgui"),
        true);
    g.layout.begin()
        .add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
        .end();
    g.texUniform = bgfx::createUniform("s_tex", bgfx::UniformType::Sampler);
}

void beginFrame(int width, int height, float dt, int mouseX, int mouseY, uint8_t mouseButtons, float wheel) {
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)width, (float)height);
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
    io.DeltaTime = dt > 0.0f ? dt : 1.0f / 60.0f;
    io.AddMousePosEvent((float)mouseX, (float)mouseY);
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, (mouseButtons & 0x01) != 0);
    io.AddMouseButtonEvent(ImGuiMouseButton_Right, (mouseButtons & 0x02) != 0);
    io.AddMouseButtonEvent(ImGuiMouseButton_Middle, (mouseButtons & 0x04) != 0);
    if (wheel != 0.0f) io.AddMouseWheelEvent(0.0f, wheel);
    ImGui::NewFrame();
}

void endFrame(uint16_t viewId) {
    ImGui::Render();
    ImDrawData* dd = ImGui::GetDrawData();
    syncTextures(dd);
    renderDrawData(dd, viewId);
}

void shutdown() {
    for (ImTextureData* tex : ImGui::GetPlatformIO().Textures) {
        if (tex->RefCount == 1 && tex->GetTexID() != ImTextureID_Invalid) {
            TextureBgfx t = bitCast<TextureBgfx>(tex->GetTexID());
            if (bgfx::isValid(t.handle)) bgfx::destroy(t.handle);
            tex->SetTexID(ImTextureID_Invalid);
            tex->SetStatus(ImTextureStatus_Destroyed);
        }
    }
    if (bgfx::isValid(g.texUniform)) bgfx::destroy(g.texUniform);
    if (bgfx::isValid(g.program)) bgfx::destroy(g.program);
    ImGui::DestroyContext(g.imgui);
    g.imgui = nullptr;
}

} // namespace imgui_bgfx
