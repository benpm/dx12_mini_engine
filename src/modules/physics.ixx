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

   private:
    void* state = nullptr;  // opaque PhysicsWorldImpl* (defined in physics.cpp)
    bool ready = false;
};
