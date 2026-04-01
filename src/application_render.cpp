module;

#if defined(__clang__)
    #define FMT_CONSTEVAL
#endif

#include <d3d12.h>
#include <DirectXMath.h>
#include <dxgi1_6.h>
#include <flecs.h>
#include <ScreenGrab.h>
#include <spdlog/spdlog.h>
#include <wincodec.h>
#include <Windows.h>
#include <wrl.h>
#include <cassert>
#include <cmath>
#include <vector>
#include "d3dx12_clean.h"
#include "profiling.h"

module application;

#ifdef TRACY_ENABLE
extern TracyD3D12Ctx g_tracyD3d12Ctx;
#else
extern void* g_tracyD3d12Ctx;
#endif

using Microsoft::WRL::ComPtr;
using namespace DirectX;

void Application::render()
{
    PROFILE_ZONE();
    PROFILE_GPU_COLLECT(g_tracyD3d12Ctx);

    // Read back picked entity from previous frame
    picker.readPickResult();

    auto backBuffer = this->backBuffers[this->curBackBufIdx];
    auto cmdList = this->cmdQueue.getCmdList();

    // --- Compute per-frame scene data ---
    mat4 viewProj = this->cam.view() * this->cam.proj();
    float camX = this->cam.radius * cos(this->cam.pitch) * cos(this->cam.yaw);
    float camY = this->cam.radius * sin(this->cam.pitch);
    float camZ = this->cam.radius * cos(this->cam.pitch) * sin(this->cam.yaw);
    vec4 cameraPos(camX, camY, camZ, 1.0f);
    vec4 ambientColor(
        bgColor.x * ambientBrightness, bgColor.y * ambientBrightness, bgColor.z * ambientBrightness,
        1.0f
    );

    // Compute animated light positions for this frame
    vec4 animLightPos[SceneConstantBuffer::maxLights] = {};
    vec4 animLightColor[SceneConstantBuffer::maxLights] = {};
    int lightIdx = 0;
    scene.lightQuery.each([&](const PointLight& pl) {
        if (lightIdx >= SceneConstantBuffer::maxLights) {
            return;
        }
        animLightPos[lightIdx] = { pl.center.x + pl.amp.x * std::sin(pl.freq.x * lightTime),
                                   pl.center.y + pl.amp.y * std::cos(pl.freq.y * lightTime),
                                   pl.center.z +
                                       pl.amp.z * std::sin(pl.freq.z * lightTime + (float)lightIdx),
                                   1.0f };
        animLightColor[lightIdx] = { pl.color.x * lightBrightness, pl.color.y * lightBrightness,
                                     pl.color.z * lightBrightness, 1.0f };
        lightIdx++;
    });
    billboards.updateInstances(
        animLightPos, animLightColor, static_cast<uint32_t>(SceneConstantBuffer::maxLights)
    );

    // Directional light shadow map viewProj
    mat4 lightViewProj{};
    vec4 dirLightDirVec{};  // toward light (negated from UI direction)
    vec4 dirLightColorVec{};
    if (shadowEnabled) {
        // UI stores direction FROM light; negate to get direction TOWARD light for shader
        XMVECTOR fromLight =
            XMVector3Normalize(XMVectorSet(dirLightDir.x, dirLightDir.y, dirLightDir.z, 0.0f));
        XMVECTOR toLight = XMVectorNegate(fromLight);
        XMStoreFloat4(reinterpret_cast<XMFLOAT4*>(&dirLightDirVec), XMVectorSetW(toLight, 0.0f));
        dirLightColorVec = { dirLightColor.x * dirLightBrightness,
                             dirLightColor.y * dirLightBrightness,
                             dirLightColor.z * dirLightBrightness, 1.0f };

        // Place virtual light position far along the direction for LookAt
        XMVECTOR lightP = XMVectorScale(toLight, shadowLightDistance);
        XMVECTOR target = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
        XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
        float dotUp = fabsf(XMVectorGetByIndex(
            XMVector3Dot(XMVector3Normalize(XMVectorSubtract(target, lightP)), up), 0
        ));
        if (dotUp > 0.99f) {
            up = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
        }
        XMMATRIX lightView = XMMatrixLookAtLH(lightP, target, up);
        XMMATRIX lightProj = XMMatrixOrthographicLH(
            shadowOrthoSize, shadowOrthoSize, std::max(0.001f, shadowNearPlane),
            std::max(shadowNearPlane + 0.001f, shadowFarPlane)
        );
        XMMATRIX lvp = XMMatrixMultiply(lightView, lightProj);
        XMStoreFloat4x4(reinterpret_cast<XMFLOAT4X4*>(&lightViewProj), lvp);
    }

    // --- Fill structured buffer (scene + shadow draw data) ---
    struct DrawCmd
    {
        uint32_t indexCount;
        uint32_t indexOffset;
        uint32_t vertexOffset;
        uint32_t instanceCount;
        uint32_t baseDrawIndex;
    };
    std::vector<DrawCmd> drawCmds;
    drawIndexToEntity.clear();
    uint32_t drawIdx = 0;
    SceneConstantBuffer* mapped = scene.drawDataMapped[curBackBufIdx];
    bool anyReflective = false;
    vec3 reflectivePos{};

    // Helper: fill per-frame fields shared by all draws
    auto fillPerFrame = [&](SceneConstantBuffer& scb, const Material& mat, const vec4& albedo) {
        scb.viewProj = viewProj;
        scb.cameraPos = cameraPos;
        scb.ambientColor = ambientColor;
        for (int li = 0; li < SceneConstantBuffer::maxLights; ++li) {
            scb.lightPos[li] = animLightPos[li];
            scb.lightColor[li] = animLightColor[li];
        }
        scb.albedo = albedo;
        scb.roughness = mat.roughness;
        scb.metallic = mat.metallic;
        scb.emissiveStrength = mat.emissiveStrength;
        scb.reflective = mat.reflective ? 1.0f : 0.0f;
        scb.emissive = mat.emissive;
        scb.dirLightDir = dirLightDirVec;
        scb.dirLightColor = dirLightColorVec;
        scb.lightViewProj = lightViewProj;
        scb.shadowBias = shadowBias;
        scb.shadowMapTexelSize = 1.0f / static_cast<float>(shadowMapSize);
        scb.fogStartY = fogStartY;
        scb.fogDensity = fogDensity;
        scb.fogColor = vec4(fogColor, 0.0f);
    };

    // Regular entities (one draw call each)
    scene.drawQuery.each([&](flecs::entity e, const Transform& tf, const MeshRef& mesh) {
        assert(drawIdx < Scene::maxDrawsPerFrame / 3);
        const Material& mat = scene.materials[mesh.materialIndex];

        SceneConstantBuffer& scb = mapped[drawIdx];
        scb.model = tf.world * this->matModel;
        vec4 albedo = mesh.albedoOverride.w > 0.0f ? mesh.albedoOverride : mat.albedo;
        fillPerFrame(scb, mat, albedo);

        if (mat.reflective && !anyReflective) {
            anyReflective = true;
            reflectivePos = vec3(scb.model._41, scb.model._42, scb.model._43);
        }

        drawCmds.push_back({ mesh.indexCount, mesh.indexOffset, mesh.vertexOffset, 1, drawIdx });
        drawIndexToEntity.push_back(e);
        drawIdx++;
    });

    // Instanced groups (one draw call per group, N structured buffer slots)
    scene.instanceQuery.each([&](flecs::entity e, const Transform& /*groupTf*/,
                                 const InstanceGroup& group) {
        uint32_t N = static_cast<uint32_t>(group.transforms.size());
        if (N == 0) {
            return;
        }
        assert(drawIdx + N <= Scene::maxDrawsPerFrame / 3);
        const Material& mat = scene.materials[group.mesh.materialIndex];
        uint32_t baseIdx = drawIdx;

        for (uint32_t i = 0; i < N; ++i) {
            SceneConstantBuffer& scb = mapped[drawIdx];
            scb.model = group.transforms[i] * this->matModel;
            vec4 albedo = (i < group.albedoOverrides.size() && group.albedoOverrides[i].w > 0.0f)
                              ? group.albedoOverrides[i]
                              : mat.albedo;
            fillPerFrame(scb, mat, albedo);
            if (i < group.roughnessOverrides.size()) {
                scb.roughness = group.roughnessOverrides[i];
            }
            if (i < group.metallicOverrides.size()) {
                scb.metallic = group.metallicOverrides[i];
            }
            if (i < group.emissiveStrengthOverrides.size()) {
                scb.emissiveStrength = group.emissiveStrengthOverrides[i];
            }

            if (mat.reflective && !anyReflective) {
                anyReflective = true;
                reflectivePos = vec3(scb.model._41, scb.model._42, scb.model._43);
            }

            drawIndexToEntity.push_back(e);
            drawIdx++;
        }

        drawCmds.push_back(
            { group.mesh.indexCount, group.mesh.indexOffset, group.mesh.vertexOffset, N, baseIdx }
        );
    });

    uint32_t totalSlots = drawIdx;

    // Shadow draw data (same model transforms, light viewProj instead of camera viewProj)
    if (shadowEnabled) {
        for (uint32_t i = 0; i < totalSlots; ++i) {
            SceneConstantBuffer& shadow = mapped[totalSlots + i];
            shadow.model = mapped[i].model;
            shadow.viewProj = lightViewProj;
        }
    }

    this->lastFrameObjectCount = totalSlots;

    // --- Shadow pass ---
    if (shadowEnabled) {
        PROFILE_ZONE_NAMED("Shadow Pass");
        PROFILE_GPU_ZONE(g_tracyD3d12Ctx, cmdList.Get(), "GPU: Shadow");
        this->transitionResource(
            cmdList, shadowMap, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_DEPTH_WRITE
        );
        auto shadowDsv = shadowDsvHeap->GetCPUDescriptorHandleForHeapStart();
        this->clearDepth(cmdList, shadowDsv);

        cmdList->SetPipelineState(this->shadowPSO.Get());
        cmdList->SetGraphicsRootSignature(this->rootSignature.Get());
        cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        D3D12_VIEWPORT shadowVP = { 0.0f, 0.0f, (float)shadowMapSize, (float)shadowMapSize,
                                    0.0f, 1.0f };
        D3D12_RECT shadowScissor = { 0, 0, (LONG)shadowMapSize, (LONG)shadowMapSize };
        cmdList->RSSetViewports(1, &shadowVP);
        cmdList->RSSetScissorRects(1, &shadowScissor);
        cmdList->OMSetRenderTargets(0, nullptr, false, &shadowDsv);

        cmdList->IASetVertexBuffers(0, 1, &scene.megaVBV);
        cmdList->IASetIndexBuffer(&scene.megaIBV);

        ID3D12DescriptorHeap* sceneHeaps[] = { scene.sceneSrvHeap.Get() };
        cmdList->SetDescriptorHeaps(1, sceneHeaps);
        CD3DX12_GPU_DESCRIPTOR_HANDLE srvGpuHandle(
            scene.sceneSrvHeap->GetGPUDescriptorHandleForHeapStart(),
            static_cast<INT>(curBackBufIdx), scene.sceneSrvDescSize
        );
        cmdList->SetGraphicsRootDescriptorTable(0, srvGpuHandle);

        for (uint32_t i = 0; i < static_cast<uint32_t>(drawCmds.size()); ++i) {
            cmdList->SetGraphicsRoot32BitConstant(1, totalSlots + drawCmds[i].baseDrawIndex, 0);
            cmdList->DrawIndexedInstanced(
                drawCmds[i].indexCount, drawCmds[i].instanceCount, drawCmds[i].indexOffset,
                static_cast<INT>(drawCmds[i].vertexOffset), 0
            );
        }

        this->transitionResource(
            cmdList, shadowMap, D3D12_RESOURCE_STATE_DEPTH_WRITE,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
        );
    }

    // --- Cubemap pass: render environment for reflective objects ---
    if (cubemapEnabled && anyReflective) {
        PROFILE_ZONE_NAMED("Cubemap Pass");
        // Build 6 cubemap face view-projection matrices (LH, 90° FOV)
        XMVECTOR eyePos = XMVectorSet(reflectivePos.x, reflectivePos.y, reflectivePos.z, 1.0f);
        XMMATRIX cubeProj = XMMatrixPerspectiveFovLH(
            XM_PIDIV2, 1.0f, std::max(0.001f, cubemapNearPlane),
            std::max(cubemapNearPlane + 0.001f, cubemapFarPlane)
        );
        struct CubeFace
        {
            XMVECTOR dir;
            XMVECTOR up;
        };
        CubeFace cubeFaces[6] = {
            { XMVectorSet(1, 0, 0, 0), XMVectorSet(0, 1, 0, 0) },   // +X
            { XMVectorSet(-1, 0, 0, 0), XMVectorSet(0, 1, 0, 0) },  // -X
            { XMVectorSet(0, 1, 0, 0), XMVectorSet(0, 0, -1, 0) },  // +Y
            { XMVectorSet(0, -1, 0, 0), XMVectorSet(0, 0, 1, 0) },  // -Y
            { XMVectorSet(0, 0, 1, 0), XMVectorSet(0, 1, 0, 0) },   // +Z
            { XMVectorSet(0, 0, -1, 0), XMVectorSet(0, 1, 0, 0) },  // -Z
        };

        // Write cubemap draw data: non-reflective entities only, 6 copies with different viewProj
        // Placed at offset 2*entityCount in the structured buffer
        uint32_t cubemapBaseIdx = 2 * totalSlots;
        std::vector<uint32_t> nonReflectiveIndices;
        for (uint32_t i = 0; i < totalSlots; ++i) {
            if (mapped[i].reflective < 0.5f) {
                nonReflectiveIndices.push_back(i);
            }
        }
        uint32_t nonReflCount = static_cast<uint32_t>(nonReflectiveIndices.size());

        static bool warnedMissingCubemapSources = false;
        if (nonReflCount == 0) {
            if (!warnedMissingCubemapSources) {
                spdlog::warn(
                    "Cubemap pass skipped: no non-reflective entities available to render."
                );
                warnedMissingCubemapSources = true;
            }
        } else {
            warnedMissingCubemapSources = false;
        }

        if (nonReflCount > 0) {
            this->transitionResource(
                cmdList, cubemapTexture, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                D3D12_RESOURCE_STATE_RENDER_TARGET
            );

            for (uint32_t face = 0; face < 6; ++face) {
                XMMATRIX faceView = XMMatrixLookAtLH(
                    eyePos, XMVectorAdd(eyePos, cubeFaces[face].dir), cubeFaces[face].up
                );
                mat4 faceVP(XMMatrixMultiply(faceView, cubeProj));
                uint32_t faceOffset = cubemapBaseIdx + face * nonReflCount;
                for (uint32_t j = 0; j < nonReflCount; ++j) {
                    uint32_t srcIdx = nonReflectiveIndices[j];
                    SceneConstantBuffer& dst = mapped[faceOffset + j];
                    dst = mapped[srcIdx];
                    dst.viewProj = faceVP;
                    dst.reflective = 0.0f;
                }
            }

            // Render 6 cubemap faces
            D3D12_VIEWPORT cubeVP = {
                0, 0, (float)cubemapResolution, (float)cubemapResolution, 0, 1
            };
            D3D12_RECT cubeScissor = { 0, 0, (LONG)cubemapResolution, (LONG)cubemapResolution };
            UINT rtvSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
            UINT dsvSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

            cmdList->SetPipelineState(this->pipelineState.Get());
            cmdList->SetGraphicsRootSignature(this->rootSignature.Get());
            cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            cmdList->IASetVertexBuffers(0, 1, &scene.megaVBV);
            cmdList->IASetIndexBuffer(&scene.megaIBV);

            ID3D12DescriptorHeap* sceneHeaps[] = { scene.sceneSrvHeap.Get() };
            cmdList->SetDescriptorHeaps(1, sceneHeaps);
            CD3DX12_GPU_DESCRIPTOR_HANDLE srvGpuHandle(
                scene.sceneSrvHeap->GetGPUDescriptorHandleForHeapStart(),
                static_cast<INT>(curBackBufIdx), scene.sceneSrvDescSize
            );
            cmdList->SetGraphicsRootDescriptorTable(0, srvGpuHandle);
            // Bind shadow map for cubemap pass too
            CD3DX12_GPU_DESCRIPTOR_HANDLE shadowSrv(
                scene.sceneSrvHeap->GetGPUDescriptorHandleForHeapStart(),
                static_cast<INT>(Scene::nBuffers), scene.sceneSrvDescSize
            );
            cmdList->SetGraphicsRootDescriptorTable(2, shadowSrv);
            // Bind cubemap SRV (will be black/uninitialized but reflective=0 prevents sampling)
            CD3DX12_GPU_DESCRIPTOR_HANDLE cubeSrv(
                scene.sceneSrvHeap->GetGPUDescriptorHandleForHeapStart(),
                static_cast<INT>(Scene::nBuffers + 1), scene.sceneSrvDescSize
            );
            cmdList->SetGraphicsRootDescriptorTable(3, cubeSrv);

            for (uint32_t face = 0; face < 6; ++face) {
                CD3DX12_CPU_DESCRIPTOR_HANDLE faceRtv(
                    cubemapRtvHeap->GetCPUDescriptorHandleForHeapStart(), static_cast<INT>(face),
                    rtvSize
                );
                CD3DX12_CPU_DESCRIPTOR_HANDLE faceDsv(
                    cubemapDsvHeap->GetCPUDescriptorHandleForHeapStart(), static_cast<INT>(face),
                    dsvSize
                );
                FLOAT clearColor[] = { bgColor.x, bgColor.y, bgColor.z, 1.0f };
                this->clearRTV(cmdList, faceRtv, clearColor);
                this->clearDepth(cmdList, faceDsv);
                cmdList->RSSetViewports(1, &cubeVP);
                cmdList->RSSetScissorRects(1, &cubeScissor);
                cmdList->OMSetRenderTargets(1, &faceRtv, true, &faceDsv);

                uint32_t faceOffset = cubemapBaseIdx + face * nonReflCount;
                for (uint32_t j = 0; j < nonReflCount; ++j) {
                    uint32_t drawDataIdx = faceOffset + j;
                    uint32_t srcIdx = nonReflectiveIndices[j];
                    cmdList->SetGraphicsRoot32BitConstant(1, drawDataIdx, 0);
                    cmdList->DrawIndexedInstanced(
                        drawCmds[srcIdx].indexCount, 1, drawCmds[srcIdx].indexOffset,
                        static_cast<INT>(drawCmds[srcIdx].vertexOffset), 0
                    );
                }
            }

            this->transitionResource(
                cmdList, cubemapTexture, D3D12_RESOURCE_STATE_RENDER_TARGET,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
            );
        }
    }

    // --- Normal pre-pass: render world normals for SSAO ---
    if (ssao.enabled) {
        PROFILE_ZONE_NAMED("Normal Pre-pass + SSAO");
        PROFILE_GPU_ZONE(g_tracyD3d12Ctx, cmdList.Get(), "GPU: Normal Pre-pass + SSAO");
        ssao.transitionResource(
            cmdList, ssao.normalRT.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_RENDER_TARGET
        );
        auto normalRtv = ssao.normalRtvCpu();
        auto dsv = this->dsvHeap->GetCPUDescriptorHandleForHeapStart();
        FLOAT clearNormal[] = { 0.5f, 0.5f, 1.0f, 1.0f };
        cmdList->ClearRenderTargetView(normalRtv, clearNormal, 0, nullptr);
        this->clearDepth(cmdList, dsv);

        cmdList->SetPipelineState(this->normalPSO.Get());
        cmdList->SetGraphicsRootSignature(this->rootSignature.Get());
        cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cmdList->RSSetViewports(1, &this->viewport);
        cmdList->RSSetScissorRects(1, &this->scissorRect);
        cmdList->OMSetRenderTargets(1, &normalRtv, true, &dsv);

        cmdList->IASetVertexBuffers(0, 1, &scene.megaVBV);
        cmdList->IASetIndexBuffer(&scene.megaIBV);

        ID3D12DescriptorHeap* prepassHeaps[] = { scene.sceneSrvHeap.Get() };
        cmdList->SetDescriptorHeaps(1, prepassHeaps);
        CD3DX12_GPU_DESCRIPTOR_HANDLE prepassSrv(
            scene.sceneSrvHeap->GetGPUDescriptorHandleForHeapStart(),
            static_cast<INT>(curBackBufIdx), scene.sceneSrvDescSize
        );
        cmdList->SetGraphicsRootDescriptorTable(0, prepassSrv);

        for (uint32_t i = 0; i < static_cast<uint32_t>(drawCmds.size()); ++i) {
            cmdList->SetGraphicsRoot32BitConstant(1, drawCmds[i].baseDrawIndex, 0);
            cmdList->DrawIndexedInstanced(
                drawCmds[i].indexCount, drawCmds[i].instanceCount, drawCmds[i].indexOffset,
                static_cast<INT>(drawCmds[i].vertexOffset), 0
            );
        }

        ssao.transitionResource(
            cmdList, ssao.normalRT.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
        );

        // Transition depth to shader-readable state for SSAO
        this->transitionResource(
            cmdList, depthBuffer, D3D12_RESOURCE_STATE_DEPTH_WRITE,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
        );

        ssao.render(cmdList, this->cam.view(), this->cam.proj(), clientWidth, clientHeight);

        // Restore depth for scene pass
        this->transitionResource(
            cmdList, depthBuffer, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_DEPTH_WRITE
        );
    }

    // --- Scene pass: render to HDR render target ---
    {
        PROFILE_ZONE_NAMED("Scene Pass");
        PROFILE_GPU_ZONE(g_tracyD3d12Ctx, cmdList.Get(), "GPU: Scene");
        FLOAT clearColor[] = { bgColor.x, bgColor.y, bgColor.z, 1.0f };
        CD3DX12_CPU_DESCRIPTOR_HANDLE hdrRtv(
            bloom.bloomRtvHeap->GetCPUDescriptorHandleForHeapStart()
        );
        this->clearRTV(cmdList, hdrRtv, clearColor);
        auto dsv = this->dsvHeap->GetCPUDescriptorHandleForHeapStart();
        this->clearDepth(cmdList, dsv);

        cmdList->SetPipelineState(this->pipelineState.Get());
        cmdList->SetGraphicsRootSignature(this->rootSignature.Get());
        cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cmdList->RSSetViewports(1, &this->viewport);
        cmdList->RSSetScissorRects(1, &this->scissorRect);
        cmdList->OMSetRenderTargets(1, &hdrRtv, true, &dsv);

        cmdList->IASetVertexBuffers(0, 1, &scene.megaVBV);
        cmdList->IASetIndexBuffer(&scene.megaIBV);

        ID3D12DescriptorHeap* sceneHeaps[] = { scene.sceneSrvHeap.Get() };
        cmdList->SetDescriptorHeaps(1, sceneHeaps);
        CD3DX12_GPU_DESCRIPTOR_HANDLE srvGpuHandle(
            scene.sceneSrvHeap->GetGPUDescriptorHandleForHeapStart(),
            static_cast<INT>(curBackBufIdx), scene.sceneSrvDescSize
        );
        cmdList->SetGraphicsRootDescriptorTable(0, srvGpuHandle);

        // Bind shadow map SRV (descriptor index 3 in sceneSrvHeap)
        CD3DX12_GPU_DESCRIPTOR_HANDLE shadowSrvHandle(
            scene.sceneSrvHeap->GetGPUDescriptorHandleForHeapStart(),
            static_cast<INT>(Scene::nBuffers), scene.sceneSrvDescSize
        );
        cmdList->SetGraphicsRootDescriptorTable(2, shadowSrvHandle);

        // Bind cubemap SRV (descriptor index nBuffers+1 in sceneSrvHeap)
        CD3DX12_GPU_DESCRIPTOR_HANDLE cubemapSrvHandle(
            scene.sceneSrvHeap->GetGPUDescriptorHandleForHeapStart(),
            static_cast<INT>(Scene::nBuffers + 1), scene.sceneSrvDescSize
        );
        cmdList->SetGraphicsRootDescriptorTable(3, cubemapSrvHandle);

        // Bind SSAO output SRV (descriptor index nBuffers+2 in sceneSrvHeap)
        CD3DX12_GPU_DESCRIPTOR_HANDLE ssaoSrvHandle(
            scene.sceneSrvHeap->GetGPUDescriptorHandleForHeapStart(),
            static_cast<INT>(Scene::nBuffers + 2), scene.sceneSrvDescSize
        );
        cmdList->SetGraphicsRootDescriptorTable(5, ssaoSrvHandle);

        cmdList->OMSetStencilRef(1);
        uint32_t currentVertexCount = 0;
        for (uint32_t i = 0; i < static_cast<uint32_t>(drawCmds.size()); ++i) {
            currentVertexCount += drawCmds[i].indexCount * drawCmds[i].instanceCount;
            cmdList->SetGraphicsRoot32BitConstant(1, drawCmds[i].baseDrawIndex, 0);
            cmdList->DrawIndexedInstanced(
                drawCmds[i].indexCount, drawCmds[i].instanceCount, drawCmds[i].indexOffset,
                static_cast<INT>(drawCmds[i].vertexOffset), 0
            );
        }

        this->lastFrameVertexCount = currentVertexCount;
    }

    // --- Outline pass: stencil-based silhouette for hovered/selected ---
    if (hoveredEntity || selectedEntity) {
        PROFILE_ZONE_NAMED("Outline Pass");
        auto dsv = this->dsvHeap->GetCPUDescriptorHandleForHeapStart();
        CD3DX12_CPU_DESCRIPTOR_HANDLE hdrRtv(
            bloom.bloomRtvHeap->GetCPUDescriptorHandleForHeapStart()
        );

        cmdList->SetPipelineState(outlinePSO.Get());
        cmdList->SetGraphicsRootSignature(this->rootSignature.Get());
        cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cmdList->RSSetViewports(1, &this->viewport);
        cmdList->RSSetScissorRects(1, &this->scissorRect);
        cmdList->OMSetRenderTargets(1, &hdrRtv, true, &dsv);
        cmdList->OMSetStencilRef(1);

        cmdList->IASetVertexBuffers(0, 1, &scene.megaVBV);
        cmdList->IASetIndexBuffer(&scene.megaIBV);

        ID3D12DescriptorHeap* outlineHeaps[] = { scene.sceneSrvHeap.Get() };
        cmdList->SetDescriptorHeaps(1, outlineHeaps);
        CD3DX12_GPU_DESCRIPTOR_HANDLE outlineSrvHandle(
            scene.sceneSrvHeap->GetGPUDescriptorHandleForHeapStart(),
            static_cast<INT>(curBackBufIdx), scene.sceneSrvDescSize
        );
        cmdList->SetGraphicsRootDescriptorTable(0, outlineSrvHandle);

        auto drawOutline = [&](flecs::entity e, float width, float r, float g, float b) {
            for (uint32_t i = 0; i < static_cast<uint32_t>(drawIndexToEntity.size()); ++i) {
                if (drawIndexToEntity[i] == e) {
                    float params[4] = { width, r, g, b };
                    cmdList->SetGraphicsRoot32BitConstants(4, 4, params, 0);
                    cmdList->SetGraphicsRoot32BitConstant(1, i, 0);
                    cmdList->DrawIndexedInstanced(
                        drawCmds[i].indexCount, 1, drawCmds[i].indexOffset,
                        static_cast<INT>(drawCmds[i].vertexOffset), 0
                    );
                    break;
                }
            }
        };

        if (hoveredEntity) {
            drawOutline(hoveredEntity, 0.03f, 0.3f, 0.8f, 1.0f);
        }
        if (selectedEntity) {
            drawOutline(selectedEntity, 0.06f, 1.0f, 0.75f, 0.1f);
        }
    }

    // --- Object ID pass (for picking) ---
    {
        PROFILE_ZONE_NAMED("ID Pass");
        auto idRtv = picker.getRTV();
        auto idDsv = picker.getDSV();
        // Clear ID RT to invalidID
        FLOAT clearColor[] = { static_cast<float>(ObjectPicker::invalidID), 0.0f, 0.0f, 0.0f };
        cmdList->ClearRenderTargetView(idRtv, clearColor, 0, nullptr);
        cmdList->ClearDepthStencilView(idDsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

        cmdList->SetPipelineState(picker.pso.Get());
        cmdList->SetGraphicsRootSignature(this->rootSignature.Get());
        cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cmdList->RSSetViewports(1, &this->viewport);
        cmdList->RSSetScissorRects(1, &this->scissorRect);
        cmdList->OMSetRenderTargets(1, &idRtv, true, &idDsv);

        cmdList->IASetVertexBuffers(0, 1, &scene.megaVBV);
        cmdList->IASetIndexBuffer(&scene.megaIBV);

        ID3D12DescriptorHeap* sceneHeaps[] = { scene.sceneSrvHeap.Get() };
        cmdList->SetDescriptorHeaps(1, sceneHeaps);
        CD3DX12_GPU_DESCRIPTOR_HANDLE srvGpuHandle(
            scene.sceneSrvHeap->GetGPUDescriptorHandleForHeapStart(),
            static_cast<INT>(curBackBufIdx), scene.sceneSrvDescSize
        );
        cmdList->SetGraphicsRootDescriptorTable(0, srvGpuHandle);

        for (uint32_t i = 0; i < static_cast<uint32_t>(drawCmds.size()); ++i) {
            cmdList->SetGraphicsRoot32BitConstant(1, drawCmds[i].baseDrawIndex, 0);
            cmdList->DrawIndexedInstanced(
                drawCmds[i].indexCount, drawCmds[i].instanceCount, drawCmds[i].indexOffset,
                static_cast<INT>(drawCmds[i].vertexOffset), 0
            );
        }

        // Copy pixel under mouse cursor to readback buffer
        uint32_t mx = static_cast<uint32_t>(mousePos.x);
        uint32_t my = static_cast<uint32_t>(mousePos.y);
        picker.copyPickedPixel(cmdList, mx, my);
    }

    // --- Light billboards pass (batched instancing) ---
    if (showLightBillboards) {
        PROFILE_ZONE_NAMED("Billboards Pass");
        auto dsv = this->dsvHeap->GetCPUDescriptorHandleForHeapStart();
        CD3DX12_CPU_DESCRIPTOR_HANDLE hdrRtv(
            bloom.bloomRtvHeap->GetCPUDescriptorHandleForHeapStart()
        );
        cmdList->RSSetViewports(1, &this->viewport);
        cmdList->RSSetScissorRects(1, &this->scissorRect);
        cmdList->OMSetRenderTargets(1, &hdrRtv, true, &dsv);
        billboards.render(cmdList, viewProj, vec3(cameraPos.x, cameraPos.y, cameraPos.z));
    }

    // --- Bloom + composite ---
    CD3DX12_CPU_DESCRIPTOR_HANDLE backBufRtv(
        rtvHeap->GetCPUDescriptorHandleForHeapStart(), static_cast<INT>(curBackBufIdx), rtvDescSize
    );
    // Compute sky params for composite pass
    BloomRenderer::SkyParams skyParams;
    {
        vec3 camPos3(camX, camY, camZ);
        vec3 target3(0.0f, 0.0f, 0.0f);
        skyParams.camForward =
            normalize(vec3(target3.x - camPos3.x, target3.y - camPos3.y, target3.z - camPos3.z));
        vec3 worldUp(0.0f, 1.0f, 0.0f);
        skyParams.camRight = normalize(cross(worldUp, skyParams.camForward));
        skyParams.camUp = cross(skyParams.camForward, skyParams.camRight);
        skyParams.sunDir = normalize(-dirLightDir);
        skyParams.aspectRatio = static_cast<float>(clientWidth) / static_cast<float>(clientHeight);
        skyParams.tanHalfFov = std::tan(cam.fov * 0.5f);
    }
    {
        PROFILE_ZONE_NAMED("Bloom Pass");
        PROFILE_GPU_ZONE(g_tracyD3d12Ctx, cmdList.Get(), "GPU: Bloom");
        bloom.render(
            cmdList, backBuffer, backBufRtv, clientWidth, clientHeight, bloomThreshold,
            bloomIntensity, tonemapMode, skyParams
        );
    }

    if (!this->runtimeConfig.skipImGui) {
        PROFILE_ZONE_NAMED("ImGui Pass");
        this->renderImGui(cmdList);
    }

    this->transitionResource(
        cmdList, backBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT
    );

    this->frameFenceValues[this->curBackBufIdx] = this->cmdQueue.execCmdList(cmdList);
    // Signal Tracy's GPU fence AFTER submitting the command list so it comes after
    // all GPU zones in the queue, ensuring Collect() reads valid timestamp data.
    PROFILE_GPU_NEW_FRAME(g_tracyD3d12Ctx);

    UINT syncInterval = (this->vsync && !this->runtimeConfig.skipImGui) ? 1 : 0;
    UINT presentFlags = (this->tearingSupported && !this->vsync) ? DXGI_PRESENT_ALLOW_TEARING : 0;
    chkDX(this->swapChain->Present(syncInterval, presentFlags));
    this->curBackBufIdx = this->swapChain->GetCurrentBackBufferIndex();
    this->cmdQueue.waitForFenceVal(this->frameFenceValues[this->curBackBufIdx]);
    PROFILE_FRAME_MARK;

    if (this->runtimeConfig.screenshotFrame > 0) {
        this->frameCount++;
        if (this->frameCount == this->runtimeConfig.screenshotFrame) {
            spdlog::info("Saving screenshot and exiting...");
            HRESULT hr = DirectX::SaveWICTextureToFile(
                this->cmdQueue.queue.Get(), backBuffer.Get(), GUID_ContainerFormatPng,
                L"screenshot.png", D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_PRESENT
            );
            if (FAILED(hr)) {
                spdlog::error(
                    "Failed to save screenshot! HRESULT: {:#010x}", static_cast<uint32_t>(hr)
                );
            }
            if (this->runtimeConfig.exitAfterScreenshot) {
                Window::get()->doExit = true;
            }
        }
    }
}
