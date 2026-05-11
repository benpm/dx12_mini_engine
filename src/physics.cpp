module;

#if defined(__clang__)
    #define FMT_CONSTEVAL
#endif

#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>
#include <Jolt/Physics/Collision/ContactListener.h>
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
