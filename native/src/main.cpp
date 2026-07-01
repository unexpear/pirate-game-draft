// Sea Trial — native app window (Milestone 0, part 2b). Public domain (Unlicense).
//
// Opens an SDL3 window, initializes bgfx on it, and clears the screen every
// frame with a debug-text overlay showing the self-test result. Dear ImGui
// panel is next (part 2c). Run with `--frames N` to auto-exit after N frames
// (handy for scripted/headless verification); otherwise runs until the window
// is closed or Escape is pressed.
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h> // for SDL_SetMainReady (main is ours; not hijacked)

#include <bgfx/bgfx.h> // PlatformData / setPlatformData / renderFrame live here now

#include "ship_model.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

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
    int maxFrames = -1; // <0 => run until the window is closed
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc)
            maxFrames = std::atoi(argv[++i]);
    }

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

    // Single-threaded bgfx: calling renderFrame() before init() disables the
    // separate render thread, which keeps a simple app straightforward.
    bgfx::renderFrame();

    bgfx::Init init;
    init.type = bgfx::RendererType::Count; // auto-pick (D3D / Vulkan / etc.)
    init.resolution.width = static_cast<uint32_t>(width);
    init.resolution.height = static_cast<uint32_t>(height);
    init.resolution.reset = BGFX_RESET_VSYNC;
    init.platformData.nwh = nativeWindowHandle(window);
    if (!bgfx::init(init)) {
        std::fprintf(stderr, "bgfx::init failed\n");
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    bgfx::setDebug(BGFX_DEBUG_TEXT);
    bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x1e2a3aff, 1.0f, 0);

    // Run the self-tests once and surface them in the debug overlay.
    const auto results = sea::runSelfTest();
    int passing = 0;
    for (const auto& r : results) if (r.pass) ++passing;
    const int total = static_cast<int>(results.size());
    std::printf("self-tests: %d / %d passing\n", passing, total);
    std::printf("renderer: %s\n", bgfx::getRendererName(bgfx::getRendererType()));
    std::fflush(stdout);

    bool running = true;
    int frame = 0;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) running = false;
            else if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) running = false;
            else if (e.type == SDL_EVENT_WINDOW_RESIZED) {
                width = e.window.data1;
                height = e.window.data2;
                bgfx::reset(static_cast<uint32_t>(width), static_cast<uint32_t>(height), BGFX_RESET_VSYNC);
            }
        }

        bgfx::setViewRect(0, 0, 0, static_cast<uint16_t>(width), static_cast<uint16_t>(height));
        bgfx::touch(0);
        bgfx::dbgTextClear();
        bgfx::dbgTextPrintf(2, 1, 0x0f, "Sea Trial - Milestone 0 (bgfx clear)");
        bgfx::dbgTextPrintf(2, 2, passing == total ? 0x0a : 0x0c, "model self-tests: %d / %d passing", passing, total);
        bgfx::dbgTextPrintf(2, 3, 0x08, "renderer: %s   -   Esc to quit", bgfx::getRendererName(bgfx::getRendererType()));
        bgfx::frame();

        if (maxFrames >= 0 && ++frame >= maxFrames) running = false;
    }

    bgfx::shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
    std::printf("clean exit after %d frames\n", frame);
    return 0;
}
