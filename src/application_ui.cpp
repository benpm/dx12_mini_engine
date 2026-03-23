module;

#if defined(__clang__)
    #define FMT_CONSTEVAL
#endif

#include <Windows.h>
#include <DirectXMath.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>
#include <algorithm>
#include <string>
#include <vector>
#include <flecs.h>
#include <spdlog/spdlog.h>
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx12.h>

module application;

using Microsoft::WRL::ComPtr;
using namespace DirectX;

// ---------------------------------------------------------------------------
// ImGui render (app-specific UI)
// ---------------------------------------------------------------------------

void Application::renderImGui(ComPtr<ID3D12GraphicsCommandList2> cmdList)
{
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    bool shadowPsoDirty = false;
    constexpr float radToDeg = 57.2957795f;
    constexpr float degToRad = 0.0174532925f;

    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("Display")) {
            ImGui::Checkbox("VSync", &vsync);
            bool fs = fullscreen;
            if (ImGui::Checkbox("Fullscreen", &fs)) {
                setFullscreen(fs);
            }
            ImGui::Separator();
            ImGui::Text("Tearing Supported: %s", tearingSupported ? "Yes" : "No");
            ImGui::Text("Test Mode: %s", testMode ? "On" : "Off");
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Camera")) {
            ImGui::PushItemWidth(220.0f);
            float fovDeg = cam.fov * radToDeg;
            if (ImGui::SliderFloat("FOV", &fovDeg, 20.0f, 120.0f, "%.1f deg")) {
                cam.fov = std::clamp(fovDeg * degToRad, 0.05f, 3.0f);
            }
            ImGui::SliderFloat("Near Plane", &cam.nearPlane, 0.01f, 10.0f, "%.3f");
            ImGui::SliderFloat("Far Plane", &cam.farPlane, 10.0f, 5000.0f, "%.1f");
            if (cam.farPlane <= cam.nearPlane + 0.01f) {
                cam.farPlane = cam.nearPlane + 0.01f;
            }

            ImGui::Separator();
            ImGui::SliderFloat("Orbit Radius", &cam.radius, 0.1f, 1000.0f, "%.2f");
            float yawDeg = cam.yaw * radToDeg;
            float pitchDeg = cam.pitch * radToDeg;
            if (ImGui::SliderFloat("Yaw", &yawDeg, -180.0f, 180.0f, "%.1f deg")) {
                cam.yaw = yawDeg * degToRad;
            }
            if (ImGui::SliderFloat("Pitch", &pitchDeg, -89.9f, 89.9f, "%.1f deg")) {
                cam.pitch = pitchDeg * degToRad;
            }
            ImGui::PopItemWidth();
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Bloom")) {
            ImGui::PushItemWidth(220.0f);
            ImGui::SliderFloat("Threshold", &bloomThreshold, 0.0f, 3.0f);
            ImGui::SliderFloat("Intensity", &bloomIntensity, 0.0f, 5.0f);
            ImGui::PopItemWidth();
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Tonemap")) {
            const char* tonemappers[] = { "ACES Filmic", "AgX", "AgX Punchy", "Gran Turismo",
                                          "PBR Neutral" };
            ImGui::PushItemWidth(180.0f);
            ImGui::Combo("##tonemap", &tonemapMode, tonemappers, IM_ARRAYSIZE(tonemappers));
            ImGui::PopItemWidth();
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Scene")) {
            ImGui::PushItemWidth(220.0f);
            ImGui::ColorEdit3("Background", bgColor);
            ImGui::Separator();
            ImGui::Text("Directional Light");
            ImGui::SliderFloat3("Direction", dirLightDir, -1.0f, 1.0f);
            ImGui::ColorEdit3("Dir Color", dirLightColor);
            ImGui::SliderFloat("Dir Brightness", &dirLightBrightness, 0.0f, 20.0f);
            ImGui::Separator();
            ImGui::Text("Point Lights");
            ImGui::SliderFloat("Point Brightness", &lightBrightness, 0.0f, 20.0f);
            ImGui::SliderFloat("Ambient Brightness", &ambientBrightness, 0.0f, 2.0f);
            ImGui::Separator();
            ImGui::Text("Height Fog");
            ImGui::SliderFloat("Fog Start Y", &fogStartY, -20.0f, 10.0f);
            ImGui::SliderFloat("Fog Density", &fogDensity, 0.0f, 1.0f, "%.3f");
            ImGui::ColorEdit3("Fog Color", fogColor);
            ImGui::PopItemWidth();
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Shadows")) {
            ImGui::PushItemWidth(220.0f);
            ImGui::Checkbox("Enabled", &shadowEnabled);
            ImGui::SliderFloat("Bias", &shadowBias, 0.0001f, 0.01f, "%.4f");

            shadowPsoDirty |=
                ImGui::SliderInt("Raster Depth Bias", &shadowRasterDepthBias, 0, 5000);
            shadowPsoDirty |=
                ImGui::SliderFloat("Raster Slope Bias", &shadowRasterSlopeBias, 0.0f, 8.0f, "%.3f");
            shadowPsoDirty |=
                ImGui::SliderFloat("Raster Bias Clamp", &shadowRasterBiasClamp, 0.0f, 5.0f, "%.3f");

            ImGui::Separator();
            ImGui::SliderFloat("Light Distance", &shadowLightDistance, 1.0f, 200.0f, "%.1f");
            ImGui::SliderFloat("Ortho Size", &shadowOrthoSize, 5.0f, 200.0f, "%.1f");
            ImGui::SliderFloat("Near", &shadowNearPlane, 0.001f, 10.0f, "%.3f");
            ImGui::SliderFloat("Far", &shadowFarPlane, 1.0f, 500.0f, "%.1f");
            if (shadowFarPlane <= shadowNearPlane + 0.001f) {
                shadowFarPlane = shadowNearPlane + 0.001f;
            }
            ImGui::PopItemWidth();
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Animation")) {
            ImGui::PushItemWidth(220.0f);
            ImGui::Checkbox("Animate Entities", &animateEntities);
            ImGui::SliderFloat("Light Animation Speed", &lightAnimationSpeed, 0.0f, 4.0f, "%.2f");
            ImGui::SliderFloat("Light Time", &lightTime, 0.0f, 600.0f, "%.1f");
            ImGui::PopItemWidth();
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Spawning")) {
            ImGui::PushItemWidth(220.0f);
            ImGui::Checkbox("Stop Spawning", &spawningStopped);
            ImGui::Checkbox("Auto Stop", &autoStopSpawning);
            ImGui::SliderFloat("Auto Stop ms", &spawnStopFrameMs, 1.0f, 50.0f, "%.1f");
            ImGui::SliderInt("Batch Size", &spawnBatchSize, 1, 500);
            if (ImGui::Button("Reset Perf Gate")) {
                spawningStopped = false;
                recentFrameHead = 0;
                recentFrameMs[0] = recentFrameMs[1] = recentFrameMs[2] = 0.0f;
            }
            ImGui::PopItemWidth();
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Lights")) {
            ImGui::PushItemWidth(220.0f);
            ImGui::Checkbox("Show Billboards", &showLightBillboards);
            ImGui::SliderFloat("Billboard Size", &billboards.spriteSize, 0.01f, 3.0f, "%.2f");
            ImGui::SliderFloat("Point Brightness", &lightBrightness, 0.0f, 20.0f);
            ImGui::Separator();
            for (int i = 0; i < SceneConstantBuffer::maxLights; ++i) {
                ImGui::PushID(i);
                if (ImGui::TreeNode("Light", "Light %d", i)) {
                    ImGui::SliderFloat3("Center", &lightAnims[i].center.x, -50.0f, 50.0f);
                    ImGui::SliderFloat("Amp X", &lightAnims[i].ampX, 0.0f, 20.0f);
                    ImGui::SliderFloat("Amp Y", &lightAnims[i].ampY, 0.0f, 20.0f);
                    ImGui::SliderFloat("Amp Z", &lightAnims[i].ampZ, 0.0f, 20.0f);
                    ImGui::SliderFloat("Freq X", &lightAnims[i].freqX, 0.0f, 3.0f);
                    ImGui::SliderFloat("Freq Y", &lightAnims[i].freqY, 0.0f, 3.0f);
                    ImGui::SliderFloat("Freq Z", &lightAnims[i].freqZ, 0.0f, 3.0f);
                    ImGui::ColorEdit3("Color", &lightAnims[i].color.x);
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
            ImGui::PopItemWidth();
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Material")) {
            ImGui::PushItemWidth(220.0f);
            if (!scene.materials.empty()) {
                if (scene.materials.size() > 1) {
                    std::vector<const char*> names;
                    names.reserve(scene.materials.size());
                    for (const auto& m : scene.materials) {
                        names.push_back(m.name.c_str());
                    }
                    ImGui::Combo(
                        "##matsel", &scene.selectedMaterialIdx, names.data(), (int)names.size()
                    );
                    ImGui::Separator();
                }
                Material& mat = scene.materials[std::clamp(
                    scene.selectedMaterialIdx, 0, (int)scene.materials.size() - 1
                )];
                ImGui::ColorEdit4("Albedo", &mat.albedo.x);
                ImGui::SliderFloat("Roughness", &mat.roughness, 0.0f, 1.0f);
                ImGui::SliderFloat("Metallic", &mat.metallic, 0.0f, 1.0f);
                ImGui::ColorEdit3("Emissive", &mat.emissive.x);
                ImGui::SliderFloat("Emissive Strength", &mat.emissiveStrength, 0.0f, 20.0f);
                ImGui::Checkbox("Reflective", &mat.reflective);
            }
            ImGui::PopItemWidth();
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Reflections")) {
            ImGui::PushItemWidth(220.0f);
            ImGui::Checkbox("Cubemap Enabled", &cubemapEnabled);
            int res = static_cast<int>(cubemapResolution);
            if (ImGui::SliderInt("Cubemap Resolution", &res, 32, 512)) {
                cubemapResolution = static_cast<uint32_t>(res);
                createCubemapResources();
            }
            ImGui::SliderFloat("Cubemap Near", &cubemapNearPlane, 0.001f, 5.0f, "%.3f");
            ImGui::SliderFloat("Cubemap Far", &cubemapFarPlane, 5.0f, 500.0f, "%.1f");
            if (cubemapFarPlane <= cubemapNearPlane + 0.001f) {
                cubemapFarPlane = cubemapNearPlane + 0.001f;
            }
            ImGui::PopItemWidth();
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Scene File")) {
            ImGui::PushItemWidth(300.0f);
            ImGui::InputText("##path", gltfPathBuf, sizeof(gltfPathBuf));
            ImGui::PopItemWidth();
            ImGui::SameLine();
            if (ImGui::MenuItem("Load")) {
                if (gltfPathBuf[0] != '\0') {
                    pendingGltfPath = gltfPathBuf;
                }
            }
            if (ImGui::MenuItem("Reset to Teapot")) {
                pendingResetToTeapot = true;
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Create")) {
            ImGui::PushItemWidth(220.0f);
            if (!scene.spawnableMeshNames.empty()) {
                std::vector<const char*> meshNames;
                meshNames.reserve(scene.spawnableMeshNames.size());
                for (const auto& n : scene.spawnableMeshNames) {
                    meshNames.push_back(n.c_str());
                }
                ImGui::Combo("Mesh", &createMeshIdx, meshNames.data(), (int)meshNames.size());
            }
            if (!scene.materials.empty()) {
                std::vector<const char*> matNames;
                matNames.reserve(scene.materials.size());
                for (const auto& m : scene.materials) {
                    matNames.push_back(m.name.empty() ? "Unnamed" : m.name.c_str());
                }
                ImGui::Combo("Material", &createMatIdx, matNames.data(), (int)matNames.size());
            }
            ImGui::DragFloat3("Position", createPos, 0.5f);
            ImGui::SliderFloat("Scale", &createScale, 0.01f, 10.0f, "%.2f");
            ImGui::Checkbox("Animated", &createAnimated);
            if (createAnimated) {
                ImGui::SliderFloat("Anim Speed", &createAnimSpeed, 0.0f, 5.0f);
                ImGui::SliderFloat("Orbit Radius", &createAnimRadius, 0.0f, 30.0f);
            }
            if (ImGui::Button("Spawn Entity")) {
                if (!scene.spawnableMeshRefs.empty()) {
                    int mi = std::clamp(createMeshIdx, 0, (int)scene.spawnableMeshRefs.size() - 1);
                    MeshRef mesh = scene.spawnableMeshRefs[mi];
                    mesh.materialIndex =
                        std::clamp(createMatIdx, 0, (int)scene.materials.size() - 1);
                    Transform tf;
                    tf.world = scale(createScale, createScale, createScale) *
                               translate(createPos[0], createPos[1], createPos[2]);
                    auto e = scene.ecsWorld.entity().set(tf).set(mesh).add<Pickable>();
                    if (createAnimated) {
                        Animated anim;
                        anim.speed = createAnimSpeed;
                        anim.orbitRadius = createAnimRadius;
                        anim.orbitY = createPos[1];
                        anim.initialScale = createScale;
                        anim.pulsePhase = static_cast<float>(scene.rng() % 1000) / 1000.0f * 6.28f;
                        e.set(anim);
                    }
                    selectedEntity = e;
                }
            }
            ImGui::PopItemWidth();
            ImGui::EndMenu();
        }

        ImGui::EndMainMenuBar();
    }

    if (shadowPsoDirty) {
        createShadowPSO();
    }

    if (ImGui::Begin(
            "Stats", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing |
                ImGuiWindowFlags_NoNav
        )) {
        ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
        ImGui::Text("Frame: %.2f ms", this->lastFrameMs);
        ImGui::Text("Objects: %u", this->lastFrameObjectCount);
        ImGui::Text("Vertices: %u", this->lastFrameVertexCount);
        ImGui::Text("Spawn Paused: %s", spawningStopped ? "Yes" : "No");
        ImGui::Text("Camera Radius: %.2f", cam.radius);
        ImGui::Text("Shadow: %s", shadowEnabled ? "On" : "Off");
        ImGui::Text("Cubemap: %s", cubemapEnabled ? "On" : "Off");
        ImGui::End();
    }

    // --- Entity Inspector ---
    if (selectedEntity.is_alive()) {
        ImGui::SetNextWindowSize(ImVec2(350, 0), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Entity Inspector", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
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
                ImGui::EndTabBar();
            }

            ImGui::Separator();
            if (!selectedEntity.has<Animated>()) {
                if (ImGui::Button("Add Animated")) {
                    Animated anim;
                    if (selectedEntity.has<Transform>()) {
                        auto tf = selectedEntity.get<Transform>();
                        anim.orbitY = tf.world._42;
                    }
                    selectedEntity.set(anim);
                }
                ImGui::SameLine();
            }
            if (!selectedEntity.has<Pickable>()) {
                if (ImGui::Button("Add Pickable")) {
                    selectedEntity.add<Pickable>();
                }
                ImGui::SameLine();
            }
            if (ImGui::Button("Deselect")) {
                selectedEntity = flecs::entity{};
            }
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.1f, 0.1f, 1.0f));
            if (ImGui::Button("Delete")) {
                selectedEntity.destruct();
                selectedEntity = flecs::entity{};
            }
            ImGui::PopStyleColor();
        }
        ImGui::End();
    }

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

    ImGui::Render();

    ID3D12DescriptorHeap* heaps[] = { imguiLayer.srvHeap.Get() };
    cmdList->SetDescriptorHeaps(1, heaps);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), cmdList.Get());
}
