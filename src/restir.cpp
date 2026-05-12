module;

#include <cstdint>

module restir;

void ReStirRenderer::createResources(gfx::IDevice& dev, uint32_t width, uint32_t height)
{
    devForDestroy = &dev;
    createTextures(dev, width, height);
    // createShaders() used to build a compute root signature, but ReStir's
    // dispatch is still a stub — the root sig was orphaned. When the actual
    // ReStir shaders land, they'll allocate through gfx::createComputePipeline
    // and stop needing a hand-rolled D3D12 root sig in this subsystem.
}

void ReStirRenderer::resize(gfx::IDevice& dev, uint32_t width, uint32_t height)
{
    for (auto& h : reservoirs) {
        if (h.isValid()) {
            dev.destroy(h);
            h = {};
        }
    }
    createTextures(dev, width, height);
}

void ReStirRenderer::render(
    gfx::ICommandList& cmdRef,
    gfx::TextureHandle /*tlas*/,
    gfx::BufferHandle /*lightBuffer*/,
    uint32_t lightCount,
    gfx::TextureHandle /*normalRT*/,
    gfx::TextureHandle /*albedoRT*/,
    gfx::TextureHandle /*materialRT*/,
    gfx::TextureHandle /*motionRT*/,
    gfx::TextureHandle /*depthBuffer*/,
    gfx::TextureHandle /*outputHdrRT*/,
    uint64_t perFrameCBAddr,
    uint32_t /*width*/,
    uint32_t /*height*/,
    uint32_t /*frameIndex*/
)
{
    if (!settings.enabled || lightCount == 0) {
        return;
    }

    // ReStir dispatch is still a stub — once the compute shaders land, this
    // body will transition reservoirs[] to UnorderedAccess via the gfx
    // command list, bind the bindless root sig, dispatch the four PSOs, and
    // transition back. Avoid touching command list state today since nothing
    // actually executes.
    (void)cmdRef;
    (void)perFrameCBAddr;
}

void ReStirRenderer::createTextures(gfx::IDevice& dev, uint32_t width, uint32_t height)
{
    for (auto& h : reservoirs) {
        gfx::TextureDesc td{};
        td.width = width;
        td.height = height;
        td.format = gfx::Format::RGBA32Uint;
        td.usage = gfx::TextureUsage::UnorderedAccess | gfx::TextureUsage::ShaderResource;
        td.initialState = gfx::ResourceState::Common;
        td.debugName = "restir_reservoir";
        h = dev.createTexture(td);
    }
}

void ReStirRenderer::createShaders(gfx::IDevice& /*dev*/)
{
    // Intentionally a stub now — see createResources().
}
