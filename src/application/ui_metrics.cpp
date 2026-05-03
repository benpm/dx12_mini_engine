module;

#include <flecs.h>
#include <imgui.h>
#include <algorithm>
#include <string>
#include <vector>

module application;

void Application::uiMetrics()
{
    if (showMetrics) {
        if (ImGui::Begin(
                iconLabel("window.Metrics", "Metrics").c_str(), &showMetrics,
                ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing |
                    ImGuiWindowFlags_NoNav
            )) {
#ifdef NDEBUG
            constexpr const char* buildMode = "Release";
#else
            constexpr const char* buildMode = "Debug";
#endif
            ImGui::Text("Build: %s", buildMode);
            ImGui::Separator();
            ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
            ImGui::Text("Frame: %.2f ms", this->lastFrameMs);

            // FPS chart (last 5 seconds)
            if (ImGui::CollapsingHeader("FPS Graph")) {
                int count = std::min(fpsHistoryHead, fpsHistorySize);
                if (count > 0) {
                    // Build contiguous array for PlotLines
                    float plotBuf[fpsHistorySize];
                    int start = (fpsHistoryHead - count + fpsHistorySize) % fpsHistorySize;
                    for (int i = 0; i < count; ++i) {
                        plotBuf[i] = fpsHistory[(start + i) % fpsHistorySize];
                    }
                    float maxFps = *std::max_element(plotBuf, plotBuf + count);
                    ImGui::PlotLines(
                        "##fps", plotBuf, count, 0, nullptr, 0.0f, maxFps * 1.1f, ImVec2(280, 60)
                    );
                }
            }

            ImGui::Separator();
            ImGui::Text("Draw Calls: %u", this->lastFrameDrawCalls);
            ImGui::Text("Objects: %u", this->lastFrameObjectCount);
            ImGui::Text("Vertices: %u", this->lastFrameVertexCount);
            ImGui::Text("Occlusion Culled: %u", this->lastFrameOcclusionCulled);

            ImGui::Separator();
            // Entity & component counts from ECS
            int entityCount = 0;
            int meshRefCount = 0;
            int animatedCount = 0;
            int instanceGroupCount = 0;
            int pointLightCount = 0;
            int pickableCount = 0;
            scene.ecsWorld.each([&](flecs::entity) { entityCount++; });
            scene.ecsWorld.query_builder<const Transform, const MeshRef>().build().each(
                [&](const Transform&, const MeshRef&) { meshRefCount++; }
            );
            scene.animQuery.each([&](const Transform&, const Animated&) { animatedCount++; });
            scene.instanceQuery.each([&](const Transform&, const InstanceGroup&) {
                instanceGroupCount++;
            });
            scene.lightQuery.each([&](const PointLight&) { pointLightCount++; });
            scene.ecsWorld.each([&](flecs::entity, const Pickable&) { pickableCount++; });
            ImGui::Text("Entities: %d", entityCount);
            ImGui::Text("  Transform+MeshRef: %d", meshRefCount);
            ImGui::Text("  Animated: %d", animatedCount);
            ImGui::Text("  InstanceGroup: %d", instanceGroupCount);
            ImGui::Text("  PointLight: %d", pointLightCount);
            ImGui::Text("  Pickable: %d", pickableCount);

            ImGui::Separator();
            ImGui::Text("Spawn Paused: %s", spawningStopped ? "Yes" : "No");
            ImGui::Text("Shadow: %s", shadow.enabled ? "On" : "Off");
            ImGui::Text("Cubemap: %s", cubemapEnabled ? "On" : "Off");
            ImGui::Text("SSAO: %s", ssao.enabled ? "On" : "Off");
            ImGui::Checkbox("Occlusion Culling", &occlusionCullingEnabled);
        }
        ImGui::End();
    }
}
