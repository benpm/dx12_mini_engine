module;

#include <d3d12.h>
#include <wrl.h>
#include <cstdint>
#include <vector>
#include "d3dx12_clean.h"

export module shadow;

export import scene;
export import common;
export import gfx;

export class ShadowRenderer
{
   public:
    // Settings
    bool enabled = true;
    float bias = 0.0002f;
    int rasterDepthBias = 1000;
    float rasterSlopeBias = 1.0f;
    float rasterBiasClamp = 0.0f;
    float lightDistance = 25.0f;
    float orthoSize = 30.0f;
    float nearPlane = 0.1f;
    float farPlane = 60.0f;

    static constexpr uint32_t mapSize = 2048;

    gfx::TextureHandle shadowMap{};
    gfx::PipelineHandle pso{};
    gfx::ShaderHandle vsHandle{};
    gfx::IDevice* devForDestroy = nullptr;
    ~ShadowRenderer();

    void createResources(gfx::IDevice& dev, ID3D12RootSignature* rootSig, D3D12_SHADER_BYTECODE vs);

    void reloadPSO(gfx::IDevice& dev, ID3D12RootSignature* rootSig, D3D12_SHADER_BYTECODE vs);

    // Compute the light view-proj matrix from the current config.
    // dirLightDir is the direction FROM the light (as stored in Application::dirLightDir).
    mat4 computeLightViewProj(vec3 dirLightDir) const;

    void render(
        gfx::ICommandList& cmdRef,
        const D3D12_VERTEX_BUFFER_VIEW& vbv,
        const D3D12_INDEX_BUFFER_VIEW& ibv,
        ID3D12DescriptorHeap* srvHeap,
        D3D12_GPU_DESCRIPTOR_HANDLE perObjHandle,
        const std::vector<DrawCmd>& drawCmds,
        uint32_t totalSlots
    );
};
