#pragma once

#include "math_types.h"

#include <cstdint>

// World-space transform
struct Transform
{
    mat4 world;
};

// Animation state for orbiting entities
struct Animated
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
struct Pickable
{
};

// Animated point light (position computed from center + amp * sin(freq * t))
struct PointLight
{
    vec3 center;
    vec3 amp;
    vec3 freq;
    vec4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
};

// Reference into shared mega vertex/index buffers
struct MeshRef
{
    uint32_t vertexOffset{ 0 };
    uint32_t indexOffset{ 0 };
    uint32_t indexCount{ 0 };
    int materialIndex{ 0 };
    // Per-entity albedo override; w==0 means "use material albedo"
    vec4 albedoOverride{ 0.0f, 0.0f, 0.0f, 0.0f };
};
