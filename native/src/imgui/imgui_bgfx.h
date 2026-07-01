// Minimal self-contained Dear ImGui (v1.92) renderer for bgfx. Public domain.
//
// No coupling to bgfx's example framework (entry / bgfx_utils / ImGuizmo). Owns
// the ImGui context, uses bgfx's precompiled ImGui shaders (embedded), and
// takes input fed directly from the app (SDL3). Call ImGui::* between
// beginFrame() and endFrame().
#pragma once

#include <cstdint>

namespace imgui_bgfx {

void init();
void shutdown();

// Feed input and start a frame.
//   width/height : window size in pixels
//   dt           : seconds since last frame
//   mouseX/Y     : cursor position in pixels
//   mouseButtons : bit0 = left, bit1 = right, bit2 = middle
//   wheel        : vertical wheel delta this frame
void beginFrame(int width, int height, float dt, int mouseX, int mouseY, uint8_t mouseButtons, float wheel);

// Render the accumulated ImGui draw data on the given bgfx view.
void endFrame(uint16_t viewId);

} // namespace imgui_bgfx
