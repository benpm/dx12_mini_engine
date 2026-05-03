module;

#include <flecs.h>
#include <imgui.h>
#include <algorithm>
#include <string>
#include <vector>

module application;

void Application::uiInspector()
{
    if (selectedEntity.is_alive()) {
        ImGui::SetNextWindowSize(ImVec2(350, 0), ImGuiCond_FirstUseEver);
        if (ImGui::Begin(
                iconLabel("window.EntityInspector", "Entity Inspector").c_str(), nullptr,
                ImGuiWindowFlags_AlwaysAutoResize
            )) {
            ImGui::Text("Entity ID: %llu", selectedEntity.id());

            if (ImGui::BeginTabBar("Components")) {
                if (selectedEntity.has<Transform>()) {
                    if (ImGui::BeginTabItem("Transform")) {
                        auto tf = selectedEntity.get_mut<Transform>();
                        auto& m = tf.world;
                        ImGui::Text("Position: %.2f, %.2f, %.2f", m._41, m._42, m._43);
                        float pos[3] = { m._41, m._42, m._43 };
                        if (ImGui::DragFloat3("Pos", pos, 0.1f)) {
                            m._41 = pos[0];
                            m._42 = pos[1];
                            m._43 = pos[2];
                        }
                        ImGui::EndTabItem();
                    }
                }
                if (selectedEntity.has<MeshRef>()) {
                    if (ImGui::BeginTabItem("MeshRef")) {
                        auto mr = selectedEntity.get_mut<MeshRef>();
                        ImGui::Text("Vertices: %u (offset %u)", mr.indexCount, mr.vertexOffset);
                        ImGui::Text("Indices: %u (offset %u)", mr.indexCount, mr.indexOffset);
                        ImGui::Text("Material: %d", mr.materialIndex);
                        if (mr.materialIndex >= 0 &&
                            mr.materialIndex < static_cast<int>(scene.materials.size())) {
                            Material& mat = scene.materials[mr.materialIndex];
                            float alb[4] = { mat.albedo.x, mat.albedo.y, mat.albedo.z,
                                             mat.albedo.w };
                            if (ImGui::ColorEdit3("Albedo", alb)) {
                                mat.albedo = { alb[0], alb[1], alb[2], alb[3] };
                            }
                            ImGui::SliderFloat("Roughness", &mat.roughness, 0.0f, 1.0f);
                            ImGui::SliderFloat("Metallic", &mat.metallic, 0.0f, 1.0f);
                            float em[3] = { mat.emissive.x, mat.emissive.y, mat.emissive.z };
                            if (ImGui::ColorEdit3("Emissive", em)) {
                                mat.emissive = { em[0], em[1], em[2], mat.emissive.w };
                            }
                            ImGui::SliderFloat("EmissiveStr", &mat.emissiveStrength, 0.0f, 10.0f);
                            ImGui::Checkbox("Reflective", &mat.reflective);
                        }
                        float ov[4] = { mr.albedoOverride.x, mr.albedoOverride.y,
                                        mr.albedoOverride.z, mr.albedoOverride.w };
                        if (ImGui::ColorEdit4("Albedo Override", ov)) {
                            mr.albedoOverride = { ov[0], ov[1], ov[2], ov[3] };
                        }
                        ImGui::EndTabItem();
                    }
                }
                if (selectedEntity.has<Animated>()) {
                    if (ImGui::BeginTabItem("Animated")) {
                        auto a = selectedEntity.get_mut<Animated>();
                        ImGui::SliderFloat("Speed", &a.speed, 0.0f, 5.0f);
                        ImGui::DragFloat("Orbit Radius", &a.orbitRadius, 0.1f);
                        ImGui::DragFloat("Orbit Y", &a.orbitY, 0.1f);
                        ImGui::SliderFloat("Scale", &a.initialScale, 0.1f, 5.0f);
                        ImGui::EndTabItem();
                    }
                }
                if (selectedEntity.has<Pickable>()) {
                    if (ImGui::BeginTabItem("Pickable")) {
                        ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "Entity is pickable");
                        if (ImGui::Button("Remove Pickable")) {
                            selectedEntity.remove<Pickable>();
                        }
                        ImGui::EndTabItem();
                    }
                }
                if (selectedEntity.has<Scripted>()) {
                    if (ImGui::BeginTabItem("Scripted")) {
                        auto s = selectedEntity.get<Scripted>();
                        ImGui::Text("Script: %s", s.scriptPath.c_str());
                        ImGui::Text("Lua Ref: %d", s.luaRef);
                        if (ImGui::Button("Detach Script")) {
                            luaScripting.detachScript(selectedEntity);
                        }
                        ImGui::EndTabItem();
                    }
                }
                ImGui::EndTabBar();
            }

            ImGui::Separator();
            if (!selectedEntity.has<Animated>() && !selectedEntity.has<InstanceAnimation>() &&
                !selectedEntity.has<InstanceGroup>()) {
                if (ImGui::Button(iconLabel("action.AddAnimated", "Add Animated").c_str())) {
                    Animated anim{};
                    if (selectedEntity.has<Transform>()) {
                        auto tf = selectedEntity.get<Transform>();
                        anim.orbitY = tf.world._42;
                    }
                    pendingAddAnimated = anim;
                }
                ImGui::SameLine();
            }
            if (!selectedEntity.has<Pickable>()) {
                if (ImGui::Button(iconLabel("action.AddPickable", "Add Pickable").c_str())) {
                    pendingAddPickable = true;
                }
                ImGui::SameLine();
            }
            if (!selectedEntity.has<Scripted>()) {
                if (ImGui::Button(iconLabel("action.AttachScript", "Attach Script...").c_str())) {
                    std::vector<std::pair<std::string, std::string>> filters = {
                        { "Lua Scripts", "*.lua" }
                    };
                    std::string path = openNativeFileDialog(
                        FileDialogType::Open, "Attach Script", filters, "lua"
                    );
                    if (!path.empty()) {
                        luaScripting.attachScript(selectedEntity, path.c_str());
                    }
                }
            }
            {
                auto sc = hotkeys.shortcutString(EditorAction::Deselect);
                auto ico = iconLabel("action.Deselect", "");
                std::string label = ico + (sc.empty() ? "Deselect" : "Deselect (" + sc + ")");
                if (ImGui::Button(label.c_str())) {
                    selectedEntity = flecs::entity{};
                }
            }
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.1f, 0.1f, 1.0f));
            {
                auto sc = hotkeys.shortcutString(EditorAction::DeleteEntity);
                auto ico = iconLabel("action.Delete", "");
                std::string label = ico + (sc.empty() ? "Delete" : "Delete (" + sc + ")");
                if (ImGui::Button(label.c_str())) {
                    pendingDeleteSelected = true;
                }
            }
            ImGui::PopStyleColor();
        }
        ImGui::End();
    }
}
