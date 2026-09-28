// Sea Trial — native self-test runner (Milestone 0). Public domain (The Unlicense).
//
// Runs the 11 model-spine checks ported from the Three.js spike and prints a
// PASS/FAIL report. Exit code 0 iff all pass — so it doubles as a CI gate.
#include "camera_orbit.h"
#include "ship_model.hpp"

#include <cmath>
#include <cstdio>

namespace {

int runOrbitChecks() {
    int pass = 0, total = 0;
    auto check = [&](const char* name, bool ok, const char* details) {
        ++total;
        if (ok) {
            std::printf("PASS: %s\n", name);
            ++pass;
        } else {
            std::printf("FAIL: %s%s%s\n", name, details[0] ? " — " : "", details);
        }
    };
    auto near = [](float a, float b, float eps = 1e-3f) { return std::fabs(a - b) < eps; };
    auto near3 = [&](ship_cam::Vec3 v, float x, float y, float z) {
        return near(v.x, x) && near(v.y, y) && near(v.z, z);
    };

    const ship_cam::Rig sail0 = ship_cam::defaultSailRig();
    const ship_cam::Pose sailPose = ship_cam::sailLocal(sail0);
    char sailBuf[160];
    std::snprintf(sailBuf, sizeof(sailBuf),
                  "eye (%.4f, %.4f, %.4f) at (%.4f, %.4f, %.4f)",
                  sailPose.eye.x, sailPose.eye.y, sailPose.eye.z,
                  sailPose.at.x, sailPose.at.y, sailPose.at.z);
    check("Sail orbit default matches chase camera",
          near3(sailPose.eye, 0.0f, 9.0f, -24.0f) && near3(sailPose.at, 0.0f, -0.4f, 5.0f),
          sailBuf);

    const float h = 1.5707963f;
    const ship_cam::Pose turned = ship_cam::toScene(sailPose, h);
    check("Sail chase follows heading",
          near3(turned.eye, -24.0f, 9.0f, 0.0f) && near3(turned.at, 5.0f, -0.4f, 0.0f),
          "");

    ship_cam::Rig side = sail0;
    side.yaw = h;
    const ship_cam::Pose sidePose = ship_cam::sailLocal(side);
    check("Sail orbit yaw +90 puts the eye to port",
          sidePose.eye.x < -20.0f && std::fabs(sidePose.eye.z) < 1.0f && sidePose.at.x > 4.0f,
          "");

    const ship_cam::Pose buildPose = ship_cam::buildLocal(
        0.0f, ship_cam::buildDefaultPitch(), ship_cam::buildDefaultDist());
    check("Build orbit default matches berth camera",
          near3(buildPose.eye, 0.0f, 12.0f, 24.0f) && near3(buildPose.at, 0.0f, 3.2f, 0.0f),
          "");

    const float a = 0.9f;
    const ship_cam::Pose angled = ship_cam::buildLocal(
        a, ship_cam::buildDefaultPitch(), ship_cam::buildDefaultDist());
    check("Build orbit angle matches the old ring",
          near(angled.eye.x, std::sin(a) * 24.0f) && near(angled.eye.y, 12.0f)
              && near(angled.eye.z, std::cos(a) * 24.0f),
          "");

    ship_cam::Rig zoomed = sail0;
    const float before = zoomed.dist;
    ship_cam::applyOrbitInput(zoomed, 0.0f, 0.0f, 1.0f, true, ship_cam::kMinDist);
    check("Scroll up zooms in", zoomed.dist < before - 0.5f, "");

    ship_cam::Rig raised = sail0;
    const float pitch0 = raised.pitch;
    ship_cam::applyOrbitInput(raised, 0.0f, -40.0f, 0.0f, true, ship_cam::kMinDist);
    check("Drag up raises the camera", raised.pitch > pitch0 + 0.1f, "");

    ship_cam::Rig yawed = sail0;
    ship_cam::applyOrbitInput(yawed, 80.0f, 0.0f, 0.0f, true, ship_cam::kMinDist);
    check("Drag right orbits toward port",
          near(yawed.yaw, 80.0f * ship_cam::kYawSens), "");

    ship_cam::Rig pitched = sail0;
    ship_cam::applyOrbitInput(pitched, 0.0f, -100000.0f, 0.0f, true, ship_cam::kMinDist);
    check("Pitch clamps below straight down",
          std::fabs(pitched.pitch - ship_cam::kMaxPitch) < 1e-4f, "");

    ship_cam::Rig far = sail0;
    ship_cam::applyOrbitInput(far, 0.0f, 0.0f, -1000.0f, true, ship_cam::kMinDist);
    check("Zoom out clamps at max distance",
          std::fabs(far.dist - ship_cam::kMaxDist) < 1e-3f, "");

    const float minHull = ship_cam::minDistForLength(24.0f);
    ship_cam::Rig close = sail0;
    close.dist = 5.0f;
    close.pitch = 0.02f;
    ship_cam::clampOrbit(close, true, minHull);
    const ship_cam::Pose closePose = ship_cam::sailLocal(close);
    check("Close zoom stays outside a long hull and above the sea",
          close.dist + 1e-3f >= minHull && closePose.eye.y >= ship_cam::kSailEyeMinY - 1e-3f,
          "");

    ship_cam::Rig wrapped = sail0;
    wrapped.yaw = 4.0f;
    ship_cam::clampOrbit(wrapped, true, ship_cam::kMinDist);
    check("Yaw wraps into (-pi, pi]",
          wrapped.yaw <= 3.1416f && wrapped.yaw > -3.1416f, "");

    std::printf("\ncamera orbit: %d / %d passing\n", pass, total);
    return pass == total ? 0 : 1;
}

} // namespace

int main() {
    const auto results = sea::runSelfTest();

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
    const int orbitOk = runOrbitChecks();
    return (passing == total && orbitOk == 0) ? 0 : 1;
}
