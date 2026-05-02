// SwapChain impl for the gfx D3D12 backend.

#include "d3d12_internal.h"

import common;

using Microsoft::WRL::ComPtr;

namespace gfxd3d12
{

    SwapChain::SwapChain(Device* dev, const gfx::SwapChainDesc& desc)
        : device(dev),
          hwnd_(reinterpret_cast<HWND>(desc.nativeWindowHandle)),
          width_(desc.width),
          height_(desc.height),
          bufferCount_(desc.bufferCount),
          format_(desc.format),
          allowTearing_(desc.allowTearing)
    {
        DXGI_SWAP_CHAIN_DESC1 sd{};
        sd.Width = width_;
        sd.Height = height_;
        sd.Format = toDXGI(format_);
        sd.Stereo = FALSE;
        sd.SampleDesc = { 1, 0 };
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.BufferCount = bufferCount_;
        sd.Scaling = DXGI_SCALING_STRETCH;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        sd.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
        sd.Flags = allowTearing_ ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

        ComPtr<IDXGISwapChain1> sc1;
        auto* queueNative = static_cast<ID3D12CommandQueue*>(dev->graphicsQueue()->nativeHandle());
        chkDX(
            dev->dxgiFactory()->CreateSwapChainForHwnd(
                queueNative, hwnd_, &sd, nullptr, nullptr, &sc1
            ),
            "CreateSwapChainForHwnd"
        );
        chkDX(sc1.As(&swap), "SwapChain QI IDXGISwapChain4");
        chkDX(
            dev->dxgiFactory()->MakeWindowAssociation(hwnd_, DXGI_MWA_NO_ALT_ENTER),
            "MakeWindowAssociation"
        );

        createBackBufferTextures();
    }

    SwapChain::~SwapChain()
    {
        releaseBackBufferTextures();
    }

    void SwapChain::createBackBufferTextures()
    {
        backBufferHandles.resize(bufferCount_);
        for (uint32_t i = 0; i < bufferCount_; ++i) {
            ComPtr<ID3D12Resource> bb;
            chkDX(swap->GetBuffer(i, IID_PPV_ARGS(&bb)), "SwapChain::GetBuffer");
            backBufferHandles[i] = device->adoptBackBuffer(bb, format_);
        }
    }

    void SwapChain::releaseBackBufferTextures()
    {
        for (auto h : backBufferHandles) {
            device->releaseBackBuffer(h);
        }
        backBufferHandles.clear();
    }

    void SwapChain::resize(uint32_t w, uint32_t h)
    {
        if (w == 0 || h == 0) {
            return;
        }
        if (w == width_ && h == height_) {
            return;
        }
        releaseBackBufferTextures();
        UINT flags = allowTearing_ ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
        chkDX(
            swap->ResizeBuffers(bufferCount_, w, h, toDXGI(format_), flags),
            "SwapChain::ResizeBuffers"
        );
        width_ = w;
        height_ = h;
        createBackBufferTextures();
    }

    gfx::TextureHandle SwapChain::currentBackBuffer()
    {
        return backBufferHandles[swap->GetCurrentBackBufferIndex()];
    }

    uint32_t SwapChain::currentIndex() const
    {
        return swap->GetCurrentBackBufferIndex();
    }

    void SwapChain::present(bool vsync)
    {
        UINT sync = vsync ? 1 : 0;
        UINT flags = (!vsync && allowTearing_) ? DXGI_PRESENT_ALLOW_TEARING : 0;
        chkDX(swap->Present(sync, flags), "SwapChain::Present");
    }

}  // namespace gfxd3d12
