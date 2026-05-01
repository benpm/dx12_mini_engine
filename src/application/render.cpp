module;

#if defined(__clang__)
    #define FMT_CONSTEVAL
#endif

#include <d3d12.h>
#include <DirectXCollision.h>
#include <DirectXMath.h>
#include <dxgi1_6.h>
#include <flecs.h>
#include <ScreenGrab.h>
#include <spdlog/spdlog.h>
#include <wincodec.h>
#include <Windows.h>
#include <wrl.h>
#include <algorithm>
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

    const uint64_t frameReadyFence = this->frameFenceValues[this->curBackBufIdx];
    if (frameReadyFence != 0 && !this->cmdQueue.isFenceComplete(frameReadyFence)) {
        PROFILE_ZONE_NAMED("Frame Wait");
        this->cmdQueue.waitForFenceVal(frameReadyFence);
    }
    // Read back picked entity from previous frame if the copy's fence has completed.
    {
        PROFILE_ZONE_NAMED("Picker Readback Poll");
        picker.readPickResult(this->cmdQueue.completedFenceValue());
    }

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
    vec4 animLightPos[PerFrameCB::maxLights] = {};
    vec4 animLightColor[PerFrameCB::maxLights] = {};
    int lightIdx = 0;
    scene.lightQuery.each([&](const PointLight& pl) {
        if (lightIdx >= PerFrameCB::maxLights) {
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
        animLightPos, animLightColor, static_cast<uint32_t>(PerFrameCB::maxLights)
    );

    // Directional light shadow map viewProj
    mat4 lightViewProj{};
    vec4 dirLightDirVec{};  // toward light (negated from UI direction)
    vec4 dirLightColorVec{};
    if (shadow.enabled) {
        // UI stores direction FROM light; negate to get direction TOWARD light for shader
        XMVECTOR fromLight =
            XMVector3Normalize(XMVectorSet(dirLightDir.x, dirLightDir.y, dirLightDir.z, 0.0f));
        XMVECTOR toLight = XMVectorNegate(fromLight);
        XMStoreFloat4(reinterpret_cast<XMFLOAT4*>(&dirLightDirVec), XMVectorSetW(toLight, 0.0f));
        dirLightColorVec = { dirLightColor.x * dirLightBrightness,
                             dirLightColor.y * dirLightBrightness,
                             dirLightColor.z * dirLightBrightness, 1.0f };

        lightViewProj = shadow.computeLightViewProj(dirLightDir);
    }

    // Construct frustum for culling
    DirectX::BoundingFrustum frustum;
    DirectX::BoundingFrustum::CreateFromMatrix(frustum, this->cam.proj().load());
    XMMATRIX invView = XMMatrixInverse(nullptr, this->cam.view().load());
    frustum.Transform(frustum, invView);

    // --- Fill structured buffer (scene + shadow draw data) ---
    scene.populateDrawCommands(curBackBufIdx, this->matModel, cameraPos.xyz(), frustum);
    this->lastFrameObjectCount = scene.totalSlots;

    // Build filtered draw command list (excludes gizmo arrows)
    std::vector<DrawCmd> sceneDrawCmds;
    std::vector<DrawCmd> gizmoDrawCmds;
    sceneDrawCmds.reserve(scene.drawCmds.size());
    for (const auto& dc : scene.drawCmds) {
        if (dc.baseDrawIndex < scene.isGizmoDraw.size() && scene.isGizmoDraw[dc.baseDrawIndex]) {
            gizmoDrawCmds.push_back(dc);
        } else {
            sceneDrawCmds.push_back(dc);
        }
    }

    PerFrameCB* frameMapped = scene.perFrameMapped[curBackBufIdx];
    PerPassCB* passMappedBase = scene.perPassMapped[curBackBufIdx];
    const uint64_t passCBStride = (sizeof(PerPassCB) + 255) & ~255;
    auto getPassMapped = [&](uint32_t passIdx) {
        uint8_t* base = reinterpret_cast<uint8_t*>(passMappedBase);
        return reinterpret_cast<PerPassCB*>(base + passIdx * passCBStride);
    };
    auto getPassCBAddress = [&](uint32_t passIdx) {
        return scene.perPassBuffer[curBackBufIdx]->GetGPUVirtualAddress() + passIdx * passCBStride;
    };

    auto bindSharedGeometry = [&](ID3D12GraphicsCommandList2* cmd) {
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cmd->RSSetViewports(1, &this->viewport);
        cmd->RSSetScissorRects(1, &this->scissorRect);
        cmd->IASetVertexBuffers(0, 1, &scene.megaVBV);
        cmd->IASetIndexBuffer(&scene.megaIBV);
    };

    auto bindSceneHeapAndObjects = [&](ID3D12GraphicsCommandList2* cmd) {
        ID3D12DescriptorHeap* heaps[] = { scene.sceneSrvHeap.Get() };
        cmd->SetDescriptorHeaps(1, heaps);

        CD3DX12_GPU_DESCRIPTOR_HANDLE objectTable(
            scene.sceneSrvHeap->GetGPUDescriptorHandleForHeapStart(),
            static_cast<INT>(curBackBufIdx), scene.sceneSrvDescSize
        );
        cmd->SetGraphicsRootDescriptorTable(app_slots::rootPerObjectSrv, objectTable);
    };

    auto bindPerFrameAndPass = [&](ID3D12GraphicsCommandList2* cmd,
                                   D3D12_GPU_VIRTUAL_ADDRESS perPassAddr) {
        auto perFrameAddr = scene.perFrameBuffer[curBackBufIdx]->GetGPUVirtualAddress();
        cmd->SetGraphicsRootConstantBufferView(app_slots::rootPerFrameCB, perFrameAddr);
        cmd->SetGraphicsRootConstantBufferView(app_slots::rootPerPassCB, perPassAddr);
    };

    PerObjectData* objectMapped = scene.perObjectMapped[curBackBufIdx];

    // Fill PerFrameCB once
    frameMapped->ambientColor = ambientColor;
    for (int li = 0; li < PerFrameCB::maxLights; ++li) {
        frameMapped->lightPos[li] = animLightPos[li];
        frameMapped->lightColor[li] = animLightColor[li];
    }
    frameMapped->dirLightDir = dirLightDirVec;
    frameMapped->dirLightColor = dirLightColorVec;
    frameMapped->lightViewProj = lightViewProj;
    frameMapped->shadowBias = shadow.bias;
    frameMapped->shadowMapTexelSize = 1.0f / static_cast<float>(ShadowRenderer::mapSize);
    frameMapped->fogStartY = fogStartY;
    frameMapped->fogDensity = fogDensity;
    frameMapped->fogColor = vec4(fogColor, 0.0f);
    frameMapped->time = lightTime;

    // Main pass PerPassCB (index 0)
    PerPassCB* mainPass = getPassMapped(0);
    mainPass->viewProj = viewProj;
    mainPass->prevViewProj = prevViewProj;
    mainPass->cameraPos = cameraPos;

    if (prevViewProj.m[0][0] == 0.0f) {
        mainPass->prevViewProj = viewProj;
    }

    // --- Render Graph Setup ---
    renderGraph.reset();
    auto hBackBuffer =
        renderGraph.importTexture("BackBuffer", backBuffer.Get(), D3D12_RESOURCE_STATE_PRESENT);
    auto hDepthBuffer =
        renderGraph.importTexture("MainDepth", depthBuffer.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE);
    auto hShadowMap = renderGraph.importTexture(
        "ShadowMap", shadow.shadowMap.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
    );
    auto hNormalRT = renderGraph.importTexture(
        "NormalRT", gbuffer.resources[GBuffer::Normal].Get(), D3D12_RESOURCE_STATE_COMMON
    );
    auto hAlbedoRT = renderGraph.importTexture(
        "AlbedoRT", gbuffer.resources[GBuffer::Albedo].Get(), D3D12_RESOURCE_STATE_COMMON
    );
    auto hMaterialRT = renderGraph.importTexture(
        "MaterialRT", gbuffer.resources[GBuffer::Material].Get(), D3D12_RESOURCE_STATE_COMMON
    );
    auto hMotionRT = renderGraph.importTexture(
        "MotionRT", gbuffer.resources[GBuffer::Motion].Get(), D3D12_RESOURCE_STATE_COMMON
    );
    auto hHdrRT = renderGraph.importTexture(
        "HdrRT", bloom.hdrRenderTarget.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
    );
    auto hCubemap = renderGraph.importTexture(
        "Cubemap", cubemapTexture.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
    );

    // --- Shadow pass ---
    if (shadow.enabled) {
        // Shadow pass PerPassCB (index 1)
        PerPassCB* shadowPass = getPassMapped(1);
        shadowPass->viewProj = lightViewProj;
        shadowPass->cameraPos = vec4(0, 0, 0, 1);  // Not used in shadow pass

        renderGraph.addPass(
            "Shadow Pass",
            [&](rg::RenderGraphBuilder& builder) {
                builder.writeDepthStencil(
                    hShadowMap, shadow.dsvHeap->GetCPUDescriptorHandleForHeapStart()
                );
            },
            [&, shadowPassAddr = getPassCBAddress(1)](
                ID3D12GraphicsCommandList2* cmd, rg::RenderGraphBuilder& builder
            ) {
                PROFILE_ZONE_NAMED("Shadow Pass");
                PROFILE_GPU_ZONE(g_tracyD3d12Ctx, cmd, "GPU: Shadow");
                cmd->SetGraphicsRootSignature(this->rootSignature.Get());
                bindPerFrameAndPass(cmd, shadowPassAddr);

                shadow.render(
                    cmd, scene.megaVBV, scene.megaIBV, scene.sceneSrvHeap.Get(),
                    scene.sceneSrvDescSize, curBackBufIdx, sceneDrawCmds, 0
                );
            }
        );
    }

    // --- Cubemap pass ---
    if (cubemapEnabled && scene.anyReflective) {
        renderGraph.addPass(
            "Cubemap Pass",
            [&](rg::RenderGraphBuilder& builder) {
                builder.writeRenderTarget(
                    hCubemap, cubemapRtvHeap->GetCPUDescriptorHandleForHeapStart()
                );
                builder.readTexture(hShadowMap);
            },
            [&](ID3D12GraphicsCommandList2* cmd, rg::RenderGraphBuilder& builder) {
                PROFILE_ZONE_NAMED("Cubemap Pass");

                uint32_t cubemapBaseIdx =
                    scene.totalSlots;  // Place cubemap object data after main ones
                std::vector<uint32_t> nonReflectiveIndices;
                for (uint32_t i = 0; i < scene.totalSlots; ++i) {
                    if (objectMapped[i].reflective < 0.5f &&
                        !(i < scene.isGizmoDraw.size() && scene.isGizmoDraw[i])) {
                        nonReflectiveIndices.push_back(i);
                    }
                }
                uint32_t nonReflCount = static_cast<uint32_t>(nonReflectiveIndices.size());
                if (nonReflCount == 0) {
                    // Fallback: capture reflective geometry too so the cubemap is not empty.
                    for (uint32_t i = 0; i < scene.totalSlots; ++i) {
                        if (!(i < scene.isGizmoDraw.size() && scene.isGizmoDraw[i])) {
                            nonReflectiveIndices.push_back(i);
                        }
                    }
                    nonReflCount = static_cast<uint32_t>(nonReflectiveIndices.size());
                    if (nonReflCount == 0) {
                        return;
                    }
                }

                // Build 6 cubemap face view-projection matrices (LH, 90° FOV)
                XMVECTOR eyePos = XMVectorSet(
                    scene.reflectivePos.x, scene.reflectivePos.y, scene.reflectivePos.z, 1.0f
                );
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

                // Cubemap faces use PerPassCB indices 2-7
                for (uint32_t face = 0; face < 6; ++face) {
                    XMMATRIX faceView = XMMatrixLookAtLH(
                        eyePos, XMVectorAdd(eyePos, cubeFaces[face].dir), cubeFaces[face].up
                    );
                    mat4 faceVP(XMMatrixMultiply(faceView, cubeProj));

                    PerPassCB* facePass = getPassMapped(2 + face);
                    facePass->viewProj = faceVP;
                    facePass->cameraPos = vec4(
                        scene.reflectivePos.x, scene.reflectivePos.y, scene.reflectivePos.z, 1
                    );

                    uint32_t faceObjectOffset = cubemapBaseIdx + face * nonReflCount;
                    for (uint32_t j = 0; j < nonReflCount; ++j) {
                        uint32_t srcIdx = nonReflectiveIndices[j];
                        PerObjectData& dst = objectMapped[faceObjectOffset + j];
                        dst = objectMapped[srcIdx];
                        dst.reflective = 0.0f;
                    }
                }

                D3D12_VIEWPORT cubeVP = { 0, 0, (float)cubemapResolution, (float)cubemapResolution,
                                          0, 1 };
                D3D12_RECT cubeScissor = { 0, 0, (LONG)cubemapResolution, (LONG)cubemapResolution };

                cmd->SetPipelineState(this->pipelineState.Get());
                cmd->SetGraphicsRootSignature(this->rootSignature.Get());
                bindSharedGeometry(cmd);
                bindSceneHeapAndObjects(cmd);

                auto perFrameAddr = scene.perFrameBuffer[curBackBufIdx]->GetGPUVirtualAddress();
                cmd->SetGraphicsRootConstantBufferView(app_slots::rootPerFrameCB, perFrameAddr);

                CD3DX12_GPU_DESCRIPTOR_HANDLE shadowSrv(
                    scene.sceneSrvHeap->GetGPUDescriptorHandleForHeapStart(),
                    static_cast<INT>(app_slots::srvSlotShadow), scene.sceneSrvDescSize
                );
                cmd->SetGraphicsRootDescriptorTable(app_slots::rootShadowSrv, shadowSrv);

                CD3DX12_GPU_DESCRIPTOR_HANDLE cubeSrv(
                    scene.sceneSrvHeap->GetGPUDescriptorHandleForHeapStart(),
                    static_cast<INT>(app_slots::srvSlotCubemap), scene.sceneSrvDescSize
                );
                cmd->SetGraphicsRootDescriptorTable(app_slots::rootCubemapSrv, cubeSrv);

                for (uint32_t face = 0; face < 6; ++face) {
                    CD3DX12_CPU_DESCRIPTOR_HANDLE faceRtv(
                        cubemapRtvHeap->GetCPUDescriptorHandleForHeapStart(),
                        static_cast<INT>(face), cubemapRtvDescSize
                    );
                    CD3DX12_CPU_DESCRIPTOR_HANDLE faceDsv(
                        cubemapDsvHeap->GetCPUDescriptorHandleForHeapStart(),
                        static_cast<INT>(face), cubemapDsvDescSize
                    );
                    FLOAT clearColor[] = { 0, 0, 0, 1 };
                    this->clearRTV(cmd, faceRtv, clearColor);
                    this->clearDepth(cmd, faceDsv);
                    cmd->RSSetViewports(1, &cubeVP);
                    cmd->RSSetScissorRects(1, &cubeScissor);
                    cmd->OMSetRenderTargets(1, &faceRtv, true, &faceDsv);

                    cmd->SetGraphicsRootConstantBufferView(
                        app_slots::rootPerPassCB, getPassCBAddress(2 + face)
                    );

                    uint32_t faceObjectOffset = cubemapBaseIdx + face * nonReflCount;
                    for (uint32_t j = 0; j < nonReflCount; ++j) {
                        uint32_t drawDataIdx = faceObjectOffset + j;
                        uint32_t srcIdx = nonReflectiveIndices[j];
                        cmd->SetGraphicsRoot32BitConstant(app_slots::rootDrawIndex, drawDataIdx, 0);
                        cmd->DrawIndexedInstanced(
                            scene.drawCmds[srcIdx].indexCount, 1,
                            scene.drawCmds[srcIdx].indexOffset,
                            static_cast<INT>(scene.drawCmds[srcIdx].vertexOffset), 0
                        );
                    }
                }
            }
        );
    }

    // --- G-Buffer Pass (formerly Normal Pre-pass) ---
    // G-Buffer pass PerPassCB (index 8)
    PerPassCB* gbufferPass = getPassMapped(8);
    gbufferPass->viewProj = viewProj;
    gbufferPass->prevViewProj = prevViewProj;
    gbufferPass->cameraPos = cameraPos;

    renderGraph.addPass(
        "G-Buffer Pass",
        [&](rg::RenderGraphBuilder& builder) {
            builder.writeRenderTarget(hNormalRT, gbuffer.getRtv(GBuffer::Normal));
            builder.writeRenderTarget(hAlbedoRT, gbuffer.getRtv(GBuffer::Albedo));
            builder.writeRenderTarget(hMaterialRT, gbuffer.getRtv(GBuffer::Material));
            builder.writeRenderTarget(hMotionRT, gbuffer.getRtv(GBuffer::Motion));
            builder.writeDepthStencil(hDepthBuffer, dsvHeap->GetCPUDescriptorHandleForHeapStart());
        },
        [&, gbufferPassAddr = getPassCBAddress(8)](
            ID3D12GraphicsCommandList2* cmd, rg::RenderGraphBuilder& builder
        ) {
            PROFILE_ZONE_NAMED("G-Buffer Pass");
            PROFILE_GPU_ZONE(g_tracyD3d12Ctx, cmd, "GPU: G-Buffer");

            D3D12_CPU_DESCRIPTOR_HANDLE rtvs[] = { gbuffer.getRtv(GBuffer::Normal),
                                                   gbuffer.getRtv(GBuffer::Albedo),
                                                   gbuffer.getRtv(GBuffer::Material),
                                                   gbuffer.getRtv(GBuffer::Motion) };
            auto dsv = this->dsvHeap->GetCPUDescriptorHandleForHeapStart();

            FLOAT clearNormal[] = { 0.5f, 0.5f, 1.0f, 1.0f };
            FLOAT clearZero[] = { 0, 0, 0, 0 };
            cmd->ClearRenderTargetView(rtvs[0], clearNormal, 0, nullptr);
            cmd->ClearRenderTargetView(rtvs[1], clearZero, 0, nullptr);
            cmd->ClearRenderTargetView(rtvs[2], clearZero, 0, nullptr);
            cmd->ClearRenderTargetView(rtvs[3], clearZero, 0, nullptr);
            this->clearDepth(cmd, dsv);

            cmd->SetPipelineState(this->gbufferPSO.Get());
            cmd->SetGraphicsRootSignature(this->rootSignature.Get());
            bindSharedGeometry(cmd);
            cmd->OMSetRenderTargets(4, rtvs, false, &dsv);
            bindSceneHeapAndObjects(cmd);
            bindPerFrameAndPass(cmd, gbufferPassAddr);

            for (const auto& dc : sceneDrawCmds) {
                cmd->SetGraphicsRoot32BitConstant(app_slots::rootDrawIndex, dc.baseDrawIndex, 0);
                cmd->DrawIndexedInstanced(
                    dc.indexCount, dc.instanceCount, dc.indexOffset,
                    static_cast<INT>(dc.vertexOffset), 0
                );
            }
        }
    );

    if (ssao.enabled) {
        renderGraph.addPass(
            "SSAO Pass",
            [&](rg::RenderGraphBuilder& builder) {
                builder.readTexture(hNormalRT);
                builder.readTexture(hDepthBuffer);
            },
            [&](ID3D12GraphicsCommandList2* cmd, rg::RenderGraphBuilder& builder) {
                PROFILE_ZONE_NAMED("SSAO Pass");
                PROFILE_GPU_ZONE(g_tracyD3d12Ctx, cmd, "GPU: SSAO");
                ssao.render(cmd, this->cam.view(), this->cam.proj(), clientWidth, clientHeight);
            }
        );
    }

    // --- Scene Pass ---
    uint32_t currentVertexCount = 0;
    uint32_t currentDrawCalls = 0;
    renderGraph.addPass(
        "Scene Pass",
        [&](rg::RenderGraphBuilder& builder) {
            builder.writeRenderTarget(
                hHdrRT, bloom.bloomRtvHeap->GetCPUDescriptorHandleForHeapStart()
            );
            builder.writeDepthStencil(hDepthBuffer, dsvHeap->GetCPUDescriptorHandleForHeapStart());
            builder.readTexture(hShadowMap);
            builder.readTexture(hCubemap);
            // SSAO blur RT is handled internally by SSAO for now, but we read it in shader
        },
        [&](ID3D12GraphicsCommandList2* cmd, rg::RenderGraphBuilder& builder) {
            PROFILE_ZONE_NAMED("Scene Pass");
            PROFILE_GPU_ZONE(g_tracyD3d12Ctx, cmd, "GPU: Scene");
            // Clear to black — Rayleigh sky fills background in composite pass
            FLOAT clearColor[] = { 0, 0, 0, 1 };
            auto hdrRtv = bloom.bloomRtvHeap->GetCPUDescriptorHandleForHeapStart();
            cmd->ClearRenderTargetView(hdrRtv, clearColor, 0, nullptr);
            auto dsv = this->dsvHeap->GetCPUDescriptorHandleForHeapStart();
            this->clearDepth(cmd, dsv);

            cmd->SetPipelineState(this->pipelineState.Get());
            cmd->SetGraphicsRootSignature(this->rootSignature.Get());
            bindSharedGeometry(cmd);
            cmd->OMSetRenderTargets(1, &hdrRtv, true, &dsv);
            bindSceneHeapAndObjects(cmd);
            bindPerFrameAndPass(cmd, getPassCBAddress(0));

            CD3DX12_GPU_DESCRIPTOR_HANDLE shadowSrvHandle(
                scene.sceneSrvHeap->GetGPUDescriptorHandleForHeapStart(),
                static_cast<INT>(app_slots::srvSlotShadow), scene.sceneSrvDescSize
            );
            cmd->SetGraphicsRootDescriptorTable(app_slots::rootShadowSrv, shadowSrvHandle);

            CD3DX12_GPU_DESCRIPTOR_HANDLE cubemapSrvHandle(
                scene.sceneSrvHeap->GetGPUDescriptorHandleForHeapStart(),
                static_cast<INT>(app_slots::srvSlotCubemap), scene.sceneSrvDescSize
            );
            cmd->SetGraphicsRootDescriptorTable(app_slots::rootCubemapSrv, cubemapSrvHandle);

            CD3DX12_GPU_DESCRIPTOR_HANDLE ssaoSrvHandle(
                scene.sceneSrvHeap->GetGPUDescriptorHandleForHeapStart(),
                static_cast<INT>(app_slots::srvSlotSsao), scene.sceneSrvDescSize
            );
            cmd->SetGraphicsRootDescriptorTable(app_slots::rootSsaoSrv, ssaoSrvHandle);

            cmd->OMSetStencilRef(1);
            for (uint32_t i = 0; i < static_cast<uint32_t>(sceneDrawCmds.size()); ++i) {
                currentVertexCount += sceneDrawCmds[i].indexCount * sceneDrawCmds[i].instanceCount;
                currentDrawCalls++;
                cmd->SetGraphicsRoot32BitConstant(
                    app_slots::rootDrawIndex, sceneDrawCmds[i].baseDrawIndex, 0
                );
                cmd->DrawIndexedInstanced(
                    sceneDrawCmds[i].indexCount, sceneDrawCmds[i].instanceCount,
                    sceneDrawCmds[i].indexOffset, static_cast<INT>(sceneDrawCmds[i].vertexOffset), 0
                );
            }
        }
    );

    // --- Gizmo Pass (renders on top of scene geometry) ---
    if (selectedEntity.is_alive() && !gizmoDrawCmds.empty()) {
        renderGraph.addPass(
            "Gizmo Pass",
            [&](rg::RenderGraphBuilder& builder) {
                builder.writeRenderTarget(
                    hHdrRT, bloom.bloomRtvHeap->GetCPUDescriptorHandleForHeapStart()
                );
                builder.writeDepthStencil(
                    hDepthBuffer, dsvHeap->GetCPUDescriptorHandleForHeapStart()
                );
            },
            [&, scenePassAddr = getPassCBAddress(0)](
                ID3D12GraphicsCommandList2* cmd, rg::RenderGraphBuilder& builder
            ) {
                PROFILE_ZONE_NAMED("Gizmo Pass");
                auto dsv = this->dsvHeap->GetCPUDescriptorHandleForHeapStart();
                this->clearDepth(cmd, dsv);  // clear depth so gizmo renders on top
                auto hdrRtv = bloom.bloomRtvHeap->GetCPUDescriptorHandleForHeapStart();

                cmd->SetPipelineState(this->pipelineState.Get());
                cmd->SetGraphicsRootSignature(this->rootSignature.Get());
                bindSharedGeometry(cmd);
                cmd->OMSetRenderTargets(1, &hdrRtv, true, &dsv);
                bindSceneHeapAndObjects(cmd);
                bindPerFrameAndPass(cmd, scenePassAddr);

                CD3DX12_GPU_DESCRIPTOR_HANDLE shadowSrvHandle(
                    scene.sceneSrvHeap->GetGPUDescriptorHandleForHeapStart(),
                    static_cast<INT>(app_slots::srvSlotShadow), scene.sceneSrvDescSize
                );
                cmd->SetGraphicsRootDescriptorTable(app_slots::rootShadowSrv, shadowSrvHandle);

                CD3DX12_GPU_DESCRIPTOR_HANDLE cubemapSrvHandle(
                    scene.sceneSrvHeap->GetGPUDescriptorHandleForHeapStart(),
                    static_cast<INT>(app_slots::srvSlotCubemap), scene.sceneSrvDescSize
                );
                cmd->SetGraphicsRootDescriptorTable(app_slots::rootCubemapSrv, cubemapSrvHandle);

                CD3DX12_GPU_DESCRIPTOR_HANDLE ssaoSrvHandle(
                    scene.sceneSrvHeap->GetGPUDescriptorHandleForHeapStart(),
                    static_cast<INT>(app_slots::srvSlotSsao), scene.sceneSrvDescSize
                );
                cmd->SetGraphicsRootDescriptorTable(app_slots::rootSsaoSrv, ssaoSrvHandle);

                for (const auto& dc : gizmoDrawCmds) {
                    cmd->SetGraphicsRoot32BitConstant(
                        app_slots::rootDrawIndex, dc.baseDrawIndex, 0
                    );
                    cmd->DrawIndexedInstanced(
                        dc.indexCount, dc.instanceCount, dc.indexOffset,
                        static_cast<INT>(dc.vertexOffset), 0
                    );
                }
            }
        );
    }

    // --- Grid Pass ---
    if (showGrid) {
        // Write grid CB into perPass slot 10
        struct GridCB
        {
            mat4 viewProj;
            mat4 invViewProj;
            vec4 cameraPos;
            vec4 gridParams;
        };
        static_assert(sizeof(GridCB) <= 256, "GridCB must fit in one perPass slot");
        auto* gridCB = reinterpret_cast<GridCB*>(
            reinterpret_cast<uint8_t*>(passMappedBase) + 10 * passCBStride
        );
        gridCB->viewProj = viewProj;
        gridCB->invViewProj = mat4(XMMatrixInverse(nullptr, XMLoadFloat4x4(&viewProj)));
        gridCB->cameraPos = cameraPos;
        gridCB->gridParams = vec4(
            std::max(0.1f, gridMajorSize), static_cast<float>(std::clamp(gridSubdivisions, 1, 128)),
            0.0f, 0.0f
        );

        renderGraph.addPass(
            "Grid Pass",
            [&](rg::RenderGraphBuilder& builder) {
                builder.writeRenderTarget(
                    hHdrRT, bloom.bloomRtvHeap->GetCPUDescriptorHandleForHeapStart()
                );
                builder.readTexture(hDepthBuffer, D3D12_RESOURCE_STATE_DEPTH_READ);
            },
            [&](ID3D12GraphicsCommandList2* cmd, rg::RenderGraphBuilder& builder) {
                auto hdrRtv = bloom.bloomRtvHeap->GetCPUDescriptorHandleForHeapStart();
                auto dsv = this->dsvHeap->GetCPUDescriptorHandleForHeapStart();
                cmd->SetPipelineState(gridPSO.Get());
                cmd->SetGraphicsRootSignature(gridRootSig.Get());
                cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                cmd->RSSetViewports(1, &this->viewport);
                cmd->RSSetScissorRects(1, &this->scissorRect);
                cmd->OMSetRenderTargets(1, &hdrRtv, true, &dsv);
                cmd->SetGraphicsRootConstantBufferView(0, getPassCBAddress(10));
                cmd->DrawInstanced(3, 1, 0, 0);
            }
        );
    }

    // --- Outline Pass (skip gizmo entities) ---
    flecs::entity outlineHovered =
        (hoveredEntity && !gizmo.isGizmoEntity(hoveredEntity)) ? hoveredEntity : flecs::entity{};
    if (outlineHovered || selectedEntity) {
        renderGraph.addPass(
            "Outline Pass",
            [&](rg::RenderGraphBuilder& builder) {
                builder.writeRenderTarget(
                    hHdrRT, bloom.bloomRtvHeap->GetCPUDescriptorHandleForHeapStart()
                );
                builder.readTexture(hDepthBuffer, D3D12_RESOURCE_STATE_DEPTH_READ);
            },
            [&](ID3D12GraphicsCommandList2* cmd, rg::RenderGraphBuilder& builder) {
                PROFILE_ZONE_NAMED("Outline Pass");
                auto dsv = this->dsvHeap->GetCPUDescriptorHandleForHeapStart();
                auto hdrRtv = bloom.bloomRtvHeap->GetCPUDescriptorHandleForHeapStart();

                auto perFrameAddr = scene.perFrameBuffer[curBackBufIdx]->GetGPUVirtualAddress();
                auto perPassAddr = getPassCBAddress(0);

                OutlineRenderContext outlineCtx{};
                outlineCtx.rootSig = this->rootSignature.Get();
                outlineCtx.vbv = &scene.megaVBV;
                outlineCtx.ibv = &scene.megaIBV;
                outlineCtx.sceneSrvHeap = scene.sceneSrvHeap.Get();
                outlineCtx.srvDescSize = scene.sceneSrvDescSize;
                outlineCtx.curBackBufIdx = curBackBufIdx;
                outlineCtx.perFrameAddr = perFrameAddr;
                outlineCtx.perPassAddr = perPassAddr;
                outlineCtx.hdrRtv = hdrRtv;
                outlineCtx.dsv = dsv;
                outlineCtx.viewport = &this->viewport;
                outlineCtx.scissorRect = &this->scissorRect;

                outline.render(
                    cmd, outlineCtx, scene.drawCmds, scene.drawIndexToEntity, outlineHovered,
                    selectedEntity
                );
            }
        );
    }

    // --- ID Pass ---
    if (!runtimeConfig.hideWindow) {
        renderGraph.addPass(
            "ID Pass",
            [&](rg::RenderGraphBuilder& builder) {
                // ID pass PerPassCB (index 9)
                PerPassCB* idPass = getPassMapped(9);
                idPass->viewProj = viewProj;
                idPass->cameraPos = cameraPos;
            },
            [&, idPassAddr = getPassCBAddress(9)](
                ID3D12GraphicsCommandList2* cmd, rg::RenderGraphBuilder& builder
            ) {
                PROFILE_ZONE_NAMED("ID Pass");
                auto idRtv = picker.getRTV();
                auto idDsv = picker.getDSV();
                FLOAT clearColor[] = { 0.0f, 0.0f, 0.0f, 0.0f };
                cmd->ClearRenderTargetView(idRtv, clearColor, 0, nullptr);
                cmd->ClearDepthStencilView(idDsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

                cmd->SetPipelineState(picker.pso.Get());
                cmd->SetGraphicsRootSignature(this->rootSignature.Get());
                bindSharedGeometry(cmd);
                cmd->OMSetRenderTargets(1, &idRtv, true, &idDsv);
                bindSceneHeapAndObjects(cmd);
                bindPerFrameAndPass(cmd, idPassAddr);

                for (const auto& dc : scene.drawCmds) {
                    cmd->SetGraphicsRoot32BitConstant(
                        app_slots::rootDrawIndex, dc.baseDrawIndex, 0
                    );
                    cmd->DrawIndexedInstanced(
                        dc.indexCount, dc.instanceCount, dc.indexOffset,
                        static_cast<INT>(dc.vertexOffset), 0
                    );
                }
                picker.copyPickedPixel(
                    cmd, static_cast<uint32_t>(mousePos.x), static_cast<uint32_t>(mousePos.y)
                );
            }
        );
    }

    // --- Billboards Pass ---
    if (showLightBillboards) {
        renderGraph.addPass(
            "Billboards Pass",
            [&](rg::RenderGraphBuilder& builder) {
                builder.writeRenderTarget(
                    hHdrRT, bloom.bloomRtvHeap->GetCPUDescriptorHandleForHeapStart()
                );
                builder.readTexture(hDepthBuffer, D3D12_RESOURCE_STATE_DEPTH_READ);
            },
            [&](ID3D12GraphicsCommandList2* cmd, rg::RenderGraphBuilder& builder) {
                PROFILE_ZONE_NAMED("Billboards Pass");
                auto dsv = this->dsvHeap->GetCPUDescriptorHandleForHeapStart();
                auto hdrRtv = bloom.bloomRtvHeap->GetCPUDescriptorHandleForHeapStart();
                cmd->RSSetViewports(1, &this->viewport);
                cmd->RSSetScissorRects(1, &this->scissorRect);
                cmd->OMSetRenderTargets(1, &hdrRtv, true, &dsv);
                billboards.render(cmd, viewProj, vec3(cameraPos.x, cameraPos.y, cameraPos.z));
            }
        );
    }

    // --- Bloom + Composite Pass ---
    renderGraph.addPass(
        "Bloom Pass",
        [&](rg::RenderGraphBuilder& builder) {
            builder.readTexture(hHdrRT);
            builder.writeRenderTarget(
                hBackBuffer, CD3DX12_CPU_DESCRIPTOR_HANDLE(
                                 rtvHeap->GetCPUDescriptorHandleForHeapStart(),
                                 static_cast<INT>(curBackBufIdx), rtvDescSize
                             )
            );
        },
        [&](ID3D12GraphicsCommandList2* cmd, rg::RenderGraphBuilder& builder) {
            PROFILE_ZONE_NAMED("Bloom Pass");
            PROFILE_GPU_ZONE(g_tracyD3d12Ctx, cmd, "GPU: Bloom");

            BloomRenderer::SkyParams skyParams;
            vec3 camPos3(camX, camY, camZ);
            vec3 target3(0.0f, 0.0f, 0.0f);
            skyParams.camForward = normalize(
                vec3(target3.x - camPos3.x, target3.y - camPos3.y, target3.z - camPos3.z)
            );
            vec3 worldUp(0.0f, 1.0f, 0.0f);
            skyParams.camRight = normalize(cross(worldUp, skyParams.camForward));
            skyParams.camUp = cross(skyParams.camForward, skyParams.camRight);
            skyParams.sunDir = normalize(-dirLightDir);
            skyParams.aspectRatio =
                static_cast<float>(clientWidth) / static_cast<float>(clientHeight);
            skyParams.tanHalfFov = std::tan(cam.fov * 0.5f);
            skyParams.time = lightTime;

            CD3DX12_CPU_DESCRIPTOR_HANDLE backBufRtv(
                rtvHeap->GetCPUDescriptorHandleForHeapStart(), static_cast<INT>(curBackBufIdx),
                rtvDescSize
            );
            bloom.render(
                cmd, backBuffer, backBufRtv, clientWidth, clientHeight, bloomThreshold,
                bloomIntensity, tonemapMode, skyParams
            );
        }
    );

    // --- ImGui Pass ---
    if (!this->runtimeConfig.skipImGui) {
        renderGraph.addPass(
            "ImGui Pass",
            [&](rg::RenderGraphBuilder& builder) {
                builder.writeRenderTarget(
                    hBackBuffer, CD3DX12_CPU_DESCRIPTOR_HANDLE(
                                     rtvHeap->GetCPUDescriptorHandleForHeapStart(),
                                     static_cast<INT>(curBackBufIdx), rtvDescSize
                                 )
                );
            },
            [&](ID3D12GraphicsCommandList2* cmd, rg::RenderGraphBuilder& builder) {
                PROFILE_ZONE_NAMED("ImGui Pass");
                this->renderImGui(cmd);
            }
        );
    }

    // --- Present Pass (Transition back to PRESENT) ---
    renderGraph.addPass(
        "Present Pass",
        [&](rg::RenderGraphBuilder& builder) {
            builder.readTexture(hBackBuffer, D3D12_RESOURCE_STATE_PRESENT);
        },
        [&](ID3D12GraphicsCommandList2* cmd, rg::RenderGraphBuilder& builder) {
            // Just for transition
        }
    );

    // Execute the graph
    renderGraph.execute(cmdList.Get());

    this->lastFrameVertexCount = currentVertexCount;
    this->lastFrameDrawCalls = currentDrawCalls;

    uint64_t submitFenceValue = this->cmdQueue.execCmdList(cmdList);
    this->frameFenceValues[this->curBackBufIdx] = submitFenceValue;
    picker.setPendingReadbackFence(submitFenceValue);
    // Signal Tracy's GPU fence AFTER submitting the command list so it comes after
    // all GPU zones in the queue, ensuring Collect() reads valid timestamp data.
    PROFILE_GPU_NEW_FRAME(g_tracyD3d12Ctx);

    const bool presentVsync = this->vsync && !this->runtimeConfig.skipImGui;
    this->gfxSwapChain->present(presentVsync);
    this->curBackBufIdx = this->gfxSwapChain->currentIndex();
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
    this->prevViewProj = viewProj;
}
