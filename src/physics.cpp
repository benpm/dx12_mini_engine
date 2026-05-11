module;

#if defined(__clang__)
    #define FMT_CONSTEVAL
#endif

#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <spdlog/spdlog.h>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <thread>

module physics;

#include "audio_capi.h"

extern "C" unsigned int engine_physics_create_box(
    void* p, float px, float py, float pz, float hx, float hy, float hz, int dynamic, float mass
)
{
    if (!p) return 0;
    return static_cast<PhysicsWorld*>(p)->createBoxBody(
        px, py, pz, hx, hy, hz, dynamic != 0, mass
    );
}

extern "C" unsigned int engine_physics_create_sphere(
    void* p, float px, float py, float pz, float radius, int dynamic, float mass
)
{
    if (!p) return 0;
    return static_cast<PhysicsWorld*>(p)->createSphereBody(
        px, py, pz, radius, dynamic != 0, mass
    );
}

extern "C" void engine_physics_destroy_body(void* p, unsigned int id)
{
    if (auto* w = static_cast<PhysicsWorld*>(p)) {
        w->destroyBody(id);
    }
}

extern "C" void engine_physics_get_body_position(
    void* p, unsigned int id, float* outX, float* outY, float* outZ
)
{
    float x = 0, y = 0, z = 0;
    if (auto* w = static_cast<PhysicsWorld*>(p)) {
        w->getBodyPosition(id, x, y, z);
    }
    if (outX) *outX = x;
    if (outY) *outY = y;
    if (outZ) *outZ = z;
}

extern "C" int engine_physics_raycast(
    void* p, float ox, float oy, float oz, float dx, float dy, float dz, float maxDistance,
    float* hitX, float* hitY, float* hitZ, float* hitDistance
)
{
    if (!p) return 0;
    float hx = 0, hy = 0, hz = 0, hd = 0;
    bool ok = static_cast<PhysicsWorld*>(p)->raycast(
        ox, oy, oz, dx, dy, dz, maxDistance, hx, hy, hz, hd
    );
    if (hitX) *hitX = hx;
    if (hitY) *hitY = hy;
    if (hitZ) *hitZ = hz;
    if (hitDistance) *hitDistance = hd;
    return ok ? 1 : 0;
}

namespace
{
    // Object layers (gameplay categories).
    namespace Layers
    {
        static constexpr JPH::ObjectLayer NON_MOVING = 0;
        static constexpr JPH::ObjectLayer MOVING = 1;
        static constexpr JPH::ObjectLayer NUM_LAYERS = 2;
    }  // namespace Layers

    // Broadphase layers (coarser bucket for spatial accel).
    namespace BPLayers
    {
        static constexpr JPH::BroadPhaseLayer NON_MOVING(0);
        static constexpr JPH::BroadPhaseLayer MOVING(1);
        static constexpr JPH::uint NUM_LAYERS = 2;
    }  // namespace BPLayers

    class BPLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface
    {
       public:
        BPLayerInterfaceImpl()
        {
            mObjectToBroadPhase[Layers::NON_MOVING] = BPLayers::NON_MOVING;
            mObjectToBroadPhase[Layers::MOVING] = BPLayers::MOVING;
        }
        JPH::uint GetNumBroadPhaseLayers() const override { return BPLayers::NUM_LAYERS; }
        JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const override
        {
            return mObjectToBroadPhase[inLayer];
        }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
        const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer inLayer) const override
        {
            return inLayer == BPLayers::NON_MOVING ? "NON_MOVING" : "MOVING";
        }
#endif
       private:
        JPH::BroadPhaseLayer mObjectToBroadPhase[Layers::NUM_LAYERS];
    };

    class ObjectVsBroadPhaseLayerFilterImpl final : public JPH::ObjectVsBroadPhaseLayerFilter
    {
       public:
        bool ShouldCollide(JPH::ObjectLayer obj, JPH::BroadPhaseLayer bp) const override
        {
            switch (obj) {
                case Layers::NON_MOVING:
                    return bp == BPLayers::MOVING;
                case Layers::MOVING:
                    return true;
                default:
                    return false;
            }
        }
    };

    class ObjectLayerPairFilterImpl final : public JPH::ObjectLayerPairFilter
    {
       public:
        bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override
        {
            switch (a) {
                case Layers::NON_MOVING:
                    return b == Layers::MOVING;
                case Layers::MOVING:
                    return true;
                default:
                    return false;
            }
        }
    };

    void traceCallback(const char* fmt, ...)
    {
        char buf[1024];
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        spdlog::info("[jolt] {}", buf);
    }

#ifdef JPH_ENABLE_ASSERTS
    bool assertFailedCallback(const char* expr, const char* msg, const char* file, JPH::uint line)
    {
        spdlog::error(
            "[jolt assert] {}:{}: ({}) {}", file ? file : "?", line, expr ? expr : "",
            msg ? msg : ""
        );
        return true;  // breakpoint
    }
#endif

    struct PhysicsWorldImpl
    {
        std::unique_ptr<JPH::TempAllocator> tempAlloc;
        std::unique_ptr<JPH::JobSystem> jobSystem;
        BPLayerInterfaceImpl bpLayers;
        ObjectVsBroadPhaseLayerFilterImpl objVsBP;
        ObjectLayerPairFilterImpl objVsObj;
        std::unique_ptr<JPH::PhysicsSystem> system;
        // Initial Jolt configuration. Tweak as physics body counts grow.
        static constexpr JPH::uint maxBodies = 4096;
        static constexpr JPH::uint numBodyMutexes = 0;
        static constexpr JPH::uint maxBodyPairs = 4096;
        static constexpr JPH::uint maxContactConstraints = 1024;
    };

    static bool g_joltInitialised = false;
}  // namespace

PhysicsWorld::PhysicsWorld()
{
    if (!g_joltInitialised) {
        JPH::RegisterDefaultAllocator();
        JPH::Trace = &traceCallback;
#ifdef JPH_ENABLE_ASSERTS
        JPH::AssertFailed = &assertFailedCallback;
#endif
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();
        g_joltInitialised = true;
    }

    auto* impl = new PhysicsWorldImpl();
    impl->tempAlloc = std::make_unique<JPH::TempAllocatorImpl>(10 * 1024 * 1024);
    impl->jobSystem = std::make_unique<JPH::JobSystemThreadPool>(
        JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, std::max(1u, std::thread::hardware_concurrency() - 1)
    );
    impl->system = std::make_unique<JPH::PhysicsSystem>();
    impl->system->Init(
        PhysicsWorldImpl::maxBodies, PhysicsWorldImpl::numBodyMutexes,
        PhysicsWorldImpl::maxBodyPairs, PhysicsWorldImpl::maxContactConstraints, impl->bpLayers,
        impl->objVsBP, impl->objVsObj
    );

    state = impl;
    ready = true;
    spdlog::info("PhysicsWorld: Jolt initialised (maxBodies={})", PhysicsWorldImpl::maxBodies);
}

PhysicsWorld::~PhysicsWorld()
{
    auto* impl = static_cast<PhysicsWorldImpl*>(state);
    if (impl) {
        delete impl;
        state = nullptr;
    }
    // Note: JPH::UnregisterTypes + Factory teardown is intentionally not done here
    // because future PhysicsWorld instances may be created (e.g. scene reload).
    // Static teardown of Jolt is handled at process exit via the factory leak.
}

void PhysicsWorld::step(float dt)
{
    if (!ready) {
        return;
    }
    auto* impl = static_cast<PhysicsWorldImpl*>(state);
    // Use a fixed substep count for now. Future: derive from frame time / config.
    constexpr int collisionSteps = 1;
    impl->system->Update(dt, collisionSteps, impl->tempAlloc.get(), impl->jobSystem.get());
}

uint32_t PhysicsWorld::bodyCount() const
{
    if (!ready) {
        return 0;
    }
    auto* impl = static_cast<PhysicsWorldImpl*>(state);
    return impl->system->GetNumBodies();
}

namespace
{
    // Helper: pack a Jolt BodyID into a PhysicsWorld::BodyId. Jolt BodyID is a
    // single uint32 already, so the conversion is trivial — we keep the
    // engine-side handle opaque so callers don't depend on Jolt directly.
    inline PhysicsWorld::BodyId toEngineId(JPH::BodyID b) { return b.GetIndexAndSequenceNumber(); }
    inline JPH::BodyID toJoltId(PhysicsWorld::BodyId id) { return JPH::BodyID(id); }

    JPH::BodyID createBodyImpl(
        JPH::PhysicsSystem& sys, JPH::ShapeRefC shape, JPH::Vec3 pos, bool dynamic, float mass
    )
    {
        JPH::BodyCreationSettings bcs(
            shape, pos, JPH::Quat::sIdentity(),
            dynamic ? JPH::EMotionType::Dynamic : JPH::EMotionType::Static,
            dynamic ? 1 : 0  // ObjectLayer: MOVING vs NON_MOVING
        );
        if (dynamic) {
            bcs.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
            bcs.mMassPropertiesOverride.mMass = std::max(0.001f, mass);
        }
        auto& bi = sys.GetBodyInterface();
        JPH::BodyID id = bi.CreateAndAddBody(bcs, JPH::EActivation::Activate);
        return id;
    }
}  // namespace

PhysicsWorld::BodyId PhysicsWorld::createBoxBody(
    float px, float py, float pz, float halfX, float halfY, float halfZ, bool dynamic, float mass
)
{
    if (!ready) {
        return 0;
    }
    auto* impl = static_cast<PhysicsWorldImpl*>(state);
    JPH::ShapeRefC shape = new JPH::BoxShape(JPH::Vec3(halfX, halfY, halfZ));
    JPH::BodyID id = createBodyImpl(*impl->system, shape, JPH::Vec3(px, py, pz), dynamic, mass);
    return toEngineId(id);
}

PhysicsWorld::BodyId PhysicsWorld::createSphereBody(
    float px, float py, float pz, float radius, bool dynamic, float mass
)
{
    if (!ready) {
        return 0;
    }
    auto* impl = static_cast<PhysicsWorldImpl*>(state);
    JPH::ShapeRefC shape = new JPH::SphereShape(std::max(0.001f, radius));
    JPH::BodyID id = createBodyImpl(*impl->system, shape, JPH::Vec3(px, py, pz), dynamic, mass);
    return toEngineId(id);
}

void PhysicsWorld::destroyBody(BodyId id)
{
    if (!ready || id == 0) {
        return;
    }
    auto* impl = static_cast<PhysicsWorldImpl*>(state);
    auto& bi = impl->system->GetBodyInterface();
    JPH::BodyID jid = toJoltId(id);
    bi.RemoveBody(jid);
    bi.DestroyBody(jid);
}

void PhysicsWorld::getBodyPosition(BodyId id, float& px, float& py, float& pz) const
{
    px = py = pz = 0.0f;
    if (!ready || id == 0) {
        return;
    }
    auto* impl = static_cast<PhysicsWorldImpl*>(state);
    auto& bi = impl->system->GetBodyInterface();
    JPH::RVec3 p = bi.GetPosition(toJoltId(id));
    px = static_cast<float>(p.GetX());
    py = static_cast<float>(p.GetY());
    pz = static_cast<float>(p.GetZ());
}

bool PhysicsWorld::raycast(
    float ox, float oy, float oz, float dx, float dy, float dz, float maxDistance, float& hitX,
    float& hitY, float& hitZ, float& hitDistance
) const
{
    hitX = hitY = hitZ = 0.0f;
    hitDistance = 0.0f;
    if (!ready) {
        return false;
    }
    auto* impl = static_cast<PhysicsWorldImpl*>(state);
    JPH::Vec3 origin(ox, oy, oz);
    JPH::Vec3 dir = JPH::Vec3(dx, dy, dz) * maxDistance;
    JPH::RRayCast ray(origin, dir);
    JPH::RayCastResult hit;
    const auto& bpQuery = impl->system->GetBroadPhaseQuery();
    (void)bpQuery;
    // Use the narrow-phase query so we get the actual hit point, not just AABB.
    if (!impl->system->GetNarrowPhaseQuery().CastRay(ray, hit)) {
        return false;
    }
    JPH::Vec3 hitPos = origin + dir * hit.mFraction;
    hitX = static_cast<float>(hitPos.GetX());
    hitY = static_cast<float>(hitPos.GetY());
    hitZ = static_cast<float>(hitPos.GetZ());
    hitDistance = maxDistance * hit.mFraction;
    return true;
}
