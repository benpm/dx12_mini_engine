module;

#include <d3d12.h>
#include <Windows.h>
#include <wrl.h>
#include <cstdint>

export module billboard;

import common;
export import math;
export import gfx;

export struct BillboardInstance
{
    vec3 position;
    vec4 color;
    float size;
};

export class BillboardRenderer
{
   public:
    static constexpr uint32_t maxInstances = 64;

    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState;
    Microsoft::WRL::ComPtr<ID3D12Resource> spriteTexture;
    uint32_t spriteSrvIdx = 0;
    gfx::BufferHandle quadVertexBuffer{};
    gfx::BufferHandle instanceBuffer{};
    BillboardInstance* mappedInstances = nullptr;
    gfx::VertexBufferView quadVBV{};
    gfx::VertexBufferView instanceVBV{};
    uint32_t instanceCount = 0;
    float spriteSize = 0.35f;

    void init(gfx::IDevice& dev, const wchar_t* texturePath);
    void updateInstances(const vec4* lightPos, const vec4* lightColor, uint32_t count);
    void render(gfx::ICommandList& cmdRef, const mat4& viewProj, const vec3& cameraPos);

    ~BillboardRenderer();

   private:
    gfx::IDevice* devForDestroy = nullptr;
};
