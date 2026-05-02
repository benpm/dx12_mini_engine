module;

#include <cstdint>

export module gbuffer;

export import common;
export import gfx;

export class GBuffer
{
   public:
    enum TextureType
    {
        Normal = 0,
        Albedo,
        Material,
        Motion,
        Count
    };

    gfx::TextureHandle resources[Count];

    void createResources(gfx::IDevice& dev, uint32_t width, uint32_t height);
    void resize(gfx::IDevice& dev, uint32_t width, uint32_t height);
    ~GBuffer();

   private:
    void createTextures(gfx::IDevice& dev, uint32_t width, uint32_t height);

    gfx::IDevice* devForDestroy = nullptr;
};
