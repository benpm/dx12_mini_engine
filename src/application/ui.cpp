module;

#if defined(__clang__)
    #define FMT_CONSTEVAL
#endif

#include <d3d12.h>
#include <DirectXMath.h>
#include <dxgi1_6.h>
#include <flecs.h>
#include <imgui.h>
#include <imgui_impl_dx12.h>
#include <imgui_impl_win32.h>
#include <spdlog/spdlog.h>
#include <Windows.h>
#include <wrl.h>
#include <algorithm>
#include <string>
#include <vector>

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
        // --- File Menu ---
        if (ImGui::BeginMenu(iconLabel("menu.File", "File").c_str())) {
            ImGui::PushItemWidth(300.0f);
            ImGui::InputText("##scenePath", scenePathBuf, sizeof(scenePathBuf));
            ImGui::PopItemWidth();
            ImGui::SameLine();
            if (ImGui::MenuItem(iconLabel("action.LoadScene", "Load Scene").c_str())) {
                if (scenePathBuf[0] != '\0') {
                    pendingSceneLoad = scenePathBuf;
                }
            }
            if (ImGui::MenuItem(iconLabel("action.SaveScene", "Save Scene").c_str())) {
                if (scenePathBuf[0] != '\0') {
                    pendingSceneSave = scenePathBuf;
                }
            }
            ImGui::Separator();
            ImGui::PushItemWidth(300.0f);
            ImGui::InputText("##gltfPath", gltfPathBuf, sizeof(gltfPathBuf));
            ImGui::PopItemWidth();
            ImGui::SameLine();
            if (ImGui::MenuItem(iconLabel("action.LoadGLB", "Load GLB").c_str())) {
                if (gltfPathBuf[0] != '\0') {
                    pendingGltfPath = gltfPathBuf;
                }
            }
            if (ImGui::MenuItem(iconLabel("action.ResetToTeapot", "Reset to Teapot").c_str())) {
                pendingResetToTeapot = true;
            }
            ImGui::Separator();
            ImGui::PushItemWidth(300.0f);
            if (ImGui::InputText("Title##sceneTitle", sceneTitleBuf, sizeof(sceneTitleBuf))) {
                sceneTitle = sceneTitleBuf;
            }
            if (ImGui::InputText("Description##sceneDesc", sceneDescBuf, sizeof(sceneDescBuf))) {
                sceneDescription = sceneDescBuf;
            }
            ImGui::PopItemWidth();
            ImGui::EndMenu();
        }

        // --- Edit Menu ---
        if (ImGui::BeginMenu(iconLabel("menu.Edit", "Edit").c_str())) {
            if (ImGui::BeginMenu(iconLabel("menu.Create", "Create").c_str())) {
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
                ImGui::DragFloat3("Position", &createPos.x, 0.5f);
                ImGui::SliderFloat("Scale", &createScale, 0.01f, 10.0f, "%.2f");
                ImGui::Checkbox("Animated", &createAnimated);
                if (createAnimated) {
                    ImGui::SliderFloat("Anim Speed", &createAnimSpeed, 0.0f, 5.0f);
                    ImGui::SliderFloat("Orbit Radius", &createAnimRadius, 0.0f, 30.0f);
                }
                if (ImGui::Button(iconLabel("action.SpawnEntity", "Spawn Entity").c_str())) {
                    pendingCreateEntity = true;
                }
                ImGui::PopItemWidth();
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu(iconLabel("menu.Material", "Material").c_str())) {
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
            ImGui::EndMenu();
        }

        // --- View Menu ---
        if (ImGui::BeginMenu(iconLabel("menu.View", "View").c_str())) {
            if (ImGui::BeginMenu(iconLabel("menu.Display", "Display").c_str())) {
                ImGui::PushItemWidth(220.0f);
                ImGui::Checkbox("VSync", &vsync);
                ImGui::Checkbox("Grid", &showGrid);
                if (showGrid) {
                    ImGui::SliderFloat("Major Grid Size", &gridMajorSize, 1.0f, 200.0f, "%.1f m");
                    gridMajorSize = std::max(0.1f, gridMajorSize);
                    ImGui::SliderInt("Grid Subdivisions", &gridSubdivisions, 1, 128);
                }
                bool fs = fullscreen;
                if (ImGui::Checkbox("Fullscreen", &fs)) {
                    pendingFullscreenValue = fs;
                    pendingFullscreenChange = true;
                }
                {
                    auto sc = hotkeys.shortcutString(EditorAction::ToggleFullscreen);
                    if (!sc.empty()) {
                        ImGui::SameLine();
                        ImGui::TextDisabled("(%s)", sc.c_str());
                    }
                }
                ImGui::Separator();
                ImGui::Text("Tearing Supported: %s", tearingSupported ? "Yes" : "No");
                ImGui::Text(
                    "Runtime: %s",
                    (runtimeConfig.spawnPerFrame > 0 || runtimeConfig.screenshotFrame > 0)
                        ? "Scene"
                        : "Default"
                );
                ImGui::PopItemWidth();
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu(iconLabel("menu.Camera", "Camera").c_str())) {
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

            ImGui::Checkbox("Metrics Panel", &showMetrics);
            ImGui::EndMenu();
        }

        // --- Render Menu ---
        if (ImGui::BeginMenu(iconLabel("menu.Render", "Render").c_str())) {
            if (ImGui::BeginMenu(iconLabel("menu.Tonemap", "Tonemap").c_str())) {
                const char* tonemappers[] = { "ACES Filmic", "AgX", "AgX Punchy", "Gran Turismo",
                                              "PBR Neutral" };
                ImGui::PushItemWidth(180.0f);
                ImGui::Combo("##tonemap", &tonemapMode, tonemappers, IM_ARRAYSIZE(tonemappers));
                ImGui::PopItemWidth();
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu(iconLabel("menu.Shadows", "Shadows").c_str())) {
                ImGui::PushItemWidth(220.0f);
                ImGui::Checkbox("Enabled", &shadow.enabled);
                ImGui::SliderFloat("Bias", &shadow.bias, 0.0001f, 0.01f, "%.4f");

                shadowPsoDirty |=
                    ImGui::SliderInt("Raster Depth Bias", &shadow.rasterDepthBias, 0, 5000);
                shadowPsoDirty |= ImGui::SliderFloat(
                    "Raster Slope Bias", &shadow.rasterSlopeBias, 0.0f, 8.0f, "%.3f"
                );
                shadowPsoDirty |= ImGui::SliderFloat(
                    "Raster Bias Clamp", &shadow.rasterBiasClamp, 0.0f, 5.0f, "%.3f"
                );

                ImGui::Separator();
                ImGui::SliderFloat("Light Distance", &shadow.lightDistance, 1.0f, 200.0f, "%.1f");
                ImGui::SliderFloat("Ortho Size", &shadow.orthoSize, 5.0f, 200.0f, "%.1f");
                ImGui::SliderFloat("Near", &shadow.nearPlane, 0.001f, 10.0f, "%.3f");
                ImGui::SliderFloat("Far", &shadow.farPlane, 1.0f, 500.0f, "%.1f");
                if (shadow.farPlane <= shadow.nearPlane + 0.001f) {
                    shadow.farPlane = shadow.nearPlane + 0.001f;
                }
                ImGui::PopItemWidth();
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu(iconLabel("menu.Reflections", "Reflections").c_str())) {
                ImGui::PushItemWidth(220.0f);
                ImGui::Checkbox("Cubemap Enabled", &cubemapEnabled);
                int res = static_cast<int>(cubemapResolution);
                if (ImGui::SliderInt("Cubemap Resolution", &res, 32, 512)) {
                    cubemapResolution = static_cast<uint32_t>(res);
                    createCubemapResources();
                }
                ImGui::SliderFloat("Cubemap Near", &cubemapNearPlane, 0.001f, 5.0f, "%.3f");
                ImGui::SliderFloat("Cubemap Near", &cubemapNearPlane, 0.001f, 5.0f, "%.3f");
                ImGui::SliderFloat("Cubemap Far", &cubemapFarPlane, 5.0f, 500.0f, "%.1f");
                if (cubemapFarPlane <= cubemapNearPlane + 0.001f) {
                    cubemapFarPlane = cubemapNearPlane + 0.001f;
                }
                ImGui::PopItemWidth();
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu(iconLabel("menu.Bloom", "Bloom").c_str())) {
                ImGui::PushItemWidth(220.0f);
                ImGui::SliderFloat("Threshold", &bloomThreshold, 0.0f, 3.0f);
                ImGui::SliderFloat("Intensity", &bloomIntensity, 0.0f, 5.0f);
                ImGui::PopItemWidth();
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu(iconLabel("menu.SSAO", "SSAO").c_str())) {
                ImGui::PushItemWidth(220.0f);
                ImGui::Checkbox("Enabled", &ssao.enabled);
                ImGui::SliderFloat("Radius", &ssao.radius, 0.01f, 2.0f, "%.3f");
                ImGui::SliderFloat("Bias", &ssao.bias, 0.001f, 0.1f, "%.4f");
                ImGui::SliderInt("Kernel Size", &ssao.kernelSize, 4, 32);
                ssao.kernelSize = std::clamp(ssao.kernelSize, 4, 32);
                ImGui::PopItemWidth();
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }

        // --- World Menu ---
        if (ImGui::BeginMenu(iconLabel("menu.World", "World").c_str())) {
            if (ImGui::BeginMenu(iconLabel("menu.Scene", "Environment").c_str())) {
                ImGui::PushItemWidth(220.0f);
                ImGui::ColorEdit3("Background", &bgColor.x);
                ImGui::Separator();
                ImGui::Text("Directional Light");
                ImGui::SliderFloat3("Direction", &dirLightDir.x, -1.0f, 1.0f);
                ImGui::ColorEdit3("Dir Color", &dirLightColor.x);
                ImGui::SliderFloat("Dir Brightness", &dirLightBrightness, 0.0f, 20.0f);
                ImGui::Separator();
                ImGui::SliderFloat("Ambient Brightness", &ambientBrightness, 0.0f, 2.0f);
                ImGui::Separator();
                ImGui::Text("Height Fog");
                ImGui::SliderFloat("Fog Start Y", &fogStartY, -20.0f, 10.0f);
                ImGui::SliderFloat("Fog Density", &fogDensity, 0.0f, 1.0f, "%.3f");
                ImGui::ColorEdit3("Fog Color", &fogColor.x);
                ImGui::PopItemWidth();
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu(iconLabel("menu.Lights", "Lights").c_str())) {
                ImGui::PushItemWidth(220.0f);
                ImGui::Checkbox("Show Billboards", &showLightBillboards);
                ImGui::SliderFloat("Billboard Size", &billboards.spriteSize, 0.01f, 3.0f, "%.2f");
                ImGui::SliderFloat("Point Brightness", &lightBrightness, 0.0f, 20.0f);
                ImGui::Separator();
                int li = 0;
                scene.lightQuery.each([&](flecs::entity e, PointLight& pl) {
                    ImGui::PushID((int)e.id());
                    if (ImGui::TreeNode("Light", "Light %d", li)) {
                        ImGui::SliderFloat3("Center", &pl.center.x, -50.0f, 50.0f);
                        ImGui::SliderFloat3("Amplitude", &pl.amp.x, 0.0f, 20.0f);
                        ImGui::SliderFloat3("Frequency", &pl.freq.x, 0.0f, 3.0f);
                        ImGui::ColorEdit3("Color", &pl.color.x);
                        ImGui::TreePop();
                    }
                    ImGui::PopID();
                    li++;
                });
                ImGui::PopItemWidth();
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu(iconLabel("menu.Animation", "Animation").c_str())) {
                ImGui::PushItemWidth(220.0f);
                ImGui::Checkbox("Animate Entities", &animateEntities);
                ImGui::SliderFloat(
                    "Light Animation Speed", &lightAnimationSpeed, 0.0f, 4.0f, "%.2f"
                );
                ImGui::SliderFloat("Light Time", &lightTime, 0.0f, 600.0f, "%.1f");
                ImGui::PopItemWidth();
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu(iconLabel("menu.Spawning", "Spawning").c_str())) {
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
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu(iconLabel("menu.Scripts", "Tools").c_str())) {
            if (ImGui::BeginMenu(iconLabel("menu.Scripts", "Scripts").c_str())) {
                ImGui::PushItemWidth(220.0f);
                const auto& bindings = luaScripting.getActionBindings();
                if (bindings.empty()) {
                    ImGui::TextDisabled("No action bindings loaded");
                } else {
                    for (const auto& b : bindings) {
                        if (ImGui::MenuItem(b.actionName.c_str())) {
                            luaScripting.executeAction(b.actionName);
                        }
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("%s", b.scriptPath.c_str());
                        }
                    }
                }
                ImGui::Separator();
                static char oneOffScriptBuf[256] = "";
                ImGui::InputText("Script Path", oneOffScriptBuf, sizeof(oneOffScriptBuf));
                ImGui::SameLine();
                if (ImGui::Button(iconLabel("action.Run", "Run").c_str())) {
                    luaScripting.executeScript(oneOffScriptBuf);
                }
                ImGui::PopItemWidth();
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }

        ImGui::EndMainMenuBar();
    }

    if (shadowPsoDirty) {
        auto vsData = shaderCompiler.data(sceneVSIdx);
        gfx::ShaderBytecode vs =
            vsData ? gfx::ShaderBytecode{ vsData, shaderCompiler.size(sceneVSIdx) }
                   : gfx::ShaderBytecode{};
        shadow.reloadPSO(*gfxDevice, rootSignature.Get(), vs);
    }

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

            ImGui::Separator();
            // Entity & component counts from ECS
            int entityCount = 0;
            int transformCount = 0;
            int meshRefCount = 0;
            int animatedCount = 0;
            int instanceGroupCount = 0;
            int pointLightCount = 0;
            int pickableCount = 0;
            scene.ecsWorld.each([&](flecs::entity) { entityCount++; });
            scene.ecsWorld.query_builder<const Transform, const MeshRef>().build().each(
                [&](const Transform&, const MeshRef&) {
                    transformCount++;
                    meshRefCount++;
                }
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
        }
        ImGui::End();
    }

    // --- Entity Inspector ---
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
                static char scriptPathBuf[256] = "color_cycle.lua";
                ImGui::InputText("Script", scriptPathBuf, sizeof(scriptPathBuf));
                ImGui::SameLine();
                if (ImGui::Button(iconLabel("action.AttachScript", "Attach Script").c_str())) {
                    luaScripting.attachScript(selectedEntity, scriptPathBuf);
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

    ImGui::Render();

    ID3D12DescriptorHeap* heaps[] = { imguiLayer.srvHeap.Get() };
    cmdList->SetDescriptorHeaps(1, heaps);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), cmdList.Get());
}
