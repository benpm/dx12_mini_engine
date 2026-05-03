module;

#include <d3d12.h>
#include <imgui.h>
#include <imgui_impl_dx12.h>
#include <imgui_impl_win32.h>
#include <Windows.h>

module application;

// ---------------------------------------------------------------------------
// ImGui render (app-specific UI) - Orchestrator
// ---------------------------------------------------------------------------

void Application::renderImGui(gfx::ICommandList& cmdRef)
{
    auto* cmdList = static_cast<ID3D12GraphicsCommandList2*>(cmdRef.nativeHandle());
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    bool shadowPsoDirty = false;

    // Modular UI components
    uiMenuBar(shadowPsoDirty);
    uiMetrics();
    uiInspector();
    uiOverlay();

    if (shadowPsoDirty) {
        auto vsData = shaderCompiler.data(sceneVSIdx);
        gfx::ShaderBytecode vs =
            vsData ? gfx::ShaderBytecode{ vsData, shaderCompiler.size(sceneVSIdx) }
                   : gfx::ShaderBytecode{};
        shadow.reloadPSO(*gfxDevice, vs);
    }

    ImGui::Render();

    ID3D12DescriptorHeap* heaps[] = { imguiLayer.srvHeap.Get() };
    cmdList->SetDescriptorHeaps(1, heaps);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), cmdList);
}
