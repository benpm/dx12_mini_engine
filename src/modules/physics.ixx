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

   private:
    void* state = nullptr;  // opaque PhysicsWorldImpl* (defined in physics.cpp)
    bool ready = false;
};
