module;

#include <d3d12.h>
#include <flecs.h>
#include <cstdint>

export module gizmo;

export import scene;
export import camera;
export import ecs_components;
export import common;

export struct GizmoState
{
    flecs::entity arrows[3];
    MeshRef arrowMeshRef;
    int materialIndices[3] = { -1, -1, -1 };

    // Drag state
    bool dragging = false;
    GizmoAxis dragAxis = GizmoAxis::X;
    vec3 dragStartEntityPos;
    vec2 dragStartMouse;
    vec2 dragScreenDir;  // screen-space axis direction (normalized)

    void init(Scene& scene, ID3D12Device2* device, CommandQueue& cmdQueue);
    void update(
        Scene& scene,
        flecs::entity& selectedEntity,
        const OrbitCamera& cam,
        const D3D12_VIEWPORT& viewport,
        vec2 mousePos,
        bool leftDown,
        bool leftWasDown,
        uint32_t pickedIndex
    );
    bool isGizmoEntity(flecs::entity e) const;
    bool isDragging() const { return dragging; }
};
