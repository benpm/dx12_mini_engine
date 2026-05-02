module;

#include <cstdint>

module gbuffer;

GBuffer::~GBuffer()
{
    if (devForDestroy) {
        for (int i = 0; i < Count; ++i) {
            if (resources[i].isValid()) {
                devForDestroy->destroy(resources[i]);
            }
        }
    }
}

void GBuffer::createResources(gfx::IDevice& dev, uint32_t width, uint32_t height)
{
    devForDestroy = &dev;
    createTextures(dev, width, height);
}

void GBuffer::resize(gfx::IDevice& dev, uint32_t width, uint32_t height)
{
    devForDestroy = &dev;
    for (int i = 0; i < Count; ++i) {
        if (resources[i].isValid()) {
            dev.destroy(resources[i]);
            resources[i] = {};
        }
    }
    createTextures(dev, width, height);
}

void GBuffer::createTextures(gfx::IDevice& dev, uint32_t width, uint32_t height)
{
    static constexpr gfx::Format formats[Count] = {
        gfx::Format::RGBA8Unorm,  // Normal
        gfx::Format::RGBA8Unorm,  // Albedo
        gfx::Format::RG8Unorm,    // Material
        gfx::Format::RG16Float,   // Motion
    };

    for (int i = 0; i < Count; ++i) {
        gfx::TextureDesc td{};
        td.width = width;
        td.height = height;
        td.format = formats[i];
        td.usage = gfx::TextureUsage::RenderTarget;
        td.initialState = gfx::ResourceState::Common;
        td.useClearValue = true;
        if (i == Normal) {
            td.clearColor[0] = 0.5f;
            td.clearColor[1] = 0.5f;
            td.clearColor[2] = 1.0f;
            td.clearColor[3] = 1.0f;
        }
        td.debugName = i == Normal     ? "gbuffer_normal"
                       : i == Albedo   ? "gbuffer_albedo"
                       : i == Material ? "gbuffer_material"
                                       : "gbuffer_motion";
        resources[i] = dev.createTexture(td);
    }
}
