module;

#include <flecs.h>
#include <imgui.h>
#include <string>

module application;

void Application::uiOverlay()
{
    // --- Hover tooltip ---
    if (hoveredEntity.is_alive() && hoveredEntity != selectedEntity &&
        !ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow)) {
        ImGui::SetNextWindowPos(ImVec2(mousePos.x + 15.0f, mousePos.y + 15.0f), ImGuiCond_Always);
        ImGui::Begin(
            "##hover", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoFocusOnAppearing
        );
        ImGui::Text("Entity %llu", hoveredEntity.id());
        if (hoveredEntity.has<MeshRef>()) {
            auto mr = hoveredEntity.get<MeshRef>();
            ImGui::Text("Material: %d", mr.materialIndex);
        }
        ImGui::Text("Click to select");
        ImGui::End();
    }

    // --- Scene title/description overlay (bottom-right) ---
    if (!sceneTitle.empty() || !sceneDescription.empty()) {
        const float margin = 10.0f;
        ImGuiIO& io = ImGui::GetIO();
        ImVec2 displaySize = io.DisplaySize;
        ImDrawList* dl = ImGui::GetForegroundDrawList();

        float y = displaySize.y - margin;
        if (!sceneDescription.empty()) {
            ImVec2 descSz = ImGui::CalcTextSize(sceneDescription.c_str());
            y -= descSz.y;
            float x = displaySize.x - descSz.x - margin;
            dl->AddText(ImVec2(x + 1, y + 1), IM_COL32(0, 0, 0, 180), sceneDescription.c_str());
            dl->AddText(ImVec2(x, y), IM_COL32(200, 200, 200, 220), sceneDescription.c_str());
            y -= margin * 0.5f;
        }
        if (!sceneTitle.empty()) {
            ImFont* font = ImGui::GetFont();
            float titleFontSz = ImGui::GetFontSize() * 1.4f;
            ImVec2 titleSz = font->CalcTextSizeA(titleFontSz, FLT_MAX, 0.0f, sceneTitle.c_str());
            y -= titleSz.y;
            float x = displaySize.x - titleSz.x - margin;
            dl->AddText(
                font, titleFontSz, ImVec2(x + 1, y + 1), IM_COL32(0, 0, 0, 180), sceneTitle.c_str()
            );
            dl->AddText(
                font, titleFontSz, ImVec2(x, y), IM_COL32(255, 255, 255, 230), sceneTitle.c_str()
            );
        }
    }
}
