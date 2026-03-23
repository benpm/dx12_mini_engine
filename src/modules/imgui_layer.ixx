module;

#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>

export module imgui_layer;

import common;

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
        ID3D12Device2* device,
        ID3D12CommandQueue* queue,
        UINT frameCount,
        DXGI_FORMAT rtvFormat
    );
    void shutdown();
    static void styleColorsDracula();
};
