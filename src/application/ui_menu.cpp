module;

#include <flecs.h>
#include <imgui.h>
#include <algorithm>
#include <string>
#include <vector>

module application;

void Application::uiMenuBar(bool& shadowPsoDirty)
{
    constexpr float radToDeg = 57.2957795f;
    constexpr float degToRad = 0.0174532925f;

    if (ImGui::BeginMainMenuBar()) {
        // --- File Menu ---
        if (ImGui::BeginMenu(iconLabel("menu.File", "File").c_str())) {
            const std::vector<std::pair<std::string, std::string>> sceneFilters = {
                { "Scene Files", "*.json" }
            };
            const std::vector<std::pair<std::string, std::string>> gltfFilters = {
                { "glTF Files", "*.glb;*.gltf" }
            };
            if (ImGui::MenuItem(iconLabel("action.LoadScene", "Load Scene...").c_str())) {
                std::string path =
                    openNativeFileDialog(FileDialogType::Open, "Open Scene", sceneFilters, "json");
                if (!path.empty()) {
                    pendingSceneLoad = std::move(path);
                }
            }
            if (ImGui::MenuItem(iconLabel("action.SaveScene", "Save Scene...").c_str())) {
                std::string path =
                    openNativeFileDialog(FileDialogType::Save, "Save Scene", sceneFilters, "json");
                if (!path.empty()) {
                    pendingSceneSave = std::move(path);
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem(iconLabel("action.LoadGLB", "Load GLB...").c_str())) {
                std::string path =
                    openNativeFileDialog(FileDialogType::Open, "Open glTF", gltfFilters, "glb");
                if (!path.empty()) {
                    pendingGltfPath = std::move(path);
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
                if (ImGui::Button(iconLabel("action.Run", "Run Script...").c_str())) {
                    std::vector<std::pair<std::string, std::string>> filters = {
                        { "Lua Scripts", "*.lua" }
                    };
                    std::string path =
                        openNativeFileDialog(FileDialogType::Open, "Run Script", filters, "lua");
                    if (!path.empty()) {
                        luaScripting.executeScript(path.c_str());
                    }
                }
                ImGui::PopItemWidth();
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }

        ImGui::EndMainMenuBar();
    }
}
