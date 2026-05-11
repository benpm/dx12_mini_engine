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
    // Sized generously so the same buffer can host lights (≤8) AND the bulk of
    // particle billboards (≤4096) without a second draw call. ~144 KB at
    // sizeof(BillboardInstance)≈36, trivial.
    static constexpr uint32_t maxInstances = 4096;

    // Pipeline + backing shaders are gfx-managed now. Destructor releases them.
    gfx::PipelineHandle pipelineState{};
    gfx::ShaderHandle vsHandle{};
    gfx::ShaderHandle psHandle{};
    // spriteTexture is owned through gfx now (was raw ComPtr<ID3D12Resource>).
    // Resource still comes from DirectXTK's WICTextureLoader; adoptTexture
    // wraps it in a gfx::TextureHandle so the lifetime + SRV slot are managed
    // through the same pendingDestroys path as engine-allocated textures.
    gfx::TextureHandle spriteTexture{};
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
    // Append particle billboards after the most recent updateInstances call.
    // Caller passes parallel arrays (positions/colors/sizes). Returns the number
    // of particles actually appended (clamped to maxInstances).
    uint32_t appendParticles(
        const vec3* positions, const vec4* colors, const float* sizes, uint32_t count
    );
    void render(gfx::ICommandList& cmdRef, const mat4& viewProj, const vec3& cameraPos);

    ~BillboardRenderer();

   private:
    gfx::IDevice* devForDestroy = nullptr;
};
