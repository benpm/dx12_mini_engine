// PhysicsWorld is now a thin facade over IPhysicsBackend. The actual
// implementation lives in src/physics_jolt.cpp (default) or src/physics_physx.cpp
// (build-time opt-in). Every call forwards to the active backend so callers
// remain backend-agnostic.

module;

#if defined(__clang__)
    #define FMT_CONSTEVAL
#endif

#include <spdlog/spdlog.h>

#include <cstdint>
#include <memory>

#include "physics_backend.h"

module physics;

namespace
{
    // Choose the active backend. Order: PhysX (if available) → Jolt → null.
    // PhysX requires user-supplied build glue (-DENGINE_PHYSICS_BACKEND=PhysX);
    // see src/physics_physx.cpp.
    IPhysicsBackend* makeActiveBackend()
    {
#if defined(ENGINE_PHYSICS_BACKEND_PHYSX)
        if (auto* px = createPhysXBackend()) {
            spdlog::info("PhysicsWorld: using PhysX backend");
            return px;
        }
        spdlog::warn("ENGINE_PHYSICS_BACKEND_PHYSX requested but createPhysXBackend returned null");
#endif
#if defined(ENGINE_PHYSICS_BACKEND_JOLT) || \
    (!defined(ENGINE_PHYSICS_BACKEND_PHYSX) && !defined(ENGINE_PHYSICS_BACKEND_NONE))
        if (auto* j = createJoltBackend()) {
            spdlog::info("PhysicsWorld: using Jolt backend");
            return j;
        }
#endif
        spdlog::warn("PhysicsWorld: falling back to null physics backend");
        return createNullBackend();
    }
}  // namespace

PhysicsWorld::PhysicsWorld()
{
    auto* backend = makeActiveBackend();
    state = backend;
    ready = backend && backend->isReady();
}

PhysicsWorld::~PhysicsWorld()
{
    delete static_cast<IPhysicsBackend*>(state);
    state = nullptr;
}

void PhysicsWorld::step(float dt)
{
    static_cast<IPhysicsBackend*>(state)->step(dt);
}

uint32_t PhysicsWorld::bodyCount() const
{
    return static_cast<IPhysicsBackend*>(state)->bodyCount();
}

PhysicsWorld::BodyId PhysicsWorld::createBoxBody(
    float px, float py, float pz, float hx, float hy, float hz, bool dynamic, float mass
)
{
    return static_cast<IPhysicsBackend*>(state)->createBoxBody(
        px, py, pz, hx, hy, hz, dynamic, mass
    );
}

PhysicsWorld::BodyId PhysicsWorld::createSphereBody(
    float px, float py, float pz, float radius, bool dynamic, float mass
)
{
    return static_cast<IPhysicsBackend*>(state)->createSphereBody(
        px, py, pz, radius, dynamic, mass
    );
}

void PhysicsWorld::destroyBody(BodyId id)
{
    static_cast<IPhysicsBackend*>(state)->destroyBody(id);
}

void PhysicsWorld::getBodyPosition(BodyId id, float& px, float& py, float& pz) const
{
    static_cast<IPhysicsBackend*>(state)->getBodyPosition(id, px, py, pz);
}

void PhysicsWorld::getBodyRotation(BodyId id, float& qx, float& qy, float& qz, float& qw) const
{
    static_cast<IPhysicsBackend*>(state)->getBodyRotation(id, qx, qy, qz, qw);
}

void PhysicsWorld::applyForce(BodyId id, float fx, float fy, float fz)
{
    static_cast<IPhysicsBackend*>(state)->applyForce(id, fx, fy, fz);
}

void PhysicsWorld::applyImpulse(BodyId id, float ix, float iy, float iz)
{
    static_cast<IPhysicsBackend*>(state)->applyImpulse(id, ix, iy, iz);
}

void PhysicsWorld::setBodyPosition(BodyId id, float px, float py, float pz, bool activate)
{
    static_cast<IPhysicsBackend*>(state)->setBodyPosition(id, px, py, pz, activate);
}

bool PhysicsWorld::raycast(
    float ox, float oy, float oz, float dx, float dy, float dz, float maxDistance, float& hitX,
    float& hitY, float& hitZ, float& hitDistance
) const
{
    return static_cast<IPhysicsBackend*>(state)->raycast(
        ox, oy, oz, dx, dy, dz, maxDistance, hitX, hitY, hitZ, hitDistance
    );
}

// ---------------------------------------------------------------------------
// C API consumed by lua_scripting_impl.cpp
// ---------------------------------------------------------------------------

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

extern "C" void engine_physics_get_body_rotation(
    void* p, unsigned int id, float* outX, float* outY, float* outZ, float* outW
)
{
    float x = 0, y = 0, z = 0, w_ = 1;
    if (auto* world = static_cast<PhysicsWorld*>(p)) {
        world->getBodyRotation(id, x, y, z, w_);
    }
    if (outX) *outX = x;
    if (outY) *outY = y;
    if (outZ) *outZ = z;
    if (outW) *outW = w_;
}

extern "C" void engine_physics_apply_force(
    void* p, unsigned int id, float fx, float fy, float fz
)
{
    if (auto* w = static_cast<PhysicsWorld*>(p)) {
        w->applyForce(id, fx, fy, fz);
    }
}

extern "C" void engine_physics_apply_impulse(
    void* p, unsigned int id, float ix, float iy, float iz
)
{
    if (auto* w = static_cast<PhysicsWorld*>(p)) {
        w->applyImpulse(id, ix, iy, iz);
    }
}

extern "C" void engine_physics_set_body_position(
    void* p, unsigned int id, float px, float py, float pz
)
{
    if (auto* w = static_cast<PhysicsWorld*>(p)) {
        w->setBodyPosition(id, px, py, pz);
    }
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
