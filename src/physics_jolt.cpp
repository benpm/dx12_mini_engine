// JoltBackend — IPhysicsBackend implementation backed by Jolt Physics 5.2.
// Lives in a plain (non-module) TU so it can include Jolt headers without
// polluting the physics module's interface. The factory createJoltBackend()
// is the only symbol non-Jolt code needs.
//
// This file holds the implementation that used to live inline in physics.cpp;
// extracting it lets PhysicsWorld pick a backend at construction (Jolt today,
// PhysX or a custom solver tomorrow) without changing call sites.

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
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <thread>

#include "physics_backend.h"

namespace
{
    // Object layers (gameplay categories).
    namespace Layers
    {
        static constexpr JPH::ObjectLayer NON_MOVING = 0;
        static constexpr JPH::ObjectLayer MOVING = 1;
        static constexpr JPH::ObjectLayer NUM_LAYERS = 2;
    }  // namespace Layers

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
        return true;
    }
#endif

    static bool g_joltInitialised = false;

    inline IPhysicsBackend::BodyId toEngineId(JPH::BodyID b)
    {
        return b.GetIndexAndSequenceNumber();
    }
    inline JPH::BodyID toJoltId(IPhysicsBackend::BodyId id) { return JPH::BodyID(id); }

    JPH::BodyID createBodyImpl(
        JPH::PhysicsSystem& sys, JPH::ShapeRefC shape, JPH::Vec3 pos, bool dynamic, float mass
    )
    {
        JPH::BodyCreationSettings bcs(
            shape, pos, JPH::Quat::sIdentity(),
            dynamic ? JPH::EMotionType::Dynamic : JPH::EMotionType::Static, dynamic ? 1 : 0
        );
        if (dynamic) {
            bcs.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
            bcs.mMassPropertiesOverride.mMass = std::max(0.001f, mass);
        }
        auto& bi = sys.GetBodyInterface();
        return bi.CreateAndAddBody(bcs, JPH::EActivation::Activate);
    }
}  // namespace

class JoltBackend final : public IPhysicsBackend
{
   public:
    JoltBackend()
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
        // 64 MB scratch — enough for 1000+ dynamic bodies through the broadphase
        // and constraint solver. Default 10 MB OOMs at ~512 bodies under our
        // physics_stress workload; this gives plenty of headroom.
        tempAlloc = std::make_unique<JPH::TempAllocatorImpl>(64 * 1024 * 1024);
        jobSystem = std::make_unique<JPH::JobSystemThreadPool>(
            JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers,
            std::max(1u, std::thread::hardware_concurrency() - 1)
        );
        system = std::make_unique<JPH::PhysicsSystem>();
        system->Init(maxBodies, numBodyMutexes, maxBodyPairs, maxContactConstraints, bpLayers,
                     objVsBP, objVsObj);
        ready = true;
        spdlog::info("JoltBackend: initialised (maxBodies={})", maxBodies);
    }

    ~JoltBackend() override = default;

    bool isReady() const override { return ready; }
    const char* name() const override { return "Jolt 5.2"; }

    void step(float dt) override
    {
        if (!ready) return;
        system->Update(dt, /*collisionSteps=*/1, tempAlloc.get(), jobSystem.get());
    }

    uint32_t bodyCount() const override { return ready ? system->GetNumBodies() : 0; }

    BodyId createBoxBody(float px, float py, float pz, float hx, float hy, float hz, bool dynamic,
                         float mass) override
    {
        if (!ready) return 0;
        JPH::ShapeRefC shape = new JPH::BoxShape(JPH::Vec3(hx, hy, hz));
        return toEngineId(createBodyImpl(*system, shape, JPH::Vec3(px, py, pz), dynamic, mass));
    }

    BodyId createSphereBody(float px, float py, float pz, float radius, bool dynamic, float mass)
        override
    {
        if (!ready) return 0;
        JPH::ShapeRefC shape = new JPH::SphereShape(std::max(0.001f, radius));
        return toEngineId(createBodyImpl(*system, shape, JPH::Vec3(px, py, pz), dynamic, mass));
    }

    BodyId createCapsuleBody(
        float px, float py, float pz, float halfHeight, float radius, bool dynamic, float mass
    ) override
    {
        if (!ready) return 0;
        JPH::ShapeRefC shape =
            new JPH::CapsuleShape(std::max(0.001f, halfHeight), std::max(0.001f, radius));
        return toEngineId(createBodyImpl(*system, shape, JPH::Vec3(px, py, pz), dynamic, mass));
    }

    BodyId createConvexHullBody(
        const float* points, uint32_t count, uint32_t stride, float px, float py, float pz,
        bool dynamic, float mass, float hullTolerance
    ) override
    {
        if (!ready || !points || count < 4) {
            return 0;  // need at least 4 non-coplanar points for a valid hull
        }
        // Repack into JPH::Vec3 with hard cap on cMaxPointsInHull (256).
        uint32_t cap = std::min<uint32_t>(count, JPH::ConvexHullShape::cMaxPointsInHull);
        JPH::Array<JPH::Vec3> jolt;
        jolt.reserve(cap);
        const uint32_t step = stride == 0 ? sizeof(float) * 3 : stride;
        const uint8_t* base = reinterpret_cast<const uint8_t*>(points);
        for (uint32_t i = 0; i < cap; ++i) {
            const float* p = reinterpret_cast<const float*>(base + i * step);
            jolt.emplace_back(p[0], p[1], p[2]);
        }
        JPH::ConvexHullShapeSettings hullSettings(jolt);
        hullSettings.mHullTolerance = hullTolerance > 0.0f ? hullTolerance : 1.0e-3f;
        JPH::ShapeSettings::ShapeResult result = hullSettings.Create();
        if (result.HasError()) {
            spdlog::warn(
                "JoltBackend: ConvexHullShape::Create failed ({})", result.GetError().c_str()
            );
            return 0;
        }
        JPH::ShapeRefC shape = result.Get();
        return toEngineId(createBodyImpl(*system, shape, JPH::Vec3(px, py, pz), dynamic, mass));
    }

    void destroyBody(BodyId id) override
    {
        if (!ready || id == 0) return;
        auto& bi = system->GetBodyInterface();
        JPH::BodyID jid = toJoltId(id);
        bi.RemoveBody(jid);
        bi.DestroyBody(jid);
    }

    void getBodyPosition(BodyId id, float& px, float& py, float& pz) const override
    {
        px = py = pz = 0.0f;
        if (!ready || id == 0) return;
        JPH::RVec3 p = system->GetBodyInterface().GetPosition(toJoltId(id));
        px = (float)p.GetX();
        py = (float)p.GetY();
        pz = (float)p.GetZ();
    }

    void getBodyRotation(BodyId id, float& qx, float& qy, float& qz, float& qw) const override
    {
        qx = qy = qz = 0.0f;
        qw = 1.0f;
        if (!ready || id == 0) return;
        JPH::Quat q = system->GetBodyInterface().GetRotation(toJoltId(id));
        qx = q.GetX();
        qy = q.GetY();
        qz = q.GetZ();
        qw = q.GetW();
    }

    void applyForce(BodyId id, float fx, float fy, float fz) override
    {
        if (!ready || id == 0) return;
        system->GetBodyInterface().AddForce(toJoltId(id), JPH::Vec3(fx, fy, fz));
    }

    void applyImpulse(BodyId id, float ix, float iy, float iz) override
    {
        if (!ready || id == 0) return;
        system->GetBodyInterface().AddImpulse(toJoltId(id), JPH::Vec3(ix, iy, iz));
    }

    void setBodyPosition(BodyId id, float px, float py, float pz, bool activate) override
    {
        if (!ready || id == 0) return;
        system->GetBodyInterface().SetPosition(
            toJoltId(id), JPH::Vec3(px, py, pz),
            activate ? JPH::EActivation::Activate : JPH::EActivation::DontActivate
        );
    }

    void getLinearVelocity(BodyId id, float& vx, float& vy, float& vz) const override
    {
        vx = vy = vz = 0.0f;
        if (!ready || id == 0) return;
        JPH::Vec3 v = system->GetBodyInterface().GetLinearVelocity(toJoltId(id));
        vx = v.GetX();
        vy = v.GetY();
        vz = v.GetZ();
    }

    void setLinearVelocity(BodyId id, float vx, float vy, float vz) override
    {
        if (!ready || id == 0) return;
        system->GetBodyInterface().SetLinearVelocity(toJoltId(id), JPH::Vec3(vx, vy, vz));
    }

    void getAngularVelocity(BodyId id, float& wx, float& wy, float& wz) const override
    {
        wx = wy = wz = 0.0f;
        if (!ready || id == 0) return;
        JPH::Vec3 w = system->GetBodyInterface().GetAngularVelocity(toJoltId(id));
        wx = w.GetX();
        wy = w.GetY();
        wz = w.GetZ();
    }

    void setAngularVelocity(BodyId id, float wx, float wy, float wz) override
    {
        if (!ready || id == 0) return;
        system->GetBodyInterface().SetAngularVelocity(toJoltId(id), JPH::Vec3(wx, wy, wz));
    }

    bool raycast(float ox, float oy, float oz, float dx, float dy, float dz, float maxDistance,
                 float& hitX, float& hitY, float& hitZ, float& hitDistance) const override
    {
        hitX = hitY = hitZ = 0.0f;
        hitDistance = 0.0f;
        if (!ready) return false;
        JPH::Vec3 origin(ox, oy, oz);
        JPH::Vec3 dir = JPH::Vec3(dx, dy, dz) * maxDistance;
        JPH::RRayCast ray(origin, dir);
        JPH::RayCastResult hit;
        if (!system->GetNarrowPhaseQuery().CastRay(ray, hit)) {
            return false;
        }
        JPH::Vec3 hitPos = origin + dir * hit.mFraction;
        hitX = (float)hitPos.GetX();
        hitY = (float)hitPos.GetY();
        hitZ = (float)hitPos.GetZ();
        hitDistance = maxDistance * hit.mFraction;
        return true;
    }

   private:
    // Sized to comfortably host the 1000-body physics_stress scene with
    // overlap-induced pair counts. Body pairs scale roughly as N * neighbours
    // so 1000 tightly-packed bodies need O(20-30k) pair slots; constraints
    // need ~O(bodies) when everything is in contact.
    static constexpr JPH::uint maxBodies = 16384;
    static constexpr JPH::uint numBodyMutexes = 0;
    static constexpr JPH::uint maxBodyPairs = 65536;
    static constexpr JPH::uint maxContactConstraints = 16384;

    std::unique_ptr<JPH::TempAllocator> tempAlloc;
    std::unique_ptr<JPH::JobSystem> jobSystem;
    BPLayerInterfaceImpl bpLayers;
    ObjectVsBroadPhaseLayerFilterImpl objVsBP;
    ObjectLayerPairFilterImpl objVsObj;
    std::unique_ptr<JPH::PhysicsSystem> system;
    bool ready = false;
};

IPhysicsBackend* createJoltBackend()
{
    return new JoltBackend();
}
