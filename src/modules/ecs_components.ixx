module;

#include <cstdint>

export module ecs_components;

export import math;

// ---------------------------------------------------------------------------
// ECS Components
// ---------------------------------------------------------------------------

// World-space transform
export struct Transform
{
    mat4 world;
};

// Animation state for orbiting entities
export struct Animated
{
    float speed;         // orbital speed (rad/s)
    float orbitRadius;   // distance from Y axis
    float orbitAngle;    // current angle (updated each frame)
    float orbitY;        // fixed Y height
    float initialScale;  // preserved from spawn
    vec3 rotAxis;        // preserved rotation axis
    float rotAngle;      // preserved rotation angle
    float pulsePhase;    // phase offset for scale pulsing
};

// Tag: entity is pickable via ID render pass
export struct Pickable
{
};

// Reference into shared mega vertex/index buffers
export struct MeshRef
{
    uint32_t vertexOffset{ 0 };
    uint32_t indexOffset{ 0 };
    uint32_t indexCount{ 0 };
    int materialIndex{ 0 };
    // Per-entity albedo override; w==0 means "use material albedo"
    vec4 albedoOverride{ 0.0f, 0.0f, 0.0f, 0.0f };
};
