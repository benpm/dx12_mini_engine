#pragma once

#include "lua_script_types.h"
#include "math_types.h"

#include <cstdint>
#include <vector>

// World-space transform
struct Transform
{
    mat4 world;
};

struct PrevTransform
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
    uint32_t vertexCount{ 0 };
    uint32_t indexOffset{ 0 };
    uint32_t indexCount{ 0 };
    int materialIndex{ 0 };
    // Per-entity albedo override; w==0 means "use material albedo"
    vec4 albedoOverride{ 0.0f, 0.0f, 0.0f, 0.0f };
};

// GPU-instanced group: many instances of a single mesh drawn in one call
struct InstanceGroup
{
    MeshRef mesh;
    std::vector<mat4> transforms;
    std::vector<vec4> albedoOverrides;
    std::vector<float> roughnessOverrides;         // empty = use material value
    std::vector<float> metallicOverrides;          // empty = use material value
    std::vector<float> emissiveStrengthOverrides;  // empty = use material value
};

struct PrevInstanceGroup
{
    std::vector<mat4> transforms;
};

// Tag: marks the procedurally-generated terrain entity
struct TerrainEntity
{
};

// Translation gizmo arrow axis
enum class GizmoAxis : uint8_t
{
    X = 0,
    Y = 1,
    Z = 2
};

// Tag: marks a gizmo arrow entity
struct GizmoArrow
{
    GizmoAxis axis;
};

// Per-frame Y-axis rotation for an InstanceGroup entity
struct InstanceAnimation
{
    float rotationSpeed;  // rad/s
    float currentAngle = 0.0f;
    std::vector<vec3> positions;  // per-instance base position (no rotation)
    std::vector<float> scales;    // per-instance uniform scale
};
