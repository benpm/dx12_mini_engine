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

    // Camera
    d.camera.fov = cam.fov;
    d.camera.nearPlane = cam.nearPlane;
    d.camera.farPlane = cam.farPlane;
    d.camera.yaw = cam.yaw;
    d.camera.pitch = cam.pitch;
    d.camera.radius = cam.radius;

    // Bloom
    d.bloom.threshold = bloomThreshold;
    d.bloom.intensity = bloomIntensity;
    d.bloom.tonemapMode = tonemapMode;

    // Directional light
    d.dirLight.dir = { dirLightDir[0], dirLightDir[1], dirLightDir[2] };
    d.dirLight.brightness = dirLightBrightness;
    d.dirLight.color = { dirLightColor[0], dirLightColor[1], dirLightColor[2] };

    // Fog
    d.fog.startY = fogStartY;
    d.fog.density = fogDensity;
    d.fog.color = { fogColor[0], fogColor[1], fogColor[2] };

    // Shadows
    d.shadow.enabled = shadowEnabled;
    d.shadow.bias = shadowBias;
    d.shadow.rasterDepthBias = shadowRasterDepthBias;
    d.shadow.rasterSlopeBias = shadowRasterSlopeBias;
    d.shadow.rasterBiasClamp = shadowRasterBiasClamp;
    d.shadow.lightDistance = shadowLightDistance;
    d.shadow.orthoSize = shadowOrthoSize;
    d.shadow.nearPlane = shadowNearPlane;
    d.shadow.farPlane = shadowFarPlane;

    // Cubemap
    d.cubemap.enabled = cubemapEnabled;
    d.cubemap.resolution = cubemapResolution;
    d.cubemap.nearPlane = cubemapNearPlane;
    d.cubemap.farPlane = cubemapFarPlane;

    // Point lights
    d.pointLights.brightness = lightBrightness;
    d.pointLights.ambient = ambientBrightness;
    for (int i = 0; i < 8; ++i) {
        auto& la = lightAnims[i];
        auto& ld = d.pointLights.lights[i];
        ld.center = { la.center.x, la.center.y, la.center.z };
        ld.amp = { la.ampX, la.ampY, la.ampZ };
        ld.freq = { la.freqX, la.freqY, la.freqZ };
        ld.color = { la.color.x, la.color.y, la.color.z, la.color.w };
    }

    // Spawning
    d.spawning.stopped = spawningStopped;
    d.spawning.autoStop = autoStopSpawning;
    d.spawning.stopFrameMs = spawnStopFrameMs;
    d.spawning.batchSize = spawnBatchSize;

    // Display
    d.display.vsync = vsync;
    d.display.animateEntities = animateEntities;
    d.display.lightAnimSpeed = lightAnimationSpeed;
    d.display.showBillboards = showLightBillboards;

    // Background
    d.bgColor = { bgColor[0], bgColor[1], bgColor[2] };

    // Runtime
    d.runtime = runtimeConfig;

    // Materials
    for (const auto& m : scene.materials) {
        MaterialData md;
        md.name = m.name;
        md.albedo = { m.albedo.x, m.albedo.y, m.albedo.z, m.albedo.w };
        md.roughness = m.roughness;
        md.metallic = m.metallic;
        md.emissiveStrength = m.emissiveStrength;
        md.reflective = m.reflective;
        md.emissive = { m.emissive.x, m.emissive.y, m.emissive.z, m.emissive.w };
        d.materials.push_back(md);
    }

    // Entities — iterate all entities with MeshRef
    scene.ecsWorld.each([&](flecs::entity e, const Transform& tf, const MeshRef& mr) {
        EntityData ed;

        // Find mesh name by matching offsets
        for (size_t i = 0; i < scene.spawnableMeshRefs.size(); ++i) {
            if (scene.spawnableMeshRefs[i].vertexOffset == mr.vertexOffset &&
                scene.spawnableMeshRefs[i].indexOffset == mr.indexOffset) {
                ed.meshName = scene.spawnableMeshNames[i];
                break;
            }
        }

        // Material name
        if (mr.materialIndex >= 0 && mr.materialIndex < static_cast<int>(scene.materials.size())) {
            ed.materialName = scene.materials[mr.materialIndex].name;
        }

        // Position from translation row of world matrix
        ed.position = { tf.world._41, tf.world._42, tf.world._43 };

        // Estimate uniform scale from first row length
        float sx = std::sqrt(
            tf.world._11 * tf.world._11 + tf.world._12 * tf.world._12 + tf.world._13 * tf.world._13
        );
        ed.scale = sx;

        ed.albedoOverride = { mr.albedoOverride.x, mr.albedoOverride.y, mr.albedoOverride.z,
                              mr.albedoOverride.w };
        ed.pickable = e.has<Pickable>();

        // Animated component
        if (e.has<Animated>()) {
            const auto& anim = e.get<Animated>();
            AnimatedData ad;
            ad.speed = anim.speed;
            ad.orbitRadius = anim.orbitRadius;
            ad.orbitY = anim.orbitY;
            ad.initialScale = anim.initialScale;
            ad.rotAxis = { anim.rotAxis.x, anim.rotAxis.y, anim.rotAxis.z };
            ad.rotAngle = anim.rotAngle;
            ad.pulsePhase = anim.pulsePhase;
            ed.animated = ad;
        }

        d.entities.push_back(ed);
    });

    return d;
}

// ---------------------------------------------------------------------------
// Apply a SceneFileData to the Application state
// ---------------------------------------------------------------------------

void Application::applySceneData(const SceneFileData& d)
{
    // Camera
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
    dirLightDir[0] = d.dirLight.dir[0];
    dirLightDir[1] = d.dirLight.dir[1];
    dirLightDir[2] = d.dirLight.dir[2];
    dirLightBrightness = d.dirLight.brightness;
    dirLightColor[0] = d.dirLight.color[0];
    dirLightColor[1] = d.dirLight.color[1];
    dirLightColor[2] = d.dirLight.color[2];

    // Fog
    fogStartY = d.fog.startY;
    fogDensity = d.fog.density;
    fogColor[0] = d.fog.color[0];
    fogColor[1] = d.fog.color[1];
    fogColor[2] = d.fog.color[2];

    // Shadows
    bool shadowRasterChanged =
        (shadowRasterDepthBias != d.shadow.rasterDepthBias ||
         shadowRasterSlopeBias != d.shadow.rasterSlopeBias ||
         shadowRasterBiasClamp != d.shadow.rasterBiasClamp);
    shadowEnabled = d.shadow.enabled;
    shadowBias = d.shadow.bias;
    shadowRasterDepthBias = d.shadow.rasterDepthBias;
    shadowRasterSlopeBias = d.shadow.rasterSlopeBias;
    shadowRasterBiasClamp = d.shadow.rasterBiasClamp;
    shadowLightDistance = d.shadow.lightDistance;
    shadowOrthoSize = d.shadow.orthoSize;
    shadowNearPlane = d.shadow.nearPlane;
    shadowFarPlane = d.shadow.farPlane;

    // Cubemap
    bool cubemapResChanged = (cubemapResolution != d.cubemap.resolution);
    cubemapEnabled = d.cubemap.enabled;
    cubemapResolution = d.cubemap.resolution;
    cubemapNearPlane = d.cubemap.nearPlane;
    cubemapFarPlane = d.cubemap.farPlane;

    // Point lights
    lightBrightness = d.pointLights.brightness;
    ambientBrightness = d.pointLights.ambient;
    for (int i = 0; i < 8; ++i) {
        const auto& ld = d.pointLights.lights[i];
        lightAnims[i].center = { ld.center[0], ld.center[1], ld.center[2] };
        lightAnims[i].ampX = ld.amp[0];
        lightAnims[i].ampY = ld.amp[1];
        lightAnims[i].ampZ = ld.amp[2];
        lightAnims[i].freqX = ld.freq[0];
        lightAnims[i].freqY = ld.freq[1];
        lightAnims[i].freqZ = ld.freq[2];
        lightAnims[i].color = { ld.color[0], ld.color[1], ld.color[2], ld.color[3] };
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

    // Background
    bgColor[0] = d.bgColor[0];
    bgColor[1] = d.bgColor[1];
    bgColor[2] = d.bgColor[2];

    // Runtime
    runtimeConfig = d.runtime;

    // Rebuild GPU resources if needed
    if (contentLoaded) {
        if (shadowRasterChanged) {
            createShadowPSO();
        }
        if (cubemapResChanged) {
            createCubemapResources();
        }
    }

    // Apply materials from scene file (if any provided)
    if (!d.materials.empty()) {
        scene.materials.clear();
        for (const auto& md : d.materials) {
            Material m;
            m.name = md.name;
            m.albedo = { md.albedo[0], md.albedo[1], md.albedo[2], md.albedo[3] };
            m.roughness = md.roughness;
            m.metallic = md.metallic;
            m.emissiveStrength = md.emissiveStrength;
            m.reflective = md.reflective;
            m.emissive = { md.emissive[0], md.emissive[1], md.emissive[2], md.emissive[3] };
            scene.materials.push_back(m);
        }
        // Rebuild preset indices by name
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
            // Resolve mesh by name
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

            // Resolve material by name
            for (int i = 0; i < static_cast<int>(scene.materials.size()); ++i) {
                if (scene.materials[i].name == ed.materialName) {
                    mesh.materialIndex = i;
                    break;
                }
            }

            mesh.albedoOverride = { ed.albedoOverride[0], ed.albedoOverride[1],
                                    ed.albedoOverride[2], ed.albedoOverride[3] };

            vec3 axis = { ed.rotAxis[0], ed.rotAxis[1], ed.rotAxis[2] };
            mat4 world = scale(ed.scale, ed.scale, ed.scale) * rotateAxis(axis, ed.rotAngle) *
                         translate(ed.position[0], ed.position[1], ed.position[2]);

            Transform tf;
            tf.world = world;

            auto entity = scene.ecsWorld.entity().set(tf).set(mesh);

            if (ed.animated.has_value()) {
                const auto& ad = ed.animated.value();
                Animated anim;
                anim.speed = ad.speed;
                anim.orbitRadius = ad.orbitRadius;
                anim.orbitAngle = std::atan2(ed.position[2], ed.position[0]);
                anim.orbitY = ad.orbitY;
                anim.initialScale = ad.initialScale;
                anim.rotAxis = { ad.rotAxis[0], ad.rotAxis[1], ad.rotAxis[2] };
                anim.rotAngle = ad.rotAngle;
                anim.pulsePhase = ad.pulsePhase;
                entity.set(anim);
            }

            if (ed.pickable) {
                entity.add<Pickable>();
            }
        }
        spdlog::info("Spawned {} entities from scene file", d.entities.size());
    }
}
