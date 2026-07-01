// Sea Trial — native app window (Milestone 0, part 2a). Public domain (Unlicense).
//
// Opens an SDL3 window and runs a clean event loop. bgfx clear + Dear ImGui
// panel come next (parts 2b/2c). Run with `--frames N` to auto-exit after N
// frames (handy for scripted/headless verification); otherwise runs until the
// window is closed or Escape is pressed.
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h> // for SDL_SetMainReady (main is ours; not hijacked)

#include "ship_model.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

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

    SDL_Window* window = SDL_CreateWindow("Sea Trial \xE2\x80\x94 Milestone 0", 1280, 720, SDL_WINDOW_RESIZABLE);
    if (!window) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    // Prove the model links into the app: run the self-tests at startup. The
    // Dear ImGui panel will surface these live once part 2c lands.
    const auto results = sea::runSelfTest();
    int passing = 0;
    for (const auto& r : results) if (r.pass) ++passing;
    std::printf("self-tests: %d / %d passing\n", passing, static_cast<int>(results.size()));
    std::fflush(stdout);

    bool running = true;
    int frame = 0;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) running = false;
            else if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) running = false;
        }
        SDL_Delay(16);
        if (maxFrames >= 0 && ++frame >= maxFrames) running = false;
    }

    SDL_DestroyWindow(window);
    SDL_Quit();
    std::printf("clean exit after %d frames\n", frame);
    return 0;
}
