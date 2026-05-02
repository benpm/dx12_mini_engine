module;

#include <d3d12.h>
#include <Windows.h>
#include <cstdint>

export module object_picking;

import common;
export import gfx;

export class ObjectPicker
{
   public:
    static constexpr uint32_t readbackRingSize = 3;

    void createResources(
        gfx::IDevice& dev,
        uint32_t width,
        uint32_t height,
        ID3D12RootSignature* rootSig
    );
    void resize(gfx::IDevice& dev, uint32_t width, uint32_t height);

    // Read back the result from a previously submitted copy once its fence is complete.
    void readPickResult(uint64_t completedFenceValue);

    // Attach the submission fence value for the pending readback copy.
    void setPendingReadbackFence(uint64_t fenceValue);

    // Copy the pixel at (x, y) from the ID RT to the readback buffer
    void copyPickedPixel(gfx::ICommandList& cmdRef, uint32_t x, uint32_t y);

    uint64_t getRTV() const;
    uint64_t getDSV() const;

    gfx::PipelineHandle pso{};

    static constexpr uint32_t invalidID = UINT32_MAX;
    uint32_t pickedIndex = invalidID;

    ~ObjectPicker();

   private:
    struct ReadbackSlot
    {
        gfx::BufferHandle buffer{};
        bool pendingRead = false;
        uint64_t fenceValue = 0;
    };

    gfx::TextureHandle idRT{};
    gfx::TextureHandle depthBuffer{};
    ReadbackSlot readbackSlots[readbackRingSize];
    uint32_t writeSlot = 0;
    uint32_t width_ = 0;
    uint32_t height_ = 0;

    gfx::IDevice* devForDestroy = nullptr;
    gfx::ShaderHandle vsHandle{};
    gfx::ShaderHandle psHandle{};
};
