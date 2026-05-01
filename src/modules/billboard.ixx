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

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvHeap;
    Microsoft::WRL::ComPtr<ID3D12Resource> spriteTexture;
    Microsoft::WRL::ComPtr<ID3D12Resource> quadVertexBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> instanceBuffer;
    BillboardInstance* mappedInstances = nullptr;
    D3D12_VERTEX_BUFFER_VIEW quadVBV{};
    D3D12_VERTEX_BUFFER_VIEW instanceVBV{};
    uint32_t instanceCount = 0;
    float spriteSize = 0.35f;

    void init(gfx::IDevice& dev, ID3D12CommandQueue* queue, const wchar_t* texturePath);
    void updateInstances(const vec4* lightPos, const vec4* lightColor, uint32_t count);
    void render(
        gfx::ICommandList& cmdRef,
        const mat4& viewProj,
        const vec3& cameraPos
    );
};
