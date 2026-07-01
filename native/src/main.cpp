// Sea Trial — native app window (Milestone 0, complete). Public domain (Unlicense).
//
// SDL3 window + bgfx clear + a Dear ImGui debug panel showing the model
// self-tests and Test Sloop stats. This is the full Milestone 0 target; ship
// rendering, water, Jolt, and Steamworks all come later.
//
// Run with `--frames N` to auto-exit after N frames (scripted verification);
// otherwise runs until the window is closed or Escape is pressed.
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h> // SDL_SetMainReady (main is ours; not hijacked)

#include <bgfx/bgfx.h>

#include <imgui.h>
#include "imgui/imgui_bgfx.h"

#include "ship_model.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

constexpr uint16_t kClearView = 0;
constexpr uint16_t kImGuiView = 200;

void* nativeWindowHandle(SDL_Window* w) {
#if defined(_WIN32)
    return SDL_GetPointerProperty(SDL_GetWindowProperties(w), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#elif defined(__APPLE__)
    return SDL_GetPointerProperty(SDL_GetWindowProperties(w), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
#else
    return SDL_GetPointerProperty(SDL_GetWindowProperties(w), SDL_PROP_WINDOW_X11_WINDOW_NUMBER, nullptr);
#endif
}

} // namespace

int main(int argc, char** argv) {
    int maxFrames = -1;
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) maxFrames = std::atoi(argv[++i]);

    SDL_SetMainReady();
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    int width = 1280, height = 720;
    SDL_Window* window = SDL_CreateWindow("Sea Trial \xE2\x80\x94 Milestone 0", width, height, SDL_WINDOW_RESIZABLE);
    if (!window) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    bgfx::renderFrame(); // single-threaded

    bgfx::Init init;
    init.type = bgfx::RendererType::Count;
    init.resolution.width = (uint32_t)width;
    init.resolution.height = (uint32_t)height;
    init.resolution.reset = BGFX_RESET_VSYNC;
    init.platformData.nwh = nativeWindowHandle(window);
    if (!bgfx::init(init)) {
        std::fprintf(stderr, "bgfx::init failed\n");
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    bgfx::setViewClear(kClearView, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x1e2a3aff, 1.0f, 0);

    imgui_bgfx::init();

    // Model data for the panel (computed once).
    const auto results = sea::runSelfTest();
    int passing = 0;
    for (const auto& r : results) if (r.pass) ++passing;
    const int total = (int)results.size();
    sea::ShipConfig cfg;
    cfg.name = "Test Sloop";
    const sea::Stats stats = sea::getShipStats(sea::makeShipFromConfig(cfg));

    std::printf("self-tests: %d / %d passing\n", passing, total);
    std::printf("renderer: %s\n", bgfx::getRendererName(bgfx::getRendererType()));
    std::fflush(stdout);

    const ImVec4 kGreen(0.55f, 0.95f, 0.60f, 1.0f);
    const ImVec4 kRed(1.0f, 0.55f, 0.55f, 1.0f);

    int mouseX = 0, mouseY = 0;
    uint8_t mouseButtons = 0;
    float wheel = 0.0f;
    uint64_t last = SDL_GetTicks();

    bool running = true;
    int frame = 0;
    while (running) {
        wheel = 0.0f;
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_EVENT_QUIT: running = false; break;
            case SDL_EVENT_KEY_DOWN:
                if (e.key.key == SDLK_ESCAPE) running = false;
                break;
            case SDL_EVENT_WINDOW_RESIZED:
                width = e.window.data1;
                height = e.window.data2;
                bgfx::reset((uint32_t)width, (uint32_t)height, BGFX_RESET_VSYNC);
                break;
            case SDL_EVENT_MOUSE_MOTION:
                mouseX = (int)e.motion.x;
                mouseY = (int)e.motion.y;
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP: {
                const bool down = (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
                uint8_t bit = 0;
                if (e.button.button == SDL_BUTTON_LEFT) bit = 0x01;
                else if (e.button.button == SDL_BUTTON_RIGHT) bit = 0x02;
                else if (e.button.button == SDL_BUTTON_MIDDLE) bit = 0x04;
                if (down) mouseButtons |= bit; else mouseButtons &= ~bit;
                break;
            }
            case SDL_EVENT_MOUSE_WHEEL:
                wheel += e.wheel.y;
                break;
            default: break;
            }
        }

        const uint64_t now = SDL_GetTicks();
        const float dt = (now - last) / 1000.0f;
        last = now;

        imgui_bgfx::beginFrame(width, height, dt, mouseX, mouseY, mouseButtons, wheel);

        ImGui::SetNextWindowPos(ImVec2(24, 24), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(440, 460), ImGuiCond_FirstUseEver);
        ImGui::Begin("Sea Trial - Milestone 0"); // ASCII: default ImGui font has no em-dash glyph
        ImGui::TextUnformatted("Engineless native C++ build");
        ImGui::Text("Renderer: %s", bgfx::getRendererName(bgfx::getRendererType()));
        ImGui::Separator();
        ImGui::TextColored(passing == total ? kGreen : kRed, "Model self-tests: %d / %d passing", passing, total);
        if (ImGui::CollapsingHeader("Self-tests", ImGuiTreeNodeFlags_DefaultOpen)) {
            for (const auto& r : results)
                ImGui::TextColored(r.pass ? kGreen : kRed, "%s  %s", r.pass ? "PASS" : "FAIL", r.name.c_str());
        }
        ImGui::Separator();
        if (ImGui::CollapsingHeader("Test Sloop - stats", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("Pieces:       %d", stats.pieceCount);
            ImGui::Text("Mass:         %.0f kg", stats.mass);
            ImGui::Text("Buoyancy:     %.0f kg-eq", stats.buoyancyScore);
            ImGui::TextColored(stats.floatMargin > 0 ? kGreen : kRed, "Float margin: %.0f kg", stats.floatMargin);
        }
        ImGui::Separator();
        ImGui::Text("Frame %d   %.1f FPS", frame, ImGui::GetIO().Framerate);
        ImGui::TextDisabled("Esc to quit");
        ImGui::End();

        bgfx::setViewRect(kClearView, 0, 0, (uint16_t)width, (uint16_t)height);
        bgfx::touch(kClearView);
        imgui_bgfx::endFrame(kImGuiView);
        bgfx::frame();

        if (maxFrames >= 0 && ++frame >= maxFrames) running = false;
    }

    imgui_bgfx::shutdown();
    bgfx::shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
    std::printf("clean exit after %d frames\n", frame);
    return 0;
}
