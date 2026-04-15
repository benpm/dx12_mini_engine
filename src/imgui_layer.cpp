module;

#include <d3d12.h>
#include <dxgi1_6.h>
#include <imgui.h>
#include <imgui_impl_dx12.h>
#include <imgui_impl_win32.h>
#include <Windows.h>
#include <wrl.h>
#include <string>
#include "d3dx12_clean.h"
#include "icons.h"

module imgui_layer;

using Microsoft::WRL::ComPtr;

// ---------------------------------------------------------------------------
// ImGuiLayer::styleColorsDracula
// ---------------------------------------------------------------------------

void ImGuiLayer::styleColorsDracula()
{
    auto& colors = ImGui::GetStyle().Colors;
    colors[ImGuiCol_Text] = ImVec4(0.95f, 0.96f, 0.98f, 1.00f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.36f, 0.42f, 0.47f, 1.00f);
    colors[ImGuiCol_WindowBg] = ImVec4(0.01f, 0.05f, 0.07f, 1.00f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.15f, 0.18f, 0.22f, 1.00f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.08f, 0.08f, 0.08f, 0.94f);
    colors[ImGuiCol_Border] = ImVec4(0.08f, 0.10f, 0.12f, 1.00f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.20f, 0.25f, 0.29f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.12f, 0.20f, 0.28f, 1.00f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.09f, 0.12f, 0.14f, 1.00f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.09f, 0.12f, 0.14f, 0.65f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.08f, 0.10f, 0.12f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.00f, 0.00f, 0.00f, 0.51f);
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.15f, 0.18f, 0.22f, 1.00f);
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.02f, 0.02f, 0.02f, 0.39f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.20f, 0.25f, 0.29f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.18f, 0.22f, 0.25f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.09f, 0.21f, 0.31f, 1.00f);
    colors[ImGuiCol_CheckMark] = ImVec4(0.28f, 0.56f, 1.00f, 1.00f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.28f, 0.56f, 1.00f, 1.00f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.37f, 0.61f, 1.00f, 1.00f);
    colors[ImGuiCol_Button] = ImVec4(0.20f, 0.25f, 0.29f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.28f, 0.56f, 1.00f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.06f, 0.53f, 0.98f, 1.00f);
    colors[ImGuiCol_Header] = ImVec4(0.20f, 0.25f, 0.29f, 0.55f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.80f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
    colors[ImGuiCol_Separator] = ImVec4(0.20f, 0.25f, 0.29f, 1.00f);
    colors[ImGuiCol_SeparatorHovered] = ImVec4(0.10f, 0.40f, 0.75f, 0.78f);
    colors[ImGuiCol_SeparatorActive] = ImVec4(0.10f, 0.40f, 0.75f, 1.00f);
    colors[ImGuiCol_ResizeGrip] = ImVec4(0.26f, 0.59f, 0.98f, 0.25f);
    colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.67f);
    colors[ImGuiCol_ResizeGripActive] = ImVec4(0.26f, 0.59f, 0.98f, 0.95f);
    colors[ImGuiCol_Tab] = ImVec4(0.11f, 0.15f, 0.17f, 1.00f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.80f);
    colors[ImGuiCol_TabActive] = ImVec4(0.20f, 0.25f, 0.29f, 1.00f);
    colors[ImGuiCol_TabUnfocused] = ImVec4(0.11f, 0.15f, 0.17f, 1.00f);
    colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.11f, 0.15f, 0.17f, 1.00f);
    colors[ImGuiCol_PlotLines] = ImVec4(0.61f, 0.61f, 0.61f, 1.00f);
    colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
    colors[ImGuiCol_PlotHistogram] = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
    colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
    colors[ImGuiCol_TextSelectedBg] = ImVec4(0.26f, 0.59f, 0.98f, 0.35f);
    colors[ImGuiCol_DragDropTarget] = ImVec4(1.00f, 1.00f, 0.00f, 0.90f);
    colors[ImGuiCol_NavHighlight] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
    colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
    colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.35f);

    auto& style = ImGui::GetStyle();
    style.TabRounding = 4;
    style.ScrollbarRounding = 4;
    style.WindowRounding = 7;
    style.GrabRounding = 3;
    style.FrameRounding = 3;
    style.PopupRounding = 4;
    style.ChildRounding = 4;
}

// ---------------------------------------------------------------------------
// ImGuiLayer::init
// ---------------------------------------------------------------------------

void ImGuiLayer::init(
    HWND hwnd,
    ID3D12Device2* device,
    ID3D12CommandQueue* queue,
    UINT frameCount,
    DXGI_FORMAT rtvFormat
)
{
    {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        desc.NumDescriptors = 16;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        chkDX(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&srvHeap)));
    }
    srvDescSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui_ImplWin32_Init(hwnd);

    float dpiScale = ImGui_ImplWin32_GetDpiScaleForHwnd(hwnd) * 1.0f;
    ImGui::GetStyle().ScaleAllSizes(dpiScale);

    float fontSize = 13.0f * dpiScale;
    {
        char exePath[MAX_PATH] = {};
        GetModuleFileNameA(nullptr, exePath, MAX_PATH);
        std::string fontPath = exePath;
        auto lastSlash = fontPath.find_last_of("\\/");
        if (lastSlash != std::string::npos) {
            fontPath = fontPath.substr(0, lastSlash + 1);
        }
        fontPath += "Roboto-Medium.ttf";

        ImFontConfig fontCfg;
        fontCfg.OversampleH = 2;
        fontCfg.OversampleV = 2;
        if (GetFileAttributesA(fontPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
            io.Fonts->AddFontFromFileTTF(fontPath.c_str(), fontSize, &fontCfg);
        } else {
            io.Fonts->AddFontDefault();
            io.FontGlobalScale = dpiScale;
        }

        // Merge Material Icons font (icon glyphs in PUA range)
        std::string iconFontPath = fontPath.substr(0, fontPath.find_last_of("\\/") + 1);
        iconFontPath += "MaterialIcons-Regular.ttf";
        if (GetFileAttributesA(iconFontPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
            static const ImWchar iconRanges[] = { IconCP::MIN, IconCP::MAX, 0 };
            ImFontConfig iconCfg;
            iconCfg.MergeMode = true;
            iconCfg.PixelSnapH = true;
            iconCfg.GlyphMinAdvanceX = fontSize;
            iconCfg.GlyphOffset.y = fontSize * 0.2f;
            io.Fonts->AddFontFromFileTTF(iconFontPath.c_str(), fontSize, &iconCfg, iconRanges);
        }
    }

    styleColorsDracula();

    ImGui_ImplDX12_InitInfo initInfo = {};
    initInfo.Device = device;
    initInfo.CommandQueue = queue;
    initInfo.NumFramesInFlight = frameCount;
    initInfo.RTVFormat = rtvFormat;
    initInfo.DSVFormat = DXGI_FORMAT_UNKNOWN;
    initInfo.SrvDescriptorHeap = srvHeap.Get();
    initInfo.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo* info,
                                       D3D12_CPU_DESCRIPTOR_HANDLE* outCpu,
                                       D3D12_GPU_DESCRIPTOR_HANDLE* outGpu) {
        auto* layer = static_cast<ImGuiLayer*>(info->UserData);
        *outCpu = CD3DX12_CPU_DESCRIPTOR_HANDLE(
            layer->srvHeap->GetCPUDescriptorHandleForHeapStart(),
            static_cast<INT>(layer->nextIndex), layer->srvDescSize
        );
        *outGpu = CD3DX12_GPU_DESCRIPTOR_HANDLE(
            layer->srvHeap->GetGPUDescriptorHandleForHeapStart(),
            static_cast<INT>(layer->nextIndex), layer->srvDescSize
        );
        layer->nextIndex++;
    };
    initInfo.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE,
                                      D3D12_GPU_DESCRIPTOR_HANDLE) {};
    initInfo.UserData = this;
    ImGui_ImplDX12_Init(&initInfo);
}

// ---------------------------------------------------------------------------
// ImGuiLayer::shutdown
// ---------------------------------------------------------------------------

void ImGuiLayer::shutdown()
{
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
}
