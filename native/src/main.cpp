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
#include "ship_view.h"
#include "ship_mesh.h"
#include "shadow_gpu.h"
#include "screenshot.h"

#include "ship_model.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr uint16_t kShadowView = 0; // sun's depth pass (renders first, lowest id)
constexpr uint16_t kClearView = 1;  // main scene
constexpr uint16_t kImGuiView = 200;

// Sun direction (must match the lighting in the shaders).
constexpr float kSunX = 0.42f, kSunY = 0.68f, kSunZ = -0.55f;

void* nativeWindowHandle(SDL_Window* w) {
#if defined(_WIN32)
    return SDL_GetPointerProperty(SDL_GetWindowProperties(w), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#elif defined(__APPLE__)
    return SDL_GetPointerProperty(SDL_GetWindowProperties(w), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
#else
    return SDL_GetPointerProperty(SDL_GetWindowProperties(w), SDL_PROP_WINDOW_X11_WINDOW_NUMBER, nullptr);
#endif
}

// A masthead wind vane HUD (top-right): bow-up compass with the no-go zone and
// best-reach arcs, a TRUE and an APPARENT wind arrow (both pointing the way the
// wind blows over the boat), and two telltales that stream aft when the sail is
// drawing and flutter when it luffs. Bearings are signed radians of where the
// wind comes FROM relative to the bow (0 = dead ahead, + = starboard).
void drawWindVane(int width, float trueSrcRel, float appSrcRel, float awaDeg,
                  float drive, float appSpeed, float trueSpeed,
                  const char* pointName, bool luffing, float timeSec) {
    const float PI = 3.14159265f;
    const float dial = 150.0f;
    ImGui::SetNextWindowPos(ImVec2(float(width) - dial - 40.0f, 40.0f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.30f);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize
        | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
    ImGui::Begin("WindVane", nullptr, flags);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(dial, dial)); // reserve the dial's space in the layout
    const ImVec2 c = ImVec2(p0.x + dial * 0.5f, p0.y + dial * 0.5f);
    const float R = dial * 0.42f;
    auto pt = [&](float bearing, float rr) {
        return ImVec2(c.x + rr * std::sin(bearing), c.y - rr * std::cos(bearing));
    };
    const float noGo = 43.0f * PI / 180.0f;

    // No-go wedge at the bow (red), then the best-reach arcs (green).
    { ImVec2 v[20]; int n = 0; v[n++] = c;
      for (int i = 0; i <= 16; ++i) v[n++] = pt(-noGo + 2.0f * noGo * i / 16.0f, R);
      dl->AddConvexPolyFilled(v, n, IM_COL32(210, 70, 55, 70)); }
    for (int side = -1; side <= 1; side += 2) {
        ImVec2 v[16]; int n = 0; v[n++] = c;
        const float a0 = side * 75.0f * PI / 180.0f, a1 = side * 120.0f * PI / 180.0f;
        for (int i = 0; i <= 12; ++i) v[n++] = pt(a0 + (a1 - a0) * i / 12.0f, R);
        dl->AddConvexPolyFilled(v, n, IM_COL32(70, 190, 110, 45));
    }
    dl->AddCircle(c, R, IM_COL32(220, 225, 235, 200), 48, 1.6f);
    dl->AddLine(pt(0, R), pt(PI, R), IM_COL32(255, 255, 255, 40), 1.0f);          // bow-stern
    dl->AddLine(pt(PI * 0.5f, R), pt(-PI * 0.5f, R), IM_COL32(255, 255, 255, 40), 1.0f); // beam
    dl->AddTriangleFilled(pt(0, R + 12), pt(0.14f, R + 1), pt(-0.14f, R + 1), IM_COL32(240, 240, 245, 230));

    // Wind arrows, pointing inward (wind blows from the source toward the boat).
    auto arrow = [&](float bearing, ImU32 col, float thick) {
        const ImVec2 a = pt(bearing, R - 2.0f), b = pt(bearing, R * 0.30f);
        dl->AddLine(a, b, col, thick);
        dl->AddTriangleFilled(b, pt(bearing + 0.16f, R * 0.30f + 11.0f),
                              pt(bearing - 0.16f, R * 0.30f + 11.0f), col);
    };
    arrow(trueSrcRel, IM_COL32(90, 160, 240, 220), 2.0f); // true wind (blue)
    arrow(appSrcRel, IM_COL32(250, 210, 90, 240), 3.0f);  // apparent wind (amber)

    // Telltales at the sail: stream aft (down) when drawing, flutter when luffing.
    const float lift = luffing ? 1.0f : (1.0f - std::min(1.0f, drive / 0.55f));
    for (int s = 0; s < 2; ++s) {
        const float side = s == 0 ? -1.0f : 1.0f;
        const ImU32 col = s == 0 ? IM_COL32(235, 90, 80, 235) : IM_COL32(90, 210, 120, 235);
        const ImVec2 anchor(c.x + side * 6.0f, c.y - 2.0f);
        const float d = lift * 1.5f + std::sin(timeSec * 15.0f + s * 2.0f) * lift * 0.6f;
        const float len = 15.0f;
        const ImVec2 mid(anchor.x + std::sin(d) * side * len * 0.5f, anchor.y + std::cos(d) * len * 0.5f);
        const ImVec2 end(mid.x + std::sin(d * 1.4f) * side * len * 0.5f, mid.y + std::cos(d) * len * 0.5f);
        dl->AddLine(anchor, mid, col, 2.0f);
        dl->AddLine(mid, end, col, 2.0f);
    }

    ImGui::PushStyleColor(ImGuiCol_Text, luffing ? IM_COL32(240, 120, 110, 255) : IM_COL32(210, 230, 215, 255));
    ImGui::Text(" %s", luffing ? "IN IRONS - bear away" : pointName);
    ImGui::PopStyleColor();
    ImGui::Text(" Drive %.0f%%   %.0f deg off bow", drive * 100.0f, awaDeg);
    ImGui::Text(" App wind %.0f  (true %.0f)", appSpeed, trueSpeed);
    ImGui::End();
}

} // namespace

int main(int argc, char** argv) {
    int maxFrames = -1;
    // Headless screenshot capture for scripted / gauntlet-loop verification:
    //   --shot <path>        write a clean PNG frame and exit
    //   --scene sail|build   which scene to capture (default sail)
    //   --pos <x> <z>        initial ship world position (framing)
    //   --head <rad>         initial ship heading
    //   --shot-frames <n>    frames to render before capture (default 90, lets waves settle)
    std::string shotPath;
    std::string shotScene = "sail";
    float shotPosX = 0.0f, shotPosZ = 0.0f, shotHead = 0.0f;
    bool shotWalk = false;
    int shotFrames = 90;
    float shotCam[6] = { 0, 0, 0, 0, 0, 0 }; // --cam ex ey ez  ax ay az (scene space free camera)
    bool useCam = false;
    bool wallDebug = false;                  // --walldebug: colour + number each wall panel
    bool auditOnly = false;                  // --audit: one isolated building, walls coloured + numbered
    bool charOn = false;                     // --char x z heading [y]: a person in the town
    float charDX = 0.0f, charDZ = 0.0f, charDH = 0.0f, charDY = 0.0f;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) maxFrames = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--shot") == 0 && i + 1 < argc) shotPath = argv[++i];
        else if (std::strcmp(argv[i], "--scene") == 0 && i + 1 < argc) shotScene = argv[++i];
        else if (std::strcmp(argv[i], "--pos") == 0 && i + 2 < argc) { shotPosX = float(std::atof(argv[++i])); shotPosZ = float(std::atof(argv[++i])); }
        else if (std::strcmp(argv[i], "--head") == 0 && i + 1 < argc) shotHead = float(std::atof(argv[++i]));
        else if (std::strcmp(argv[i], "--walk") == 0) shotWalk = true;
        else if (std::strcmp(argv[i], "--cam") == 0 && i + 6 < argc) { for (int k = 0; k < 6; ++k) shotCam[k] = float(std::atof(argv[++i])); useCam = true; }
        else if (std::strcmp(argv[i], "--shot-frames") == 0 && i + 1 < argc) shotFrames = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--walldebug") == 0) wallDebug = true;
        else if (std::strcmp(argv[i], "--audit") == 0) { wallDebug = true; auditOnly = true; }
        else if (std::strcmp(argv[i], "--char") == 0 && i + 3 < argc) {
            charOn = true;
            charDX = float(std::atof(argv[++i]));
            charDZ = float(std::atof(argv[++i]));
            charDH = float(std::atof(argv[++i]));
            if (i + 1 < argc && argv[i + 1][0] != '-') charDY = float(std::atof(argv[++i]));
        }
    }
    const bool shotMode = !shotPath.empty();
    const bool hideUI = shotMode;      // clean frame for the critic — no debug panel
    if (shotMode && maxFrames < 0) maxFrames = shotFrames;

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
    init.type = bgfx::RendererType::Direct3D11; // water shaders are compiled for D3D11 (dxbc)
    init.resolution.width = (uint32_t)width;
    init.resolution.height = (uint32_t)height;
    init.resolution.reset = shotMode ? BGFX_RESET_NONE : BGFX_RESET_VSYNC; // no vsync wait when capturing
    init.platformData.nwh = nativeWindowHandle(window);
    init.callback = shot::callback(); // handles bgfx::requestScreenShot -> PNG
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
    sea::Ship ship = sea::makeShipFromConfig(cfg); // mutable: cargo/damage are live
    const std::vector<sea::Wave> waves = sea::makeWaveField("sea-trial-native");

    ship_view::init();
    shadow::init(1024);

    std::printf("self-tests: %d / %d passing\n", passing, total);
    std::printf("renderer: %s\n", bgfx::getRendererName(bgfx::getRendererType()));
    std::fflush(stdout);

    const ImVec4 kGreen(0.55f, 0.95f, 0.60f, 1.0f);
    const ImVec4 kRed(1.0f, 0.55f, 0.55f, 1.0f);

    int mouseX = 0, mouseY = 0;
    uint8_t mouseButtons = 0;
    float wheel = 0.0f;
    uint64_t last = SDL_GetTicks();
    float timeSec = 0.0f;
    float sinkDepth = 0.0f;
    int sailTier = 1;                 // 0 anchored, 1 half sail (cruise default), 2 full sail
    float wHoldTime = 0.0f;           // time W held at full sail -> engages travel speed
    float sailStepCooldown = 0.0f;    // debounce between sail-state steps
    float speed = 0.0f, heading = 0.0f, worldX = 0.0f, worldZ = 0.0f;
    float windDir = 2.1f;             // wind blows toward this heading (radians); drifts

    // Gunnery + an AI enemy warship that maneuvers for a broadside and fires back.
    // The enemy is drawn from the difficulty ZONE the player is in: a light raider
    // near the harbour, heavier ships the further out you sail (see sea::ZoneMap).
    // Each ship's own hull profile makes it sail and turn to its class.
    int enemyLevel = 1;
    sea::ShipConfig enemyCfg = sea::enemyConfigForLevel(enemyLevel);
    sea::Ship enemy = sea::makeShipFromConfig(enemyCfg);
    enemy.display_name = sea::zoneShipName(enemyLevel);
    sea::HullProfile enemyProfile = sea::bakeHullProfile(enemy);
    float enemyWorldX = 10.0f, enemyWorldZ = 60.0f; // starts off the starboard bow
    float enemyHeading = 3.0f, enemySink = 0.0f;
    float enemySpeed = 0.0f, enemyReload = 0.0f;
    float respawnTimer = 0.0f; // delay before the next zone-scaled enemy sails in
    std::vector<sea::Projectile> shots;      // ours -> hit the enemy
    std::vector<sea::Projectile> enemyShots; // theirs -> hit us
    float reload = 0.0f;
    bool wantFire = false;
    bool enemyStruck = false, boarding = false, captured = false;
    float boardTimer = 0.0f;
    bool wantBoard = false;

    // Build mode: freeze and assemble the hull plank-by-plank in a tradition's order.
    bool buildMode = false;
    int buildTrad = 2;  // 0 Roman, 1 Viking, 2 English (Age of Sail)
    int placed = 0;     // pieces revealed so far

    // The hull's baked water-physics profile (Build -> Sail bridge). Sailing reads
    // top-speed & turn from it, so the boat sails like the shape you built.
    sea::HullProfile activeProfile = sea::bakeHullProfile(ship);
    bool launching = false, launched = true;
    float launchTimer = 0.0f;

    // On-foot character — walk the shipyard. Position is in the build-scene's local
    // space (the ship sits on the stocks at the origin).
    bool walkMode = false, onDeck = false;
    float charX = 9.0f, charY = 1.8f, charZ = -11.0f, charHeading = -0.7f, walkPhase = 0.0f;

    // A large island with a port + shipyard, moored at a fixed spot to the north.
    const float islandX = 0.0f, islandZ = 120.0f;
    const float kLandRadius = 56.0f;  // run aground here
    const float kLandCutRadius = 40.0f; // sea carved out inside this (< min coastline ~42, so no dry moat)
    const float kSafeRadius = 95.0f;  // combat-free harbour truce inside this ring
    bool wasAground = false;          // edge-trigger the run-aground penalty

    // The shifting-level sea map: danger rises in bands out from the harbour, with
    // a little overlap at each frontier so a heavier ship can prowl inward.
    sea::ZoneMap zones;
    zones.harborX = islandX; zones.harborZ = islandZ; zones.safeRadius = kSafeRadius;
    sea::Ship yardShip = sea::makeShipFromConfig(cfg); // a half-built hull on the stocks
    {
        const std::vector<int> ord = sea::buildOrder(yardShip, sea::BuildTradition::English);
        sea::Ship partial = yardShip;
        partial.pieces.clear();
        for (int i = 0; i < 12 && i < int(ord.size()); ++i) partial.pieces.push_back(yardShip.pieces[ord[i]]);
        partial.systems.mast_count = 0;
        partial.systems.sail_count = 0;
        yardShip = partial;
    }

    auto clampf = [](float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); };
    auto isHull = [](const sea::Piece& p) { return p.type == "plank" || p.type == "rib" || p.type == "keel"; };
    auto damagePlank = [&]() {
        int n = 0;
        for (const auto& p : ship.pieces) if (isHull(p) && p.damage < 1.0) ++n;
        if (n == 0) return;
        const int target = int(std::fabs(std::sin(timeSec * 17.13 + n)) * n) % n;
        int k = 0;
        for (auto& p : ship.pieces)
            if (isHull(p) && p.damage < 1.0 && k++ == target) { p.damage = p.damage + 0.25 > 1.0 ? 1.0 : p.damage + 0.25; return; }
    };
    auto repairAll = [&]() { for (auto& p : ship.pieces) p.damage = 0.0; };
    // Deterministic pseudo-random in [0,1) — no rand(), seeded by time + position.
    auto pseudoRand = [&](double salt) {
        double v = std::sin((timeSec * 1.37 + salt) * 12.9898 + worldX * 0.0719 + worldZ * 0.1237) * 43758.5453;
        return v - std::floor(v);
    };
    // Sail a fresh enemy in, scaled to the zone the player is in (heavier ships
    // deeper out; near a frontier a heavier one may spill in from the next band).
    auto spawnEnemyForZone = [&]() {
        const double ang = pseudoRand(1.0) * 6.2831853;
        const double dist = 120.0 + pseudoRand(2.0) * 70.0;
        double sx = double(worldX) + std::cos(ang) * dist;
        double sz = double(worldZ) + std::sin(ang) * dist;
        sea::keepOutsideCircle(sx, sz, islandX, islandZ, double(kSafeRadius) + 10.0); // never in the truce
        int lvl = sea::spawnLevel(zones, sx, sz, pseudoRand(3.0));
        if (lvl < 1) lvl = 1;
        enemyLevel = lvl;
        enemyCfg = sea::enemyConfigForLevel(lvl);
        enemy = sea::makeShipFromConfig(enemyCfg);
        enemy.display_name = sea::zoneShipName(lvl);
        enemyProfile = sea::bakeHullProfile(enemy);
        enemyWorldX = float(sx); enemyWorldZ = float(sz);
        enemyHeading = float(ang + 3.14159);
        enemySink = 0.0f; enemySpeed = 0.0f; enemyReload = 1.0f; respawnTimer = 0.0f;
        enemyStruck = false; boarding = false; captured = false; boardTimer = 0.0f;
        enemyShots.clear();
    };
    auto resetShip = [&]() {
        ship = sea::makeShipFromConfig(cfg);
        sinkDepth = 0.0f; sailTier = 1; wHoldTime = 0.0f; speed = 0.0f; heading = 0.0f; worldX = 0.0f; worldZ = 0.0f;
        shots.clear();
        spawnEnemyForZone();
    };

    // Shot mode: pose the scene for a clean capture (no player input arrives).
    if (shotMode) {
        worldX = shotPosX; worldZ = shotPosZ; heading = shotHead;
        if (shotScene == "build") { buildMode = true; walkMode = shotWalk; placed = 9999; } // full hull on the stocks
        if (useCam) ship_view::setFreeCamera(true, shotCam[0], shotCam[1], shotCam[2], shotCam[3], shotCam[4], shotCam[5]);
    }
    ship_view::setWallDebug(wallDebug);
    ship_view::setAuditOnly(auditOnly);
    ship_view::setDebugCharacter(charOn, charDX, charDZ, charDH, charDY);
    const int shotAt = shotMode ? (maxFrames > 3 ? maxFrames - 3 : 0) : -1; // request a few frames before exit
    bool shotRequested = false;

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
                else if (!ImGui::GetIO().WantCaptureKeyboard) {
                    switch (e.key.scancode) {
                    case SDL_SCANCODE_C: ship.systems.cargo_mass = clampf(float(ship.systems.cargo_mass) + 200.0f, 0.0f, 6000.0f); break;
                    case SDL_SCANCODE_V: ship.systems.cargo_mass = clampf(float(ship.systems.cargo_mass) - 200.0f, 0.0f, 6000.0f); break;
                    case SDL_SCANCODE_X: damagePlank(); break;
                    case SDL_SCANCODE_Z: repairAll(); break;
                    case SDL_SCANCODE_R: resetShip(); break;
                    case SDL_SCANCODE_SPACE: if (!e.key.repeat) wantFire = true; break;
                    case SDL_SCANCODE_B: if (!e.key.repeat) wantBoard = true; break;
                    default: break; // W/S/A/D sailing is polled below (hold-aware)
                    }
                }
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
        timeSec += dt;

        // Sailing — Black Flag's stepped "gearbox" of sail states. Tap or hold W to
        // raise sail (anchored -> half -> full), then HOLD W at full sail to reach
        // TRAVEL SPEED, the open-water overdrive. S lowers a notch and coasts to a
        // stop — there is NO reverse. A/D steer; turn radius is inversely tied to
        // speed, so a near-stop pivots sharpest while full/travel turn wide (the
        // stop-to-turn tactic). Ship-centric: the ship holds the origin, ocean scrolls.
        const bool* keys = SDL_GetKeyboardState(nullptr);
        const bool kbFree = !ImGui::GetIO().WantCaptureKeyboard && !buildMode; // no sailing input while building/walking
        float steer = 0.0f;
        if (kbFree) {
            if (keys[SDL_SCANCODE_D]) steer += 1.0f;
            if (keys[SDL_SCANCODE_A]) steer -= 1.0f;
        }
        // Sail-state stepping (debounced), then hold-W-at-full for travel speed.
        const bool wHeld = kbFree && keys[SDL_SCANCODE_W];
        const bool sHeld = kbFree && keys[SDL_SCANCODE_S];
        sailStepCooldown = clampf(sailStepCooldown - dt, 0.0f, 1.0f);
        if (sailStepCooldown == 0.0f) {
            if (wHeld && sailTier < 2) { sailTier++; sailStepCooldown = 0.28f; }
            else if (sHeld && sailTier > 0) { sailTier--; sailStepCooldown = 0.28f; }
        }
        // Holding W at full sail latches into TRAVEL SPEED (the open-water
        // overdrive); tap S to drop back down. No reverse — S only coasts to a stop.
        if (wHeld && sailTier == 2) wHoldTime += dt; else wHoldTime = 0.0f;
        if (wHoldTime > 0.45f) sailTier = 3;
        const int effTier = sailTier;

        // Real points of sail: no drive in the no-go zone (you must tack upwind),
        // fastest on a reach, slower dead downwind. Wind shifts slowly.
        windDir += 0.02f * dt;
        if (windDir > 6.28318531f) windDir -= 6.28318531f;
        const float align = std::cos(heading - windDir);
        const float awaRad = std::acos(clampf(-align, -1.0f, 1.0f)); // wind angle off the bow
        const float awaDeg = awaRad * 57.2957795f;
        const float windFactor = float(sea::sailPower(awaDeg));      // 0 in irons .. 1 on a reach

        const float kFullSpeed = 11.0f;
        const float kTravelSpeed = 16.5f;
        // This hull's speeds, scaled by its baked profile (long & fine sails faster).
        const float pFull = kFullSpeed * float(activeProfile.topSpeedFactor);
        const float pTravel = kTravelSpeed * float(activeProfile.topSpeedFactor);
        const float tierSpeed = effTier == 0 ? 0.0f
                              : (effTier == 1 ? pFull * 0.5f
                              : (effTier == 2 ? pFull : pTravel));
        const float rig = float(sea::rigIntegrity(ship)); // a shot-up rig loses drive
        const float targetSpeed = tierSpeed * windFactor * rig;
        // Heavier hulls gather (and lose) way more slowly.
        speed += (targetSpeed - speed) * clampf(dt * 1.2f * float(activeProfile.accelFactor), 0.0f, 1.0f);
        const float speedFrac = clampf(speed / (pTravel > 0.1f ? pTravel : kTravelSpeed), 0.0f, 1.0f);
        // Turn radius couples to speed AND the hull's agility (short & beamy turns tighter).
        const float turnRate = 1.35f * (1.0f - 0.62f * speedFrac) * float(activeProfile.turnFactor);
        heading += steer * turnRate * dt;
        if (buildMode) speed = 0.0f; // frozen on the stocks while building
        worldX += std::sin(heading) * speed * dt;
        worldZ += std::cos(heading) * speed * dt;
        // Run aground: keep the hull out of the island's shore circle.
        {
            double px = worldX, pz = worldZ;
            const bool aground = sea::keepOutsideCircle(px, pz, islandX, islandZ, kLandRadius);
            if (aground) {
                worldX = float(px); worldZ = float(pz);
                if (!wasAground) speed *= 0.2f; // lose way once when you first strike, not every frame
            }
            wasAground = aground;
        }
        const float islX = islandX - worldX, islZ = islandZ - worldZ; // island vs. our ship
        const float islandDist = std::sqrt(islX * islX + islZ * islZ);
        const bool inSafeZone = islandDist < kSafeRadius; // harbour truce - no combat

        // Visible canvas reflects the sail state: furled -> half -> full.
        const float sailFullness = effTier == 0 ? 0.0f : (effTier == 1 ? 0.55f : 1.0f);

        const sea::Stats stats = sea::getShipStats(ship);
        sea::FloatPose pose = sea::computeFloatPose(ship, waves, timeSec, worldX, worldZ, heading);
        // Founder: an over-margin ship sinks progressively; recovers if lightened in time.
        if (stats.sinking) sinkDepth += dt * clampf(std::fabs(float(stats.floatMargin)) / 1200.0f, 0.3f, 3.0f);
        else sinkDepth = clampf(sinkDepth - dt * 2.5f, 0.0f, 1000.0f);
        pose.heaveY -= sinkDepth;

        // --- Enemy warship: AI maneuver, buoyancy pose, return fire, foundering ---
        const bool enemyGone = enemySink > 22.0f;
        enemyReload = clampf(enemyReload - dt, 0.0f, 10.0f);
        sea::FloatPose enemyPose;
        // Keep the enemy out of the harbour safe zone (and thus off the island) — it
        // blockades the mouth instead of sailing through the land or camping the truce.
        {
            double ex = enemyWorldX, ez = enemyWorldZ;
            if (sea::keepOutsideCircle(ex, ez, islandX, islandZ, kSafeRadius)) {
                enemyWorldX = float(ex); enemyWorldZ = float(ez);
            }
        }
        if (!enemyGone && !enemyStruck && !buildMode) {
            // Fighting: maneuver for a broadside and fire.
            const bool eReady = enemyReload <= 0.0f;
            const sea::AiOrders ord = sea::aiCaptain(enemyWorldX, enemyWorldZ, enemyHeading,
                                                     worldX, worldZ, 28.0, eReady);
            const float eFull = kFullSpeed * float(enemyProfile.topSpeedFactor);
            const float eTrav = kTravelSpeed * float(enemyProfile.topSpeedFactor);
            const float eRig = float(sea::rigIntegrity(enemy)); // rake her rig to slow her
            const float eTarget = (ord.sailTier <= 0 ? 0.0f
                                : (ord.sailTier == 1 ? eFull * 0.5f
                                : (ord.sailTier == 2 ? eFull : eTrav))) * eRig;
            enemySpeed += (eTarget - enemySpeed) * clampf(dt * 1.0f * float(enemyProfile.accelFactor), 0.0f, 1.0f);
            const float eFrac = clampf(enemySpeed / (eTrav > 0.1f ? eTrav : kTravelSpeed), 0.0f, 1.0f);
            enemyHeading += ord.steer * (1.2f * (1.0f - 0.55f * eFrac) * float(enemyProfile.turnFactor)) * dt;
            enemyWorldX += std::sin(enemyHeading) * enemySpeed * dt;
            enemyWorldZ += std::cos(enemyHeading) * enemySpeed * dt;
            enemyPose = sea::computeFloatPose(enemy, waves, timeSec, enemyWorldX, enemyWorldZ, enemyHeading);
            if (ord.fireSide != 0 && eReady && !inSafeZone) {
                auto ev = sea::fireBroadside(enemy, ord.fireSide, enemyWorldX, enemyPose.heaveY, enemyWorldZ, enemyHeading);
                enemyShots.insert(enemyShots.end(), ev.begin(), ev.end());
                enemyReload = 1.6f;
            }
        } else {
            // Struck / going down: dead in the water, coasting to a stop.
            enemySpeed += (0.0f - enemySpeed) * clampf(dt * 0.8f, 0.0f, 1.0f);
            enemyWorldX += std::sin(enemyHeading) * enemySpeed * dt;
            enemyWorldZ += std::cos(enemyHeading) * enemySpeed * dt;
            enemyPose = sea::computeFloatPose(enemy, waves, timeSec, enemyWorldX, enemyWorldZ, enemyHeading);
        }
        const sea::Stats enemyStats = sea::getShipStats(enemy);
        if (enemyStats.sinking) enemyStruck = true; // floods to the waterline -> strikes her colours

        // Our broadside (Space) fires from whichever side faces the enemy.
        reload = clampf(reload - dt, 0.0f, 10.0f);
        if (wantFire && reload <= 0.0f && !buildMode && !inSafeZone) {
            const float dxE = enemyWorldX - worldX, dzE = enemyWorldZ - worldZ;
            const float starboardComp = dxE * std::cos(heading) - dzE * std::sin(heading);
            const int side = starboardComp >= 0.0f ? 1 : -1;
            auto volley = sea::fireBroadside(ship, side, worldX, pose.heaveY, worldZ, heading);
            shots.insert(shots.end(), volley.begin(), volley.end());
            reload = 1.2f;
        }
        wantFire = false;

        // Advance both volleys; ours flood the enemy, theirs flood us.
        sea::stepProjectiles(shots, dt);
        sea::stepProjectiles(enemyShots, dt);
        if (!enemyGone && !enemyStruck) // struck/surrendered ships take no more damage; enemy is never in the zone
            sea::resolveHits(shots, enemy, enemyWorldX, enemyPose.heaveY, enemyWorldZ, enemyHeading);
        if (!buildMode && !inSafeZone) sea::resolveHits(enemyShots, ship, worldX, pose.heaveY, worldZ, heading);
        auto dead = [](const sea::Projectile& p) { return !p.alive; };
        shots.erase(std::remove_if(shots.begin(), shots.end(), dead), shots.end());
        enemyShots.erase(std::remove_if(enemyShots.begin(), enemyShots.end(), dead), enemyShots.end());

        // A captured prize stops foundering; otherwise a struck ship slowly goes down.
        if (enemyStats.sinking && !captured) enemySink += dt * clampf(std::fabs(float(enemyStats.floatMargin)) / 1200.0f, 0.3f, 3.0f);
        enemyPose.heaveY -= enemySink;

        // Endless zone patrol: once the current enemy is sunk or taken, a fresh one
        // scaled to the player's zone sails in after a beat — but never while the
        // player is holed up in the harbour truce (peace holds there).
        const bool enemyResolved = enemyGone || captured;
        if (enemyResolved && !buildMode) {
            respawnTimer += dt;
            if (respawnTimer > 4.0f && !inSafeZone) spawnEnemyForZone();
        } else if (!enemyResolved) {
            respawnTimer = 0.0f;
        }

        // Boarding: pull alongside a struck (surrendered) enemy at low speed, press B.
        const float bdx = enemyWorldX - worldX, bdz = enemyWorldZ - worldZ;
        const float enemyRange = std::sqrt(bdx * bdx + bdz * bdz);
        const bool boardable = enemyStruck && !captured && !enemyGone && enemyRange < 12.0f && speed < 4.5f;
        if (wantBoard && boardable && !boarding) { boarding = true; boardTimer = 1.5f; }
        wantBoard = false;
        if (boarding) { boardTimer -= dt; if (boardTimer <= 0.0f) { boarding = false; captured = true; } }
        const bool weSank = sinkDepth > 22.0f;

        // Build mode: the ship's pieces in this tradition's construction order.
        std::vector<int> border;
        if (buildMode) {
            border = sea::buildOrder(ship, sea::BuildTradition(buildTrad));
            placed = std::max(0, std::min(placed, int(border.size())));
        }
        // Launch bake: after the "calculating" beat, freeze the hull's physics profile.
        if (launching) {
            launchTimer -= dt;
            if (launchTimer <= 0.0f) { launching = false; launched = true; activeProfile = sea::bakeHullProfile(ship); }
        }
        // On-foot: walk the yard (WASD move, A/D turn), kept on the dock.
        if (buildMode && walkMode) {
            const bool* wk = SDL_GetKeyboardState(nullptr);
            float mv = 0.0f, tn = 0.0f;
            if (!ImGui::GetIO().WantCaptureKeyboard) {
                if (wk[SDL_SCANCODE_W]) mv += 1.0f;
                if (wk[SDL_SCANCODE_S]) mv -= 1.0f;
                if (wk[SDL_SCANCODE_D]) tn += 1.0f;
                if (wk[SDL_SCANCODE_A]) tn -= 1.0f;
            }
            charHeading += tn * 2.2f * dt;
            charX += std::sin(charHeading) * mv * 3.4f * dt;
            charZ += std::cos(charHeading) * mv * 3.4f * dt;
            if (onDeck) {
                // Stay on the deck of the ship (at the origin on the stocks).
                const float hw = float(ship.bounds.width) * 0.5f - 0.5f;
                const float hl = float(ship.bounds.length) * 0.5f - 0.5f;
                charX = clampf(charX, -hw, hw);
                charZ = clampf(charZ, -hl, hl);
                charY = ship_view::deckStandHeight(ship);
            } else {
                charX = clampf(charX, -26.0f, 26.0f);
                charZ = clampf(charZ, -20.0f, 26.0f);
                charY = 1.8f; // dock top
            }
            if (mv != 0.0f) walkPhase += dt * 9.0f;
        }

        imgui_bgfx::beginFrame(width, height, dt, mouseX, mouseY, mouseButtons, wheel);

        if (!hideUI) {
        ImGui::SetNextWindowPos(ImVec2(24, 24), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(440, 460), ImGuiCond_FirstUseEver);
        ImGui::Begin("Sea Trial - Milestone 0"); // ASCII: default ImGui font has no em-dash glyph
        ImGui::TextUnformatted("Engineless native C++ build");
        ImGui::Text("Renderer: %s", bgfx::getRendererName(bgfx::getRendererType()));
        ImGui::TextColored(stats.sinking ? kRed : kGreen, stats.sinking ? "Status: SINKING" : "Status: afloat");
        if (captured)      ImGui::TextColored(kGreen, ">>> PRIZE TAKEN - enemy boarded & captured <<<");
        else if (enemyGone) ImGui::TextColored(kGreen, ">>> ENEMY SUNK <<<");
        else if (weSank)   ImGui::TextColored(kRed, ">>> DEFEAT - you sank <<<");
        ImGui::Separator();
        if (ImGui::CollapsingHeader("Controls", ImGuiTreeNodeFlags_DefaultOpen)) {
            float cargo = float(ship.systems.cargo_mass);
            if (ImGui::SliderFloat("Cargo (kg)", &cargo, 0.0f, 3000.0f, "%.0f"))
                ship.systems.cargo_mass = cargo;
            if (ImGui::Button("Damage plank")) damagePlank();
            ImGui::SameLine();
            if (ImGui::Button("Repair")) repairAll();
            ImGui::SameLine();
            if (ImGui::Button("Reset")) resetShip();
            ImGui::TextDisabled("Keys: W raise sail (hold at full = travel), S lower, A/D steer");
            ImGui::TextDisabled("      Space fire, B board  ·  C/V cargo, X damage, Z repair, R reset");
        }
        if (ImGui::CollapsingHeader("Build mode", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Guided assembly (freeze & build)", &buildMode);
            const char* trads[] = { "Roman", "Viking", "English (Age of Sail)" };
            ImGui::Combo("Tradition", &buildTrad, trads, 3);
            ImGui::TextDisabled("%s", sea::traditionName(sea::BuildTradition(buildTrad)));
            if (buildMode) {
                ImGui::Checkbox("Walk the yard (on foot)", &walkMode);
                if (walkMode) {
                    if (!onDeck) {
                        if (ImGui::Button("Board ship (up the gangplank)")) {
                            onDeck = true; charX = 0.0f; charZ = -float(ship.bounds.length) * 0.3f;
                            charHeading = 0.0f; charY = ship_view::deckStandHeight(ship);
                        }
                        ImGui::TextColored(kGreen, "  On the dock - WASD walk, A/D turn.");
                    } else {
                        if (ImGui::Button("Go ashore")) {
                            onDeck = false; charX = 9.0f; charZ = -11.0f; charHeading = -0.7f; charY = 1.8f;
                        }
                        ImGui::TextColored(kGreen, "  On deck - WASD walk, A/D turn.");
                    }
                }
                ImGui::Separator();
                // Shape the hull (until freeform placement exists, drive L/B/depth).
                bool shapeChanged = false;
                float shL = float(cfg.length), shB = float(cfg.width), shD = float(cfg.depth);
                if (ImGui::SliderFloat("Length (m)", &shL, 8.0f, 24.0f, "%.1f")) { cfg.length = shL; shapeChanged = true; }
                if (ImGui::SliderFloat("Beam (m)",   &shB, 3.0f, 9.0f, "%.1f"))  { cfg.width = shB;  shapeChanged = true; }
                if (ImGui::SliderFloat("Depth (m)",  &shD, 2.0f, 3.6f, "%.1f"))  { cfg.depth = shD;  shapeChanged = true; }
                if (shapeChanged) { ship = sea::makeShipFromConfig(cfg); placed = 0; launched = false; }

                // Submit for launch -> bake the water-physics profile.
                if (launching) {
                    ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.2f, 1.0f), "Calculating hull profile...");
                } else if (ImGui::Button("Submit for launch (bake profile)")) {
                    launching = true; launchTimer = 1.6f;
                }
                if (!launched && !launching)
                    ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "Hull changed - submit to re-bake its profile.");

                // The baked profile — what the boat will sail like.
                ImGui::Separator();
                ImGui::Text("Hull:  %.1f x %.1f m, draft %.1f", activeProfile.length, activeProfile.beam, activeProfile.draft);
                ImGui::Text("Displaces: %.1f t", activeProfile.displacementMass / 1000.0);
                const char* stab = activeProfile.gm < 0.0 ? "TENDER (tippy)"
                                 : (activeProfile.gm < 0.4 ? "lively" : (activeProfile.gm < 1.0 ? "stable" : "stiff"));
                ImGui::Text("Stability: %s (GM %.2f)", stab, activeProfile.gm);
                ImGui::Text("Top speed: %.0f%%   Turn: %.0f%%",
                            activeProfile.topSpeedFactor * 100.0, activeProfile.turnFactor * 100.0);
                ImGui::Separator();

                const int total = int(border.size());
                ImGui::Text("Placed: %d / %d pieces", placed, total);
                if (ImGui::Button("Lay next")) placed = std::min(placed + 1, total);
                ImGui::SameLine(); if (ImGui::Button("Strike last")) placed = std::max(placed - 1, 0);
                ImGui::SameLine(); if (ImGui::Button("Build all")) placed = total;
                ImGui::SameLine(); if (ImGui::Button("Clear")) placed = 0;
                ImGui::SliderInt("Reveal", &placed, 0, total);
                const std::vector<std::string> seq = sea::buildSequence(sea::BuildTradition(buildTrad));
                const int step = total > 0
                    ? std::min(int(seq.size()) - 1, placed * int(seq.size()) / total) : 0;
                ImGui::Separator();
                for (int i = 0; i < int(seq.size()); ++i)
                    ImGui::TextColored(i == step ? kGreen : ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                                       "%d. %s", i + 1, seq[i].c_str());
            } else {
                ImGui::TextDisabled("Enable to watch a hull assemble plank by plank.");
            }
        }
        if (ImGui::CollapsingHeader("Sailing", ImGuiTreeNodeFlags_DefaultOpen)) {
            const char* tierName = effTier == 0 ? "anchored"
                                 : (effTier == 1 ? "half sail"
                                 : (effTier == 2 ? "full sail" : "TRAVEL SPEED"));
            ImGui::Text("Sails:   %s", tierName);
            ImGui::Text("Speed:   %.1f", speed);
            ImGui::TextColored(rig < 0.6f ? kRed : (rig < 0.99f ? ImVec4(1.0f, 0.8f, 0.3f, 1.0f) : kGreen),
                               "Rig:     %.0f%%", rig * 100.0f);
            ImGui::Text("Heading: %.0f deg", heading * 57.2957795f);
            ImGui::Text("Wind:    %.0f deg off bow  (%s)", awaDeg, sea::pointOfSail(awaDeg));
            ImGui::TextColored(windFactor < 0.08f ? kRed : kGreen, "Drive:   %.0f%%%s",
                               windFactor * 100.0f, windFactor < 0.08f ? "   in irons - bear away!" : "");
            ImGui::Text("Port:    %.0f m", islandDist);
            if (inSafeZone) ImGui::TextColored(kGreen, ">> SAFE ZONE - harbour truce <<");
            else {
                const int zlvl = sea::zoneLevel(zones, worldX, worldZ);
                ImGui::TextColored(zlvl >= 4 ? kRed : kGreen, "Zone:    level %d waters (%s patrol)",
                                   zlvl, sea::zoneShipName(zlvl));
            }
            const char* hs = activeProfile.gm < 0.0 ? "tender" : (activeProfile.gm < 0.4 ? "lively"
                            : (activeProfile.gm < 1.0 ? "stable" : "stiff"));
            ImGui::Text("Hull:    %.0f%% speed, %.0f%% turn (%s)",
                        activeProfile.topSpeedFactor * 100.0, activeProfile.turnFactor * 100.0, hs);
        }
        if (ImGui::CollapsingHeader("Gunnery & boarding", ImGuiTreeNodeFlags_DefaultOpen)) {
            const char* est = captured ? "captured" : (enemyGone ? "sunk"
                            : (enemyStruck ? "STRUCK - boardable" : (enemyStats.sinking ? "SINKING" : "afloat")));
            ImGui::Text("Enemy:   %s (level %d)", enemy.display_name.c_str(), enemyLevel);
            ImGui::TextColored(captured ? kGreen
                               : ((enemyGone || enemyStruck || enemyStats.sinking) ? kRed : kGreen),
                               "         %s (dmg %.0f%%)", est, enemyStats.damageRatio * 100.0);
            if (enemyResolved && !inSafeZone) ImGui::Text("         next patrol inbound...");
            ImGui::Text("Range:   %.0f m", enemyRange);
            ImGui::Text("Incoming: %d shots", int(enemyShots.size()));
            if (inSafeZone)     ImGui::TextColored(kGreen, "Safe zone - no combat");
            else if (boarding)  ImGui::TextColored(kGreen, "Boarding...");
            else if (boardable) ImGui::TextColored(kGreen, "Alongside - press B to board!");
            else                ImGui::TextDisabled(reload > 0.0f ? "Reloading..." : "Space: fire broadside");
        }
        ImGui::Separator();
        ImGui::TextColored(passing == total ? kGreen : kRed, "Model self-tests: %d / %d passing", passing, total);
        if (ImGui::CollapsingHeader("Self-tests")) {
            for (const auto& r : results)
                ImGui::TextColored(r.pass ? kGreen : kRed, "%s  %s", r.pass ? "PASS" : "FAIL", r.name.c_str());
        }
        ImGui::Separator();
        if (ImGui::CollapsingHeader("Test Sloop - stats", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("Pieces:       %d", stats.pieceCount);
            ImGui::Text("Mass:         %.0f kg", stats.mass);
            ImGui::Text("Buoyancy:     %.0f kg-eq", stats.buoyancyScore);
            ImGui::Text("Cargo:        %.0f kg", stats.cargoMass);
            ImGui::Text("Damage:       %.0f%%", stats.damageRatio * 100.0);
            ImGui::TextColored(stats.floatMargin > 0 ? kGreen : kRed, "Float margin: %.0f kg", stats.floatMargin);
        }
        if (ImGui::CollapsingHeader("Buoyancy (live)", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("Heave: %+.2f m", pose.heaveY);
            ImGui::Text("Pitch: %+.2f deg", pose.pitch * 57.2957795);
            ImGui::Text("Heel:  %+.2f deg", pose.heel * 57.2957795);
        }
        ImGui::Separator();
        ImGui::Text("Frame %d   %.1f FPS", frame, ImGui::GetIO().Framerate);
        ImGui::TextDisabled("Orbit camera - Esc to quit");
        ImGui::End();
        } // !hideUI

        // Wind vane HUD (hidden while building on the stocks, or capturing a clean shot).
        if (!buildMode && !hideUI) {
            auto norm = [](float a) {
                while (a > 3.14159265f) a -= 6.28318531f;
                while (a <= -3.14159265f) a += 6.28318531f;
                return a;
            };
            const float kTrueWind = 12.0f;
            const float trueSrcRel = norm((windDir + 3.14159265f) - heading);
            const sea::ApparentWind aw = sea::apparentWind(windDir, kTrueWind, heading, speed);
            const float appSrcRel = norm((float(aw.dir) + 3.14159265f) - heading);
            drawWindVane(width, trueSrcRel, appSrcRel, awaDeg, windFactor, float(aw.speed),
                         kTrueWind, sea::pointOfSail(awaDeg), windFactor < 0.08f, timeSec);
        }

        // 3D scene. In build mode: only the pieces placed so far, on calm water,
        // in this tradition's construction order (sailing & combat frozen).
        if (buildMode) {
            sea::Ship shown = ship;
            shown.pieces.clear();
            const int reveal = walkMode ? int(border.size()) : placed; // full ship to walk around
            for (int i = 0; i < reveal && i < int(border.size()); ++i)
                shown.pieces.push_back(ship.pieces[border[i]]);
            const bool complete = reveal >= int(border.size());
            if (!complete) { shown.systems.mast_count = 0; shown.systems.sail_count = 0; }
            const float buildOrbit = shotMode ? shotHead : timeSec * 0.12f; // --head frames the shot
            // Shadow pass: the hull on the stocks casts onto the dock.
            shadow::beginPass(kShadowView, kSunX, kSunY, kSunZ, 0.0f, 0.0f, 34.0f);
            sea::FloatPose bp; bp.heaveY = 2.9f + float(ship.bounds.depth) * 0.55f;
            ship_mesh::renderDepth(kShadowView, shown, bp, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f);
            ship_view::renderBuildScene(kClearView, shown, waves, timeSec, buildOrbit, width, height,
                                        walkMode, charX, charY, charZ, charHeading, walkPhase);
        } else {
            // Shadow depth pass: the sun renders the ship(s) into the shadow map
            // (view 0, before the scene) so they cast real shadows on the sea.
            shadow::beginPass(kShadowView, kSunX, kSunY, kSunZ, 0.0f, 0.0f, 30.0f);
            ship_mesh::renderDepth(kShadowView, ship, pose, heading, windDir, sailFullness, 0.0f, 0.0f, float(activeProfile.heelFactor));
            if (!enemyGone)
                ship_mesh::renderDepth(kShadowView, enemy, enemyPose, enemyHeading, windDir,
                                       enemyStruck ? 0.0f : 0.75f, enemyWorldX - worldX, enemyWorldZ - worldZ);
            ship_view::render(kClearView, ship, waves, pose, timeSec, heading, worldX, worldZ, windDir, sailFullness, width, height, float(activeProfile.heelFactor), islandX, islandZ, kLandCutRadius, speed);
            if (!enemyGone) {
                ship_view::renderShip(kClearView, enemy, enemyPose, enemyHeading, windDir,
                                      enemyStruck ? 0.0f : 0.75f, timeSec, // furled sails once she strikes
                                      enemyWorldX - worldX, enemyWorldZ - worldZ);
            }
            for (const auto& p : shots)
                ship_view::renderTracer(kClearView, float(p.x) - worldX, float(p.y), float(p.z) - worldZ, 0.35f);
            for (const auto& p : enemyShots)
                ship_view::renderTracer(kClearView, float(p.x) - worldX, float(p.y), float(p.z) - worldZ, 0.35f, 1.0f, 0.25f, 0.2f);
            // The island, the safe-zone buoy ring, and a half-built hull on the slipway.
            ship_view::renderIsland(kClearView, islX, islZ);
            const int kBuoys = 36;
            for (int i = 0; i < kBuoys; ++i) {
                const float a = 6.2831853f * i / kBuoys;
                const float bxo = islandX + kSafeRadius * std::cos(a) - worldX;
                const float bzo = islandZ + kSafeRadius * std::sin(a) - worldZ;
                const float byo = 0.8f + 0.3f * std::sin(timeSec * 1.5f + i);
                ship_view::renderTracer(kClearView, bxo, byo, bzo, 0.6f, 0.86f, 0.42f, 0.12f); // weathered nav-buoy orange
            }
            sea::FloatPose yardPose; yardPose.heaveY = 1.9f;
            ship_view::renderShip(kClearView, yardShip, yardPose, 0.15f, windDir, 0.0f, timeSec,
                                  islX + 24.0f, islZ - 44.0f);
        }
        imgui_bgfx::endFrame(kImGuiView);

        // Queue the screenshot a few frames before exit so bgfx has frames to deliver it.
        if (shotMode && !shotRequested && frame >= shotAt) {
            bgfx::requestScreenShot(BGFX_INVALID_HANDLE, shotPath.c_str());
            shotRequested = true;
        }
        bgfx::frame();

        if (maxFrames >= 0 && ++frame >= maxFrames) running = false;
    }

    shadow::shutdown();
    ship_view::shutdown();
    imgui_bgfx::shutdown();
    bgfx::shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
    std::printf("clean exit after %d frames\n", frame);
    return 0;
}
