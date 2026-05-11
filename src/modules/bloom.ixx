module;

#include <d3d12.h>
#include <Windows.h>
#include <wrl.h>
#include <cstdint>

export module bloom;

import common;
export import gfx;

using Microsoft::WRL::ComPtr;

// ---------------------------------------------------------------------------
// BloomRenderer — owns HDR RT, bloom mip textures, heaps, PSOs, root sig
// ---------------------------------------------------------------------------
export class BloomRenderer
{
   public:
    static constexpr uint32_t bloomMipCount = 5;

    gfx::TextureHandle hdrRT{};
    gfx::TextureHandle bloomMips[bloomMipCount]{};

    void createResources(gfx::IDevice& dev, uint32_t width, uint32_t height);
    void resize(gfx::IDevice& dev, uint32_t width, uint32_t height);

    struct SkyParams
    {
        vec3 camForward, camRight, camUp;
        vec3 sunDir;
        float aspectRatio, tanHalfFov;
        float time;
    };

    void render(
        gfx::ICommandList& cmdRef,
        uint64_t backBufRtv,
        uint32_t width,
        uint32_t height,
        float threshold,
        float intensity,
        int tonemapMode,
        const SkyParams& sky
    );

    void reloadPipelines(
        gfx::IDevice& dev,
        gfx::ShaderBytecode fullscreenVS = {},
        gfx::ShaderBytecode prefilterPS = {},
        gfx::ShaderBytecode downsamplePS = {},
        gfx::ShaderBytecode upsamplePS = {},
        gfx::ShaderBytecode compositePS = {}
    );

    ~BloomRenderer();

   private:
    // PSOs and their backing shaders are owned by gfx now. The destructor
    // releases everything through devForDestroy->destroy().
    gfx::PipelineHandle prefilterPSO{};
    gfx::PipelineHandle downsamplePSO{};
    gfx::PipelineHandle upsamplePSO{};
    gfx::PipelineHandle compositePSO{};
    gfx::ShaderHandle vsHandle{};
    gfx::ShaderHandle prefilterPSShader{};
    gfx::ShaderHandle downsamplePSShader{};
    gfx::ShaderHandle upsamplePSShader{};
    gfx::ShaderHandle compositePSShader{};

    gfx::IDevice* devForDestroy = nullptr;

    void createTexturesAndHeaps(gfx::IDevice& dev, uint32_t width, uint32_t height);
    void createPipelines(gfx::IDevice& dev);
};
