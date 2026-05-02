module;

#include <d3d12.h>
#include <Windows.h>
#include <wrl.h>

export module imgui_layer;

import common;
export import gfx;

using Microsoft::WRL::ComPtr;

// ---------------------------------------------------------------------------
// ImGuiLayer — owns the imgui SRV descriptor heap and init / shutdown logic
// ---------------------------------------------------------------------------
export class ImGuiLayer
{
   public:
    ComPtr<ID3D12DescriptorHeap> srvHeap;
    UINT srvDescSize = 0;
    UINT nextIndex = 0;

    void init(
        HWND hwnd,
        gfx::IDevice& dev,
        ID3D12CommandQueue* queue,
        UINT frameCount,
        gfx::Format rtvFormat
    );
    void shutdown();
    static void styleColorsDracula();
};
