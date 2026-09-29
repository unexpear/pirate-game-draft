// Sea Trial — native self-test runner (Milestone 0). Public domain (The Unlicense).
//
// Runs the model-spine checks and the shadow-cascade focus checks, and prints a
// PASS/FAIL report. Exit code 0 iff all pass — so it doubles as a CI gate.
#include "ship_model.hpp"
#include "shadow_focus.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

struct Check { std::string name; bool pass; std::string details; };

void addShadowChecks(std::vector<Check>& out) {
    const shadow::LightBasis basis = shadow::lightBasis(shadow::kSunX, shadow::kSunY, shadow::kSunZ);
    auto push = [&](const std::string& name, bool pass, const std::string& details = "") {
        out.push_back({ name, pass, details });
    };
    auto dot3 = [](const float a[3], const float b[3]) {
        return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    };

    // The basis is the left-handed look-at the GPU uses, so "inside the square"
    // here is the same test the shader's UV bounds apply.
    const float rr = dot3(basis.right, basis.right);
    const float ortho = std::fabs(dot3(basis.right, basis.up)) + std::fabs(dot3(basis.right, basis.view));
    push("Shadow light basis is orthonormal", std::fabs(rr - 1.0f) < 1.0e-4f && ortho < 1.0e-4f);

    const float shipTexel = shadow::texelMeters(shadow::kShipRadius);
    const float islandTexel = shadow::texelMeters(shadow::kIslandRadius);
    push("Ship cascade keeps the 30 m / 2048 texel", std::fabs(shipTexel - (60.0f / 2048.0f)) < 1.0e-6f);
    push("Island cascade is coarser than the ship cascade", islandTexel > shipTexel);
    push("Island cascade stays under 13 cm texels", islandTexel < 0.13f);

    // Offshore: the old single map (radius 30) left the island unshadowed past
    // ~230 m. Cascade 1 has to cover it, and cascade 0 must stay tight.
    {
        const shadow::FrameFocus far = shadow::planFrame(0.0f, 400.0f);
        push("Offshore ship map stays radius 30", far.ship.active && far.ship.radius == shadow::kShipRadius
             && far.ship.cx == 0.0f && far.ship.cz == 0.0f);
        push("Offshore island map is on and centred on the island",
             far.island.active && far.island.cx == 0.0f && far.island.cz == 400.0f
             && far.island.radius == shadow::kIslandRadius);
        push("Offshore town is not redrawn into the ship map", !far.townInShipMap && !far.hullsInIslandMap);
        const bool shipLit = shadow::inMap(basis, far.ship, 0.0f, 2.0f, 0.0f);
        const bool islandInShip = shadow::inMap(basis, far.ship, 0.0f, 4.0f, 400.0f);
        const bool islandLit = shadow::inMap(basis, far.island, 0.0f, 4.0f, 400.0f);
        push("Offshore hull is inside the ship map only", shipLit && !islandInShip && islandLit);
        // Mean coastline, on the near side, plus a low roof — the town the old
        // map dropped.
        const float coastZ = 400.0f - shadow::kIslandLandDisk;
        const bool coast = shadow::inMap(basis, far.island, 0.0f, 8.0f, coastZ);
        const bool coastInShip = shadow::inMap(basis, far.ship, 0.0f, 8.0f, coastZ);
        push("Offshore coastline is inside the island map", coast && !coastInShip);
        // Worst-case point of the land disk: straight along the light's X axis.
        const float lx = basis.right[0] * shadow::kIslandLandDisk;
        const float lz = basis.right[2] * shadow::kIslandLandDisk;
        push("Land disk fits the island square along the light axis",
             shadow::inMap(basis, far.island, lx, 12.0f, 400.0f + lz));
        const float hx = basis.right[0] * shadow::kIslandHeightfield;
        const float hz = basis.right[2] * shadow::kIslandHeightfield;
        push("Heightfield disk fits the island square along the light axis",
             shadow::inMap(basis, far.island, hx, 15.0f, 400.0f + hz));
        // Gunnery engages at 28 m — that fight stays in the tight map.
        const float ex = basis.right[0] * 28.0f;
        const float ez = basis.right[2] * 28.0f;
        push("A 28 m broadside stays inside the ship map",
             shadow::inMap(basis, far.ship, ex, 2.0f, ez));
    }

    // Approaching: quality on the hull does not collapse the way the old widened
    // map did (radius grew to dist/2 + 72). The town is drawn into BOTH maps
    // while a caster on the island can still project into the ship square.
    {
        const shadow::FrameFocus near = shadow::planFrame(0.0f, 80.0f);
        push("Near the island the ship map is still radius 30",
             near.ship.active && near.ship.radius == shadow::kShipRadius);
        push("Near the island the town is drawn into the ship map",
             near.island.active && near.townInShipMap && near.hullsInIslandMap);
        push("Quay beside the hull is inside the ship map",
             shadow::inMap(basis, near.ship, 0.0f, 3.0f, 20.0f));
        push("Island centre is outside the ship map and inside the island map",
             !shadow::inMap(basis, near.ship, 0.0f, 6.0f, 80.0f)
             && shadow::inMap(basis, near.island, 0.0f, 6.0f, 80.0f));
    }

    {
        const float reach = shadow::kShipRadius + shadow::kIslandRadius + shadow::kCasterMargin;
        const shadow::FrameFocus edge = shadow::planFrame(0.0f, reach);
        const shadow::FrameFocus past = shadow::planFrame(0.0f, reach + 1.0f);
        push("Town shares the ship map out to the caster reach", edge.townInShipMap && edge.island.active);
        push("Past caster reach the island map stands alone", !past.townInShipMap && past.island.active);
    }

    {
        const shadow::FrameFocus atFar = shadow::planFrame(shadow::kIslandVisibleRange, 0.0f);
        const shadow::FrameFocus beyond = shadow::planFrame(shadow::kIslandVisibleRange + 1.0f, 0.0f);
        push("Island cascade stays on through the visible range", atFar.island.active && atFar.ship.active);
        push("Island cascade turns off past the camera far plane", !beyond.island.active && beyond.ship.active
             && !beyond.townInShipMap && !beyond.hullsInIslandMap);
        push("Past the far plane the hull is still shadowed",
             shadow::inMap(basis, beyond.ship, 0.0f, 2.0f, 0.0f));
    }

    {
        const shadow::FrameFocus iso = shadow::planFrame(10.0f, 10.0f, 22.0f);
        push("Isolation uses one map and no island cascade",
             iso.ship.active && iso.ship.radius == 22.0f && !iso.island.active && iso.townInShipMap
             && !iso.hullsInIslandMap);
        push("An isolated building's centre is inside its map",
             shadow::inMap(basis, iso.ship, 0.0f, 4.0f, 0.0f));
    }
}

} // namespace

int main() {
    std::vector<Check> results;
    for (const auto& r : sea::runSelfTest())
        results.push_back({ r.name, r.pass, r.details });
    addShadowChecks(results);

    int passing = 0;
    for (const auto& r : results) {
        if (r.pass) {
            std::printf("PASS: %s\n", r.name.c_str());
            ++passing;
        } else {
            std::printf("FAIL: %s%s%s\n", r.name.c_str(),
                        r.details.empty() ? "" : " — ", r.details.c_str());
        }
    }

    const int total = static_cast<int>(results.size());
    std::printf("\n%d / %d passing\n", passing, total);
    return passing == total ? 0 : 1;
}
