module;

#include <cstdint>

export module physics;

// PhysicsWorld — thin wrapper around Jolt's PhysicsSystem. Owned by Application.
// First-pass integration: world initialises, registers Jolt types, allocates a
// temp allocator + worker job system, and steps each frame. RigidBody ECS
// components, raycasts, character controllers, and Lua bindings are follow-up
// work — the goal here is to validate the build/init path and have the per-frame
// step in place so future component-driven bodies have a host to attach to.
//
// As with audio.ixx, all Jolt types are kept inside the implementation TU so
// non-physics consumers don't pay the include cost.
export class PhysicsWorld
{
   public:
    PhysicsWorld();
    ~PhysicsWorld();

    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;

    bool isReady() const { return ready; }

    // Advance the simulation by dt. Calls collide/integrate steps internally.
    // Safe to call from Application::update().
    void step(float dt);

    // Number of active bodies (debug/UI).
    uint32_t bodyCount() const;

    // Opaque handle returned by createBox/Sphere. 0 = invalid.
    using BodyId = uint32_t;

    // Body creation. Static bodies don't move; dynamic bodies fall under gravity
    // and respond to forces. Friction defaults to 0.2, restitution to 0.0.
    BodyId createBoxBody(float px, float py, float pz, float halfX, float halfY, float halfZ,
                         bool dynamic, float mass = 1.0f);
    BodyId createSphereBody(float px, float py, float pz, float radius, bool dynamic,
                            float mass = 1.0f);

    // Capsule body (Y-up). Half-height is the cylinder segment between the two
    // hemispherical caps, NOT including the caps — total height = halfHeight*2
    // + radius*2. Pass values for a typical 1.8m-tall human as (0.6, 0.3).
    BodyId createCapsuleBody(float px, float py, float pz, float halfHeight, float radius,
                             bool dynamic, float mass = 1.0f);

    // Build a body around a convex hull from `count` 3-float positions stored
    // `stride` bytes apart. `hullTolerance` controls backend-side simplification
    // (Jolt: passed to ConvexHullShapeSettings). The caller should already have
    // simplified the input to ~32 points or fewer for a stress-friendly hull;
    // the backend enforces a hard cap (Jolt: 256 points).
    BodyId createConvexHullBody(const float* points, uint32_t count, uint32_t stride, float px,
                                float py, float pz, bool dynamic, float mass = 1.0f,
                                float hullTolerance = 0.05f);

    // Destroys + unregisters the body. Safe to call on an invalid id.
    void destroyBody(BodyId id);

    // Query position. Returns 0,0,0 for invalid ids.
    void getBodyPosition(BodyId id, float& px, float& py, float& pz) const;

    // Query rotation as a quaternion (x, y, z, w). Returns identity (0,0,0,1)
    // for invalid ids.
    void getBodyRotation(BodyId id, float& qx, float& qy, float& qz, float& qw) const;

    // Cast a ray from origin in direction (normalized). Returns true on hit
    // and writes the hit point + distance. distance is treated as a max.
    bool raycast(float ox, float oy, float oz, float dx, float dy, float dz, float maxDistance,
                 float& hitX, float& hitY, float& hitZ, float& hitDistance) const;

    // Apply a continuous force at the body's center of mass for one step.
    // Effective only on dynamic bodies; no-op for static/kinematic.
    void applyForce(BodyId id, float fx, float fy, float fz);

    // Apply an instantaneous impulse (mass-independent velocity kick).
    void applyImpulse(BodyId id, float ix, float iy, float iz);

    // Teleport a body to a new position. Useful for kinematic-style movement
    // and respawn logic.
    void setBodyPosition(BodyId id, float px, float py, float pz, bool activate = true);

    // Linear / angular velocity. Setting is direct — no impulse calculation;
    // useful for character controllers that drive bodies imperatively.
    void getLinearVelocity(BodyId id, float& vx, float& vy, float& vz) const;
    void setLinearVelocity(BodyId id, float vx, float vy, float vz);
    void getAngularVelocity(BodyId id, float& wx, float& wy, float& wz) const;
    void setAngularVelocity(BodyId id, float wx, float wy, float wz);

   private:
    // Opaque pointer to the active IPhysicsBackend. The header defining that
    // interface lives in include/physics_backend.h; physics.cpp's factory
    // picks Jolt (default) / PhysX / null based on the CMake-selected backend.
    void* state = nullptr;
    bool ready = false;
};
