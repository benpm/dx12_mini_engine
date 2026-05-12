#pragma once

#include <cstdint>

// IPhysicsBackend — pure virtual interface implemented by the various physics
// libraries the engine can drive. PhysicsWorld holds a unique_ptr<IPhysicsBackend>
// and forwards calls; the factory in physics.cpp picks the active backend based
// on the ENGINE_PHYSICS_BACKEND_* compile-time flag set by CMake.
//
// Today the engine ships with a Jolt 5.2 backend (ENGINE_PHYSICS_BACKEND_JOLT,
// default). A PhysX backend is reserved at the interface level
// (ENGINE_PHYSICS_BACKEND_PHYSX) so callers can swap implementations without
// touching engine code. The PhysX backend is a build-time choice the user
// supplies their own PhysX SDK + CMake glue for; see CMakeLists.txt.

class IPhysicsBackend
{
   public:
    using BodyId = uint32_t;

    virtual ~IPhysicsBackend() = default;

    virtual bool isReady() const = 0;
    virtual void step(float dt) = 0;
    virtual uint32_t bodyCount() const = 0;

    virtual BodyId createBoxBody(
        float px, float py, float pz, float halfX, float halfY, float halfZ, bool dynamic,
        float mass
    ) = 0;
    virtual BodyId createSphereBody(
        float px, float py, float pz, float radius, bool dynamic, float mass
    ) = 0;

    // Build a body around a convex hull computed from `count` points stored
    // at `stride` bytes apart (so callers can pass `VertexPBR` directly or a
    // tight `vec3` array). `hullTolerance` is a backend hint for how aggressively
    // to simplify; Jolt interprets it directly. Returns 0 if the hull is
    // degenerate (fewer than 4 unique points, all colinear, etc.).
    virtual BodyId createConvexHullBody(
        const float* points, uint32_t count, uint32_t stride, float px, float py, float pz,
        bool dynamic, float mass, float hullTolerance
    ) = 0;

    virtual void destroyBody(BodyId id) = 0;

    virtual void getBodyPosition(BodyId id, float& px, float& py, float& pz) const = 0;
    virtual void getBodyRotation(
        BodyId id, float& qx, float& qy, float& qz, float& qw
    ) const = 0;

    virtual void applyForce(BodyId id, float fx, float fy, float fz) = 0;
    virtual void applyImpulse(BodyId id, float ix, float iy, float iz) = 0;
    virtual void setBodyPosition(BodyId id, float px, float py, float pz, bool activate) = 0;

    virtual bool raycast(
        float ox, float oy, float oz, float dx, float dy, float dz, float maxDistance,
        float& hitX, float& hitY, float& hitZ, float& hitDistance
    ) const = 0;

    // Human-readable name (e.g. "Jolt 5.2", "PhysX 5", "null") for UI / log output.
    virtual const char* name() const = 0;
};

// Factory entry points — implementations are linked in by CMake based on the
// active backend macro. Returning a non-null pointer means the backend
// initialised successfully; nullptr means the caller should treat physics as
// disabled.
IPhysicsBackend* createJoltBackend();
IPhysicsBackend* createPhysXBackend();   // returns nullptr unless ENGINE_PHYSICS_BACKEND_PHYSX
IPhysicsBackend* createNullBackend();    // always succeeds, all calls are no-ops
