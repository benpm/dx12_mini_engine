module;

#if defined(__clang__)
    #define FMT_CONSTEVAL
#endif

#include <d3d12.h>
#include <DirectXMath.h>
#include <dxgi1_6.h>
#include <flecs.h>
#include <spdlog/spdlog.h>
#include <Windows.h>
#include <wrl.h>
#include <cmath>
#include <string>
#include <vector>
#include "scene_data.h"

module application;

using Microsoft::WRL::ComPtr;
using namespace DirectX;

// ---------------------------------------------------------------------------
// Extract current Application state into a SceneFileData
// ---------------------------------------------------------------------------

SceneFileData Application::extractSceneData() const
{
    SceneFileData d;

    d.title = sceneTitle;
    d.description = sceneDescription;

    // Camera (aspectRatio is runtime state, not serialized)
    d.camera.fov = cam.fov;
    d.camera.nearPlane = cam.nearPlane;
    d.camera.farPlane = cam.farPlane;
    d.camera.yaw = cam.yaw;
    d.camera.pitch = cam.pitch;
    d.camera.radius = cam.radius;

    // Bloom
    d.bloom = { bloomThreshold, bloomIntensity, tonemapMode };

    // Directional light
    d.dirLight.dir = dirLightDir;
    d.dirLight.brightness = dirLightBrightness;
    d.dirLight.color = dirLightColor;

    // Fog
    d.fog.startY = fogStartY;
    d.fog.density = fogDensity;
    d.fog.color = fogColor;

    // Shadows
    d.shadow = { shadow.enabled,         shadow.bias,
                 shadow.rasterDepthBias, shadow.rasterSlopeBias,
                 shadow.rasterBiasClamp, shadow.lightDistance,
                 shadow.orthoSize,       shadow.nearPlane,
                 shadow.farPlane };

    // Cubemap
    d.cubemap = { cubemapEnabled, cubemapResolution, cubemapNearPlane, cubemapFarPlane };

    // Point lights
    d.pointLights.brightness = lightBrightness;
    d.pointLights.ambient = ambientBrightness;
    int li = 0;
    scene.lightQuery.each([&](const PointLight& pl) {
        if (li < static_cast<int>(d.pointLights.lights.size())) {
            d.pointLights.lights[li++] = pl;
        }
    });

    // Spawning
    d.spawning = { spawningStopped, autoStopSpawning, spawnStopFrameMs, spawnBatchSize };

    // Display
    d.display = { vsync, animateEntities, lightAnimationSpeed, showLightBillboards, showGrid };

    // Background
    d.bgColor = bgColor;

    // Runtime
    d.runtime = runtimeConfig;

    // Materials
    d.materials = scene.materials;

    // Entities
    scene.drawQuery.each([&](flecs::entity e, const Transform& tf, const MeshRef& mr) {
        EntityData ed;

        for (size_t i = 0; i < scene.spawnableMeshRefs.size(); ++i) {
            if (scene.spawnableMeshRefs[i].vertexOffset == mr.vertexOffset &&
                scene.spawnableMeshRefs[i].indexOffset == mr.indexOffset) {
                ed.meshName = scene.spawnableMeshNames[i];
                break;
            }
        }

        if (mr.materialIndex >= 0 && mr.materialIndex < static_cast<int>(scene.materials.size())) {
            ed.materialName = scene.materials[mr.materialIndex].name;
        }

        ed.position = { tf.world._41, tf.world._42, tf.world._43 };
        ed.scale = std::sqrt(
            tf.world._11 * tf.world._11 + tf.world._12 * tf.world._12 + tf.world._13 * tf.world._13
        );
        ed.albedoOverride = mr.albedoOverride;
        ed.pickable = e.has<Pickable>();

        if (e.has<Animated>()) {
            ed.animated = e.get<Animated>();
        }

        d.entities.push_back(ed);
    });

    // Instance groups
    scene.instanceQuery.each([&](flecs::entity /*e*/, const Transform& /*tf*/,
                                 const InstanceGroup& group) {
        InstanceGroupData igd;
        for (size_t i = 0; i < scene.spawnableMeshRefs.size(); ++i) {
            if (scene.spawnableMeshRefs[i].vertexOffset == group.mesh.vertexOffset &&
                scene.spawnableMeshRefs[i].indexOffset == group.mesh.indexOffset) {
                igd.meshName = scene.spawnableMeshNames[i];
                break;
            }
        }
        if (group.mesh.materialIndex >= 0 &&
            group.mesh.materialIndex < static_cast<int>(scene.materials.size())) {
            igd.materialName = scene.materials[group.mesh.materialIndex].name;
        }
        for (const auto& t : group.transforms) {
            igd.positions.push_back({ t._41, t._42, t._43 });
            igd.scales.push_back(std::sqrt(t._11 * t._11 + t._12 * t._12 + t._13 * t._13));
        }
        igd.albedoOverrides = group.albedoOverrides;
        d.instanceGroups.push_back(igd);
    });

    return d;
}

// ---------------------------------------------------------------------------
// Apply a SceneFileData to the Application state
// ---------------------------------------------------------------------------

void Application::applySceneData(const SceneFileData& d)
{
    sceneTitle = d.title;
    sceneDescription = d.description;
    snprintf(sceneTitleBuf, sizeof(sceneTitleBuf), "%s", sceneTitle.c_str());
    snprintf(sceneDescBuf, sizeof(sceneDescBuf), "%s", sceneDescription.c_str());

    // Camera (don't copy aspectRatio — set by onResize)
    cam.fov = d.camera.fov;
    cam.nearPlane = d.camera.nearPlane;
    cam.farPlane = d.camera.farPlane;
    cam.yaw = d.camera.yaw;
    cam.pitch = d.camera.pitch;
    cam.radius = d.camera.radius;

    // Bloom
    bloomThreshold = d.bloom.threshold;
    bloomIntensity = d.bloom.intensity;
    tonemapMode = d.bloom.tonemapMode;

    // Directional light
    dirLightDir = d.dirLight.dir;
    dirLightBrightness = d.dirLight.brightness;
    dirLightColor = d.dirLight.color;

    // Fog
    fogStartY = d.fog.startY;
    fogDensity = d.fog.density;
    fogColor = d.fog.color;

    // Shadows
    bool shadowRasterChanged =
        (shadow.rasterDepthBias != d.shadow.rasterDepthBias ||
         shadow.rasterSlopeBias != d.shadow.rasterSlopeBias ||
         shadow.rasterBiasClamp != d.shadow.rasterBiasClamp);
    shadow.enabled = d.shadow.enabled;
    shadow.bias = d.shadow.bias;
    shadow.rasterDepthBias = d.shadow.rasterDepthBias;
    shadow.rasterSlopeBias = d.shadow.rasterSlopeBias;
    shadow.rasterBiasClamp = d.shadow.rasterBiasClamp;
    shadow.lightDistance = d.shadow.lightDistance;
    shadow.orthoSize = d.shadow.orthoSize;
    shadow.nearPlane = d.shadow.nearPlane;
    shadow.farPlane = d.shadow.farPlane;

    // Cubemap
    bool cubemapResChanged = (cubemapResolution != d.cubemap.resolution);
    cubemapEnabled = d.cubemap.enabled;
    cubemapResolution = d.cubemap.resolution;
    cubemapNearPlane = d.cubemap.nearPlane;
    cubemapFarPlane = d.cubemap.farPlane;

    // Point lights
    lightBrightness = d.pointLights.brightness;
    ambientBrightness = d.pointLights.ambient;
    {
        int i = 0;
        scene.lightQuery.each([&](PointLight& pl) {
            if (i < static_cast<int>(d.pointLights.lights.size())) {
                pl = d.pointLights.lights[i++];
            }
        });
    }

    // Spawning
    spawningStopped = d.spawning.stopped;
    autoStopSpawning = d.spawning.autoStop;
    spawnStopFrameMs = d.spawning.stopFrameMs;
    spawnBatchSize = d.spawning.batchSize;

    // Display
    vsync = d.display.vsync;
    animateEntities = d.display.animateEntities;
    lightAnimationSpeed = d.display.lightAnimSpeed;
    showLightBillboards = d.display.showBillboards;
    showGrid = d.display.showGrid;

    // Background
    bgColor = d.bgColor;

    // Terrain position
    if (contentLoaded) {
        scene.ecsWorld.query_builder<Transform>().with<TerrainEntity>().build().each(
            [&](Transform& tf) { tf.world = translate(0.0f, d.terrain.positionY, 0.0f); }
        );
    }

    // Runtime
    runtimeConfig = d.runtime;

    if (contentLoaded && runtimeConfig.singleTeapotMode) {
        scene.clearScene(cmdQueue);
        scene.loadTeapot(device.Get(), cmdQueue, false);
        spawningStopped = true;
        autoStopSpawning = false;
    }

    // Rebuild GPU resources if needed
    if (contentLoaded) {
        if (shadowRasterChanged) {
            auto vsData = shaderCompiler.data(sceneVSIdx);
            D3D12_SHADER_BYTECODE vs =
                vsData ? D3D12_SHADER_BYTECODE{ vsData, shaderCompiler.size(sceneVSIdx) }
                       : D3D12_SHADER_BYTECODE{};
            shadow.reloadPSO(device.Get(), rootSignature.Get(), vs);
        }
        if (cubemapResChanged) {
            createCubemapResources();
        }
    }

    // Apply materials from scene file
    if (!d.materials.empty()) {
        scene.materials = d.materials;
        for (int i = 0; i < static_cast<int>(MaterialPreset::Count); ++i) {
            scene.presetIdx[i] = -1;
        }
        for (int i = 0; i < static_cast<int>(scene.materials.size()); ++i) {
            const auto& n = scene.materials[i].name;
            if (n == "Diffuse") {
                scene.presetIdx[static_cast<int>(MaterialPreset::Diffuse)] = i;
            } else if (n == "Metal") {
                scene.presetIdx[static_cast<int>(MaterialPreset::Metal)] = i;
            } else if (n == "Mirror") {
                scene.presetIdx[static_cast<int>(MaterialPreset::Mirror)] = i;
            }
        }
    }

    // Spawn entities from scene file
    if (!d.entities.empty() && contentLoaded) {
        for (const auto& ed : d.entities) {
            int meshIdx = -1;
            for (size_t i = 0; i < scene.spawnableMeshNames.size(); ++i) {
                if (scene.spawnableMeshNames[i] == ed.meshName) {
                    meshIdx = static_cast<int>(i);
                    break;
                }
            }
            if (meshIdx < 0) {
                spdlog::warn("Scene entity references unknown mesh '{}', skipping", ed.meshName);
                continue;
            }

            MeshRef mesh = scene.spawnableMeshRefs[meshIdx];
            for (int i = 0; i < static_cast<int>(scene.materials.size()); ++i) {
                if (scene.materials[i].name == ed.materialName) {
                    mesh.materialIndex = i;
                    break;
                }
            }
            mesh.albedoOverride = ed.albedoOverride;

            mat4 world =
                scale(ed.scale) * rotateAxis(ed.rotAxis, ed.rotAngle) * translate(ed.position);

            Transform tf;
            tf.world = world;
            auto entity = scene.ecsWorld.entity().set(tf).set(mesh);

            if (ed.animated) {
                Animated anim = *ed.animated;
                anim.orbitAngle = std::atan2(ed.position.z, ed.position.x);
                entity.set(anim);
            }

            if (ed.pickable) {
                entity.add<Pickable>();
            }
        }
        spdlog::info("Spawned {} entities from scene file", d.entities.size());
    }

    // Spawn instance groups from scene file
    if (!d.instanceGroups.empty() && contentLoaded) {
        for (const auto& igd : d.instanceGroups) {
            int meshIdx = -1;
            for (size_t i = 0; i < scene.spawnableMeshNames.size(); ++i) {
                if (scene.spawnableMeshNames[i] == igd.meshName) {
                    meshIdx = static_cast<int>(i);
                    break;
                }
            }
            if (meshIdx < 0) {
                spdlog::warn("Instance group references unknown mesh '{}', skipping", igd.meshName);
                continue;
            }

            InstanceGroup group;
            group.mesh = scene.spawnableMeshRefs[meshIdx];
            for (int i = 0; i < static_cast<int>(scene.materials.size()); ++i) {
                if (scene.materials[i].name == igd.materialName) {
                    group.mesh.materialIndex = i;
                    break;
                }
            }
            for (size_t i = 0; i < igd.positions.size(); ++i) {
                float s = (i < igd.scales.size()) ? igd.scales[i] : 1.0f;
                group.transforms.push_back(scale(s, s, s) * translate(igd.positions[i]));
            }
            group.albedoOverrides = igd.albedoOverrides;

            Transform tf;
            tf.world = mat4{};
            scene.ecsWorld.entity().set(tf).set(std::move(group));
        }
        spdlog::info("Spawned {} instance groups from scene file", d.instanceGroups.size());
    }
}
