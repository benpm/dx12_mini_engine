module;

#include <DirectXMath.h>
#include <cstdint>

export module ecs_components;

using DirectX::XMFLOAT4X4;

// ---------------------------------------------------------------------------
// ECS Components
// ---------------------------------------------------------------------------

// World-space transform
export struct Transform
{
    XMFLOAT4X4 world{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
};

// Reference into shared mega vertex/index buffers
export struct MeshRef
{
    uint32_t vertexOffset{ 0 };
    uint32_t indexOffset{ 0 };
    uint32_t indexCount{ 0 };
    int materialIndex{ 0 };
};
