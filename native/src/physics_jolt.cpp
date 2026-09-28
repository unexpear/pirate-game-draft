// Sea Trial — Jolt physics adapter. Public domain (The Unlicense).
#include "physics_jolt.hpp"

#include "ship_model.hpp"

#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/IssueReporting.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cmath>
#include <vector>

JPH_SUPPRESS_WARNINGS

namespace {

using namespace JPH;

constexpr float kFixedDt = 1.0f / 60.0f;
constexpr int kMaxSubsteps = 8;
// Critically-damped chase: time constant is about 1/gain seconds.
constexpr float kHeaveGain = 8.0f;
constexpr float kOriGain = 24.0f;
constexpr float kMaxHeaveSpeed = 8.0f;
constexpr float kMaxAngSpeed = 8.0f;
constexpr uint64 kShipUserData = 1;

float clampf(float v, float lo, float hi) { return std::max(lo, std::min(hi, v)); }

static void TraceImpl(const char* fmt, ...) {
    va_list list;
    va_start(list, fmt);
    char buffer[1024];
    std::vsnprintf(buffer, sizeof(buffer), fmt, list);
    va_end(list);
    std::fputs(buffer, stderr);
    std::fputc('\n', stderr);
}

#ifdef JPH_ENABLE_ASSERTS
static bool AssertFailedImpl(const char* expr, const char* message, const char* file, uint line) {
    std::fprintf(stderr, "Jolt assert %s:%u (%s) %s\n", file, line, expr, message ? message : "");
    return false; // log and continue; the self-test checks the outcome
}
#endif

void ensureJolt() {
    static bool ready = false;
    if (ready) return;
    RegisterDefaultAllocator();
    Trace = TraceImpl;
    JPH_IF_ENABLE_ASSERTS(AssertFailed = AssertFailedImpl;)
    Factory::sInstance = new Factory();
    RegisterTypes();
    ready = true;
}

namespace Layers {
constexpr ObjectLayer NON_MOVING = 0;
constexpr ObjectLayer MOVING = 1;
constexpr ObjectLayer NUM_LAYERS = 2;
}

namespace BroadPhaseLayers {
constexpr BroadPhaseLayer NON_MOVING(0);
constexpr BroadPhaseLayer MOVING(1);
constexpr uint NUM_LAYERS(2);
}

class BPLayerInterfaceImpl final : public BroadPhaseLayerInterface {
public:
    BPLayerInterfaceImpl() {
        objectToBroadPhase_[Layers::NON_MOVING] = BroadPhaseLayers::NON_MOVING;
        objectToBroadPhase_[Layers::MOVING] = BroadPhaseLayers::MOVING;
    }
    uint GetNumBroadPhaseLayers() const override { return BroadPhaseLayers::NUM_LAYERS; }
    BroadPhaseLayer GetBroadPhaseLayer(ObjectLayer layer) const override {
        return objectToBroadPhase_[layer];
    }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(BroadPhaseLayer) const override { return "Layer"; }
#endif
private:
    BroadPhaseLayer objectToBroadPhase_[Layers::NUM_LAYERS];
};

class ObjectVsBroadPhaseLayerFilterImpl : public ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(ObjectLayer layer1, BroadPhaseLayer layer2) const override {
        switch (layer1) {
        case Layers::NON_MOVING: return layer2 == BroadPhaseLayers::MOVING;
        case Layers::MOVING: return true;
        default: return false;
        }
    }
};

class ObjectLayerPairFilterImpl : public ObjectLayerPairFilter {
public:
    bool ShouldCollide(ObjectLayer object1, ObjectLayer object2) const override {
        switch (object1) {
        case Layers::NON_MOVING: return object2 == Layers::MOVING;
        case Layers::MOVING: return true;
        default: return false;
        }
    }
};

// Game axes: +Z is forward at heading 0, +heading yaws toward +X (nose-up pitch
// is the opposite of Jolt's right-handed +X rotation). Heel lifts +X.
Quat shipOrientation(float heading, float pitch, float heel) {
    return Quat::sRotation(Vec3::sAxisY(), heading)
         * Quat::sRotation(Vec3::sAxisX(), -pitch)
         * Quat::sRotation(Vec3::sAxisZ(), heel);
}

struct EulerPose { float pitch, heading, heel; };

EulerPose extractPose(Quat rot) {
    const Vec3 fwd = rot * Vec3(0.0f, 0.0f, 1.0f);
    const float heading = std::atan2(fwd.GetX(), fwd.GetZ());
    const Quat invYaw = Quat::sRotation(Vec3::sAxisY(), -heading);
    const Vec3 fwdLocal = invYaw * fwd;
    const float pitch = std::atan2(fwdLocal.GetY(), fwdLocal.GetZ());
    const Quat invPitch = Quat::sRotation(Vec3::sAxisX(), pitch); // inverse of RotX(-pitch)
    const Vec3 rightLocal = invPitch * (invYaw * (rot * Vec3(1.0f, 0.0f, 0.0f)));
    const float heel = std::atan2(rightLocal.GetY(), rightLocal.GetX());
    return { pitch, heading, heel };
}

} // namespace

namespace sea {

struct DrivenBody {
    JPH::BodyID id;
    ShipDrive drive;
    bool hasDrive = false;
};

struct PhysicsWorld::Impl {
    struct Listener : JPH::ContactListener {
        std::vector<uint32_t>* contacts = nullptr;
        void note(const JPH::Body& a, const JPH::Body& b) {
            if (!contacts) return;
            auto add = [&](const JPH::Body& body) {
                if (body.GetUserData() == kShipUserData)
                    contacts->push_back(body.GetID().GetIndexAndSequenceNumber());
            };
            add(a);
            add(b);
        }
        JPH::ValidateResult OnContactValidate(const JPH::Body&, const JPH::Body&, JPH::RVec3Arg,
                                              const JPH::CollideShapeResult&) override {
            return JPH::ValidateResult::AcceptAllContactsForThisBodyPair;
        }
        void OnContactAdded(const JPH::Body& a, const JPH::Body& b, const JPH::ContactManifold&,
                            JPH::ContactSettings&) override { note(a, b); }
        void OnContactPersisted(const JPH::Body& a, const JPH::Body& b, const JPH::ContactManifold&,
                                JPH::ContactSettings&) override { note(a, b); }
        void OnContactRemoved(const JPH::SubShapeIDPair&) override {}
    };

    Listener listener;
    BPLayerInterfaceImpl broadPhase;
    ObjectVsBroadPhaseLayerFilterImpl objectVsBroadPhase;
    ObjectLayerPairFilterImpl objectVsObject;
    JPH::TempAllocatorImpl temp{16 * 1024 * 1024};
    JPH::JobSystemSingleThreaded jobs{JPH::cMaxPhysicsJobs};
    std::vector<DrivenBody> driven;
    std::vector<uint32_t> contacts;
    float accumulator = 0.0f;
    JPH::PhysicsSystem system;

    Impl() {
        ensureJolt();
        listener.contacts = &contacts;
        system.Init(1024, 0, 1024, 1024, broadPhase, objectVsBroadPhase, objectVsObject);
        system.SetContactListener(&listener);
    }

    ~Impl() { system.SetContactListener(nullptr); }

    DrivenBody* find(uint32_t raw) {
        for (DrivenBody& d : driven)
            if (d.id.GetIndexAndSequenceNumber() == raw) return &d;
        return nullptr;
    }

    void applyDrive(JPH::BodyInterface& bi, const JPH::BodyID& id, const ShipDrive& drive) {
        const JPH::RVec3 pos = bi.GetPosition(id);
        const JPH::Quat rot = bi.GetRotation(id);
        const float vx = std::sin(drive.heading) * drive.speed;
        const float vz = std::cos(drive.heading) * drive.speed;
        const float vy = clampf((drive.buoyancy.heaveY - pos.GetY()) * kHeaveGain,
                                -kMaxHeaveSpeed, kMaxHeaveSpeed);
        bi.SetLinearVelocity(id, JPH::Vec3(vx, vy, vz));

        JPH::Quat delta = shipOrientation(drive.heading, drive.buoyancy.pitch, drive.buoyancy.heel)
                        * rot.Conjugated();
        if (delta.GetW() < 0.0f)
            delta = JPH::Quat(-delta.GetX(), -delta.GetY(), -delta.GetZ(), -delta.GetW());
        JPH::Vec3 axis = JPH::Vec3::sZero();
        float angle = 0.0f;
        delta.GetAxisAngle(axis, angle);
        if (angle < 1.0e-4f) {
            bi.SetAngularVelocity(id, JPH::Vec3::sZero());
        } else {
            bi.SetAngularVelocity(id, axis * clampf(angle * kOriGain, 0.0f, kMaxAngSpeed));
        }
    }

    void substep(float dt) {
        JPH::BodyInterface& bi = system.GetBodyInterface();
        for (const DrivenBody& d : driven) {
            if (d.hasDrive) applyDrive(bi, d.id, d.drive);
        }
        system.Update(dt, 1, &temp, &jobs);
    }
};

PhysicsWorld::PhysicsWorld() {
    // Jolt's allocator hook has to be installed before Impl's members run.
    // TempAllocatorImpl allocates inside its own constructor.
    ensureJolt();
    impl_ = std::make_unique<Impl>();
}
PhysicsWorld::~PhysicsWorld() = default;

uint32_t PhysicsWorld::createShip(const HullCollider& hull, float x, float y, float z, float heading) {
    const float hx = std::max(0.1f, hull.beam * 0.5f);
    const float hy = std::max(0.1f, hull.height * 0.5f);
    const float hz = std::max(0.1f, hull.length * 0.5f);
    JPH::BodyCreationSettings settings(
        new JPH::BoxShape(JPH::Vec3(hx, hy, hz)),
        JPH::RVec3(x, y, z),
        shipOrientation(heading, 0.0f, 0.0f),
        JPH::EMotionType::Dynamic,
        Layers::MOVING);
    settings.mGravityFactor = 0.0f; // buoyancy target owns the vertical axis
    settings.mAllowSleeping = false;
    settings.mLinearDamping = 0.0f;
    settings.mAngularDamping = 0.0f;
    settings.mRestitution = 0.0f;
    settings.mUserData = kShipUserData;
    settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
    settings.mMassPropertiesOverride.mMass = std::max(1.0f, hull.mass);

    JPH::BodyInterface& bi = impl_->system.GetBodyInterface();
    const JPH::BodyID id = bi.CreateAndAddBody(settings, JPH::EActivation::Activate);
    if (id.IsInvalid()) return kInvalidPhysicsBody;
    DrivenBody driven;
    driven.id = id;
    impl_->driven.push_back(driven);
    return id.GetIndexAndSequenceNumber();
}

uint32_t PhysicsWorld::createIsland(const IslandCollider& island) {
    const float halfH = std::max(1.0f, island.height * 0.5f);
    const float radius = std::max(0.25f, island.radius);
    JPH::BodyCreationSettings settings(
        new JPH::CylinderShape(halfH, radius),
        JPH::RVec3(island.centerX, 0.0f, island.centerZ),
        JPH::Quat::sIdentity(),
        JPH::EMotionType::Static,
        Layers::NON_MOVING);
    settings.mRestitution = 0.0f;
    JPH::BodyInterface& bi = impl_->system.GetBodyInterface();
    const JPH::BodyID id = bi.CreateAndAddBody(settings, JPH::EActivation::DontActivate);
    if (id.IsInvalid()) return kInvalidPhysicsBody;
    return id.GetIndexAndSequenceNumber();
}

void PhysicsWorld::driveShip(uint32_t ship, const ShipDrive& drive) {
    if (DrivenBody* d = impl_->find(ship)) {
        d->drive = drive;
        d->hasDrive = true;
    }
}

void PhysicsWorld::teleportShip(uint32_t ship, float x, float y, float z,
                                float heading, float pitch, float heel) {
    DrivenBody* d = impl_->find(ship);
    if (!d) return;
    JPH::BodyInterface& bi = impl_->system.GetBodyInterface();
    bi.SetPositionAndRotation(d->id, JPH::RVec3(x, y, z), shipOrientation(heading, pitch, heel),
                              JPH::EActivation::Activate);
    bi.SetLinearVelocity(d->id, JPH::Vec3::sZero());
    bi.SetAngularVelocity(d->id, JPH::Vec3::sZero());
}

void PhysicsWorld::step(float dt) {
    if (!(dt > 0.0f)) return;
    impl_->accumulator += dt;
    if (impl_->accumulator < kFixedDt) return;
    impl_->contacts.clear();
    int n = 0;
    while (impl_->accumulator >= kFixedDt && n < kMaxSubsteps) {
        impl_->substep(kFixedDt);
        impl_->accumulator -= kFixedDt;
        ++n;
    }
    if (n == kMaxSubsteps) impl_->accumulator = 0.0f;
}

BodyPose PhysicsWorld::bodyPose(uint32_t body) const {
    BodyPose out;
    const JPH::BodyID id(body);
    if (id.IsInvalid()) return out;
    JPH::BodyLockRead lock(impl_->system.GetBodyLockInterfaceNoLock(), id);
    if (!lock.Succeeded()) return out;
    const JPH::Body& b = lock.GetBody();
    const JPH::RVec3 p = b.GetPosition();
    const EulerPose e = extractPose(b.GetRotation());
    const JPH::Vec3 v = b.GetLinearVelocity();
    out.x = p.GetX();
    out.y = p.GetY();
    out.z = p.GetZ();
    out.pitch = e.pitch;
    out.heading = e.heading;
    out.heel = e.heel;
    out.vx = v.GetX();
    out.vy = v.GetY();
    out.vz = v.GetZ();
    out.speed = std::sqrt(out.vx * out.vx + out.vz * out.vz);
    return out;
}

BodyExtents PhysicsWorld::bodyExtents(uint32_t body) const {
    BodyExtents out;
    const JPH::BodyID id(body);
    if (id.IsInvalid()) return out;
    JPH::BodyLockRead lock(impl_->system.GetBodyLockInterfaceNoLock(), id);
    if (!lock.Succeeded()) return out;
    const JPH::Body& b = lock.GetBody();
    const JPH::Vec3 half = b.GetShape()->GetLocalBounds().GetExtent();
    out.halfBeam = half.GetX();
    out.halfHeight = half.GetY();
    out.halfLength = half.GetZ();
    if (const JPH::MotionProperties* motion = b.GetMotionProperties()) {
        const float inv = motion->GetInverseMass();
        if (inv > 0.0f) out.mass = 1.0f / inv;
    }
    return out;
}

bool PhysicsWorld::shipInContact(uint32_t ship) const {
    for (uint32_t id : impl_->contacts)
        if (id == ship) return true;
    return false;
}

int PhysicsWorld::bodyCount() const {
    return static_cast<int>(impl_->system.GetNumBodies());
}

HullCollider hullColliderFromShip(const Ship& ship) {
    const Stats stats = getShipStats(ship);
    HullCollider hull;
    hull.length = static_cast<float>(std::max(0.2, ship.bounds.length));
    hull.beam = static_cast<float>(std::max(0.2, ship.bounds.width));
    hull.height = static_cast<float>(std::max(0.2, ship.bounds.depth));
    hull.mass = static_cast<float>(std::max(1.0, stats.mass));
    return hull;
}

const char* physicsBackendName() {
    static char buf[32];
    static bool init = false;
    if (!init) {
        std::snprintf(buf, sizeof(buf), "Jolt %d.%d.%d", JPH_VERSION_MAJOR, JPH_VERSION_MINOR, JPH_VERSION_PATCH);
        init = true;
    }
    return buf;
}

} // namespace sea
