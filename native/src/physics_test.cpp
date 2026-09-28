// Headless checks for the Jolt adapter. No window, no GPU.
// Public domain (The Unlicense).
#include "physics_jolt.hpp"
#include "ship_model.hpp"

#include <cmath>
#include <cstdio>
#include <string>

namespace {

int gFails = 0;
int gTotal = 0;

void check(const char* name, bool ok, const std::string& details = {}) {
    ++gTotal;
    if (ok) {
        std::printf("PASS: %s\n", name);
    } else {
        std::printf("FAIL: %s%s%s\n", name, details.empty() ? "" : " — ", details.c_str());
        ++gFails;
    }
}

bool near(float a, float b, float eps) { return std::fabs(a - b) <= eps; }

std::string fmt(const char* k, float v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s=%.4f", k, v);
    return buf;
}

void settle(sea::PhysicsWorld& world, uint32_t id, const sea::ShipDrive& drive, int frames) {
    for (int i = 0; i < frames; ++i) {
        world.driveShip(id, drive);
        world.step(1.0f / 60.0f);
    }
}

} // namespace

int main() {
    std::printf("backend: %s\n", sea::physicsBackendName());

    const sea::Ship ship = sea::makeShipFromConfig({});
    const sea::Stats stats = sea::getShipStats(ship);
    const sea::HullCollider hull = sea::hullColliderFromShip(ship);
    check("Hull collider copies the ship envelope and mass",
          near(hull.length, float(ship.bounds.length), 1.0e-4f)
              && near(hull.beam, float(ship.bounds.width), 1.0e-4f)
              && near(hull.height, float(ship.bounds.depth), 1.0e-4f)
              && near(hull.mass, float(stats.mass), 0.05f),
          "mass " + std::to_string(hull.mass) + " vs " + std::to_string(stats.mass));

    {
        sea::PhysicsWorld world;
        const uint32_t id = world.createShip(hull, 0.0f, 0.0f, 0.0f, 0.0f);
        check("Jolt world accepts a ship body", id != sea::kInvalidPhysicsBody && world.bodyCount() == 1);
        const sea::BodyExtents ext = world.bodyExtents(id);
        check("Ship body is the hull box at the ship's mass",
              near(ext.halfLength, hull.length * 0.5f, 0.02f)
                  && near(ext.halfBeam, hull.beam * 0.5f, 0.02f)
                  && near(ext.halfHeight, hull.height * 0.5f, 0.02f)
                  && near(ext.mass, hull.mass, std::max(0.5f, hull.mass * 0.001f)),
              fmt("mass", ext.mass) + " " + fmt("halfL", ext.halfLength));

        world.teleportShip(id, 3.0f, 1.5f, -2.0f, 0.4f, 0.12f, -0.18f);
        const sea::BodyPose posed = world.bodyPose(id);
        check("Teleport round-trips the buoyancy pose",
              near(posed.x, 3.0f, 1.0e-3f) && near(posed.y, 1.5f, 1.0e-3f) && near(posed.z, -2.0f, 1.0e-3f)
                  && near(posed.heading, 0.4f, 1.0e-3f) && near(posed.pitch, 0.12f, 1.0e-3f)
                  && near(posed.heel, -0.18f, 1.0e-3f),
              fmt("pitch", posed.pitch) + " " + fmt("head", posed.heading) + " " + fmt("heel", posed.heel));
    }

    {
        sea::PhysicsWorld world;
        const uint32_t id = world.createShip(hull, 0.0f, 0.0f, 0.0f, 0.0f);
        sea::ShipDrive drive;
        drive.buoyancy.heaveY = 6.0f;
        drive.buoyancy.pitch = 0.5f;
        drive.speed = 0.0f;
        world.driveShip(id, drive);
        world.step(1.0f / 60.0f);
        const sea::BodyPose one = world.bodyPose(id);
        check("One step integrates heave instead of snapping to the target",
              one.y > 0.05f && one.y < 0.4f, fmt("y", one.y));
        check("One step integrates pitch partway toward the target",
              one.pitch > 0.04f && one.pitch < 0.25f, fmt("pitch", one.pitch));
        check("A single step leaves the world contact-free", !world.shipInContact(id));
    }

    {
        const auto waves = sea::makeWaveField("jolt-slice");
        const double t = 4.0;
        const float x = 20.0f, z = -15.0f, heading = 0.35f;
        const sea::FloatPose target = sea::computeFloatPose(ship, waves, t, x, z, heading);
        sea::PhysicsWorld world;
        const uint32_t id = world.createShip(hull, x, 0.0f, z, 0.0f);
        sea::ShipDrive drive;
        drive.buoyancy.heaveY = float(target.heaveY);
        drive.buoyancy.pitch = float(target.pitch);
        drive.buoyancy.heel = float(target.heel);
        drive.heading = heading;
        drive.speed = 0.0f;
        settle(world, id, drive, 180);
        const sea::BodyPose bp = world.bodyPose(id);
        char buf[192];
        std::snprintf(buf, sizeof(buf), "y %.3f vs %.3f  pitch %.3f vs %.3f  heel %.3f vs %.3f  head %.3f",
                      bp.y, drive.buoyancy.heaveY, bp.pitch, drive.buoyancy.pitch,
                      bp.heel, drive.buoyancy.heel, bp.heading);
        check("Settled hull matches computeFloatPose",
              near(bp.y, drive.buoyancy.heaveY, 0.05f)
                  && near(bp.pitch, drive.buoyancy.pitch, 0.02f)
                  && near(bp.heel, drive.buoyancy.heel, 0.02f)
                  && near(bp.heading, heading, 0.02f)
                  && near(bp.x, x, 0.05f) && near(bp.z, z, 0.05f),
              buf);
    }

    {
        sea::PhysicsWorld world;
        const uint32_t id = world.createShip(hull, 0.0f, 1.0f, 0.0f, 0.0f);
        sea::ShipDrive drive;
        drive.buoyancy.heaveY = 1.0f;
        drive.heading = 0.0f;
        drive.speed = 8.0f;
        settle(world, id, drive, 120); // 2 s
        const sea::BodyPose ahead = world.bodyPose(id);
        check("Heading 0 integrates along +Z",
              near(ahead.z, 16.0f, 0.4f) && near(ahead.x, 0.0f, 0.15f) && !world.shipInContact(id),
              fmt("x", ahead.x) + " " + fmt("z", ahead.z));

        sea::PhysicsWorld beam;
        const uint32_t id2 = beam.createShip(hull, 0.0f, 1.0f, 0.0f, 1.5707963f);
        drive.heading = 1.5707963f;
        drive.speed = 5.0f;
        settle(beam, id2, drive, 120);
        const sea::BodyPose side = beam.bodyPose(id2);
        check("Heading +pi/2 integrates along +X",
              near(side.x, 10.0f, 0.4f) && near(side.z, 0.0f, 0.15f),
              fmt("x", side.x) + " " + fmt("z", side.z));
    }

    {
        sea::HullCollider box;
        box.length = 12.0f;
        box.beam = 4.0f;
        box.height = 2.8f;
        box.mass = 4000.0f;
        const float islandZ = 50.0f;
        const float radius = 12.0f;
        const float speed = 18.0f;
        const int frames = 180; // 3 s, free float would reach z = 54
        sea::ShipDrive drive;
        drive.buoyancy.heaveY = 1.2f;
        drive.heading = 0.0f;
        drive.speed = speed;

        sea::PhysicsWorld open;
        const uint32_t freeId = open.createShip(box, 0.0f, 1.2f, 0.0f, 0.0f);
        settle(open, freeId, drive, frames);
        const sea::BodyPose freePose = open.bodyPose(freeId);

        sea::PhysicsWorld blocked;
        const uint32_t shipId = blocked.createShip(box, 0.0f, 1.2f, 0.0f, 0.0f);
        sea::IslandCollider land;
        land.centerX = 0.0f;
        land.centerZ = islandZ;
        land.radius = radius;
        const uint32_t landId = blocked.createIsland(land);
        settle(blocked, shipId, drive, frames);
        const sea::BodyPose hit = blocked.bodyPose(shipId);
        const float shore = islandZ - radius - box.length * 0.5f; // front face against the cylinder
        char buf[192];
        std::snprintf(buf, sizeof(buf), "hit z=%.3f shore=%.3f free z=%.3f contact=%d bodies=%d",
                      hit.z, shore, freePose.z, blocked.shipInContact(shipId) ? 1 : 0, blocked.bodyCount());
        check("Island cylinder stops the hull short of open water",
              landId != sea::kInvalidPhysicsBody
                  && blocked.bodyCount() == 2
                  && blocked.shipInContact(shipId)
                  && hit.z < freePose.z - 10.0f
                  && hit.z < shore + 1.5f
                  && hit.z > shore - 3.0f
                  && near(hit.y, drive.buoyancy.heaveY, 0.3f)
                  && !open.shipInContact(freeId),
              buf);
    }

    const int passing = gTotal - gFails;
    std::printf("\n%d / %d passing\n", passing, gTotal);
    return gFails == 0 ? 0 : 1;
}
