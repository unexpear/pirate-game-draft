// Sea Trial — Jolt physics adapter. Public domain (The Unlicense).
//
// Boundary: the model spine stays authoritative for gameplay.
//   * sea::computeFloatPose (Gerstner sampling + float margin) produces the
//     heave / pitch / heel target. This adapter does not sample waves.
//   * The sail model produces heading and speed. Thrust is not a Jolt force.
//   * Jolt integrates the player hull and resolves contact. A static cylinder
//     is the island shore, so the hull box cannot sail through land.
//
// Gravity is off on the hull. Vertical motion is a damped chase of the
// buoyancy target, not displaced-volume hydrostatics. Orientation chases the
// same target (and the commanded heading) instead of tumbling on impact.
// Enemy ships, characters, sails, and ropes are not rigid bodies.
#pragma once

#include <cstdint>
#include <memory>

namespace sea {

struct Ship;

// Matches JPH::BodyID::cInvalidBodyID.
constexpr uint32_t kInvalidPhysicsBody = 0xffffffffu;

// Axis-aligned hull box, centered on the ship origin. X is beam, Y is up,
// Z is length (the game's forward axis when heading is 0).
struct HullCollider {
    float length = 1;
    float beam = 1;
    float height = 1;
    float mass = 1;
};

// Same signs as sea::FloatPose: pitch nose-up, heel starboard-up, radians.
struct BuoyancyTarget {
    float heaveY = 0;
    float pitch = 0;
    float heel = 0;
};

// One frame of inputs. Call driveShip before step.
struct ShipDrive {
    BuoyancyTarget buoyancy;
    float heading = 0; // 0 faces +Z; positive heading turns toward +X
    float speed = 0;   // metres/second along heading
};

struct BodyPose {
    float x = 0, y = 0, z = 0;
    float pitch = 0, heading = 0, heel = 0;
    float vx = 0, vy = 0, vz = 0;
    float speed = 0; // horizontal speed
};

struct BodyExtents {
    float halfBeam = 0;
    float halfHeight = 0;
    float halfLength = 0;
    float mass = 0;
};

// Vertical cylinder standing in for the island shoreline.
struct IslandCollider {
    float centerX = 0;
    float centerZ = 0;
    float radius = 56;
    float height = 80; // full height, centered on y = 0
};

// Owns one Jolt world. Not copyable: the simulation holds internal listeners.
// A process may construct worlds one after another; Jolt's type registry is
// process-lifetime and shared.
class PhysicsWorld {
public:
    PhysicsWorld();
    ~PhysicsWorld();
    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;
    PhysicsWorld(PhysicsWorld&&) = delete;
    PhysicsWorld& operator=(PhysicsWorld&&) = delete;

    // Dynamic hull. Returns kInvalidPhysicsBody if the world refuses the body.
    uint32_t createShip(const HullCollider& hull, float x, float y, float z, float heading);
    uint32_t createIsland(const IslandCollider& island);

    // Remember the buoyancy target and the sail velocity. step() reapplies
    // them on every fixed substep so contact can still change position.
    void driveShip(uint32_t ship, const ShipDrive& drive);

    // Place the hull, matching the buoyancy pose exactly (startup / reset).
    void teleportShip(uint32_t ship, float x, float y, float z,
                      float heading, float pitch, float heel);

    // Advance the world by dt seconds, in fixed 1/60 s substeps (at most 8).
    void step(float dt);

    BodyPose bodyPose(uint32_t body) const;
    BodyExtents bodyExtents(uint32_t body) const;
    // True if this hull touched a static collider during the last step that
    // actually simulated. A frame that accumulates no substep keeps the
    // previous answer so a resting contact does not flicker.
    bool shipInContact(uint32_t ship) const;
    int bodyCount() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Hull box from the ship envelope. Height is bounds.depth (the float model's
// hull), not the mast. Mass is getShipStats().mass at the moment of the call;
// later cargo edits change the buoyancy target, not this mass. The island is
// static, so that mass does not affect grounding.
HullCollider hullColliderFromShip(const Ship& ship);

// "Jolt X.Y.Z", from the vendored headers. No display, no world required.
const char* physicsBackendName();

} // namespace sea
