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
#include <span>
#include <vector>
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
    {
        PROFILE_ZONE_NAMED("Occlusion Readback Poll");
        this->pollOcclusionResults();
    }

    gfx::TextureHandle backBuffer = this->backBuffers[this->curBackBufIdx];
    auto* backBufferRes = static_cast<ID3D12Resource*>(this->gfxDevice->nativeResource(backBuffer));
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

    // Build filtered draw command list (excludes gizmo arrows). Buffers are
    // Application members reused across frames to avoid per-frame heap alloc.
    sceneDrawCmds.clear();
    visibleSceneDrawCmds.clear();
    gizmoDrawCmds.clear();
    sceneDrawCmds.reserve(scene.drawCmds.size());
    visibleSceneDrawCmds.reserve(scene.drawCmds.size());
    for (const auto& dc : scene.drawCmds) {
        if (dc.baseDrawIndex < scene.isGizmoDraw.size() && scene.isGizmoDraw[dc.baseDrawIndex]) {
            gizmoDrawCmds.push_back(dc);
        } else {
            sceneDrawCmds.push_back(dc);
        }
    }
    this->lastFrameOcclusionCulled = 0;
    this->ensureOcclusionQueryResources(Scene::maxDrawsPerFrame / 3);
    for (const auto& dc : sceneDrawCmds) {
        if (this->isDrawOcclusionVisible(dc)) {
            visibleSceneDrawCmds.push_back(dc);
        } else {
            this->lastFrameOcclusionCulled++;
        }
    }

    const bool recordOcclusionQueries =
        this->occlusionCullingEnabled && this->occlusionQueryHeap && this->occlusionReadback &&
        this->occlusionPendingFence == 0 && !visibleSceneDrawCmds.empty();
    if (recordOcclusionQueries) {
        this->occlusionPendingQueryCount = 0;
        this->occlusionPendingEntityIds.clear();
    }

    PerFrameCB* frameMapped = scene.perFrameMapped[curBackBufIdx];
    PerPassCB* passMappedBase = scene.perPassMapped[curBackBufIdx];
    const uint64_t passCBStride = (sizeof(PerPassCB) + 255) & ~255;
    auto getPassMapped = [&](uint32_t passIdx) {
        uint8_t* base = reinterpret_cast<uint8_t*>(passMappedBase);
        return reinterpret_cast<PerPassCB*>(base + passIdx * passCBStride);
    };
    auto getPassCBAddress = [&](uint32_t passIdx) {
        auto* res = static_cast<ID3D12Resource*>(
            gfxDevice->nativeResource(scene.perPassBuffer[curBackBufIdx])
        );
        return res->GetGPUVirtualAddress() + passIdx * passCBStride;
    };

    auto bindSharedGeometry = [&](ID3D12GraphicsCommandList2* cmd) {
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        D3D12_VIEWPORT d3dVp{ viewport.x,      viewport.y,        viewport.width,
                              viewport.height, viewport.minDepth, viewport.maxDepth };
        D3D12_RECT d3dSr{ scissorRect.x, scissorRect.y, scissorRect.x + scissorRect.width,
                          scissorRect.y + scissorRect.height };
        cmd->RSSetViewports(1, &d3dVp);
        cmd->RSSetScissorRects(1, &d3dSr);
        D3D12_VERTEX_BUFFER_VIEW vbv{ scene.megaVBV.gpuAddress, scene.megaVBV.sizeInBytes,
                                      scene.megaVBV.strideInBytes };
        D3D12_INDEX_BUFFER_VIEW ibv{ scene.megaIBV.gpuAddress, scene.megaIBV.sizeInBytes,
                                     DXGI_FORMAT_R32_UINT };
        cmd->IASetVertexBuffers(0, 1, &vbv);
        cmd->IASetIndexBuffer(&ibv);
    };

    auto* gfxSrvHeap = static_cast<ID3D12DescriptorHeap*>(gfxDevice->srvHeapNative());
    auto* gfxSamplerHeap = static_cast<ID3D12DescriptorHeap*>(gfxDevice->samplerHeapNative());

    auto bindSceneHeapAndObjects = [&](ID3D12GraphicsCommandList2* cmd) {
        ID3D12DescriptorHeap* heaps[] = { gfxSrvHeap, gfxSamplerHeap };
        cmd->SetDescriptorHeaps(2, heaps);
        D3D12_GPU_DESCRIPTOR_HANDLE heapStart;
        heapStart.ptr = gfxDevice->srvGpuDescriptorHandle(0);
        cmd->SetGraphicsRootDescriptorTable(app_slots::bindlessSrvTable, heapStart);
        D3D12_GPU_DESCRIPTOR_HANDLE samplerHeapStart;
        samplerHeapStart.ptr = gfxDevice->samplerGpuDescriptorHandle(0);
        cmd->SetGraphicsRootDescriptorTable(app_slots::bindlessSamplerTable, samplerHeapStart);
    };

    auto bindPerFrameAndPass = [&](ID3D12GraphicsCommandList2* cmd,
                                   D3D12_GPU_VIRTUAL_ADDRESS perPassAddr) {
        auto* perFrameRes = static_cast<ID3D12Resource*>(
            gfxDevice->nativeResource(scene.perFrameBuffer[curBackBufIdx])
        );
        auto perFrameAddr = perFrameRes->GetGPUVirtualAddress();
        cmd->SetGraphicsRootConstantBufferView(app_slots::bindlessPerFrameCB, perFrameAddr);
        cmd->SetGraphicsRootConstantBufferView(app_slots::bindlessPerPassCB, perPassAddr);
    };

    // Standard pass setup: bind PSO (auto-binds matching root sig), then the
    // shared per-frame state — geometry buffers + viewport/scissor, bindless
    // SRV/sampler tables, per-frame and per-pass CBVs. Render targets are
    // pass-specific and bound by the caller.
    auto beginPass = [&](gfx::ICommandList& cmdRef,
                         gfx::PipelineHandle pso,
                         D3D12_GPU_VIRTUAL_ADDRESS perPassAddr) {
        auto* cmd = static_cast<ID3D12GraphicsCommandList2*>(cmdRef.nativeHandle());
        cmdRef.bindPipeline(pso);
        bindSharedGeometry(cmd);
        bindSceneHeapAndObjects(cmd);
        bindPerFrameAndPass(cmd, perPassAddr);
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
        renderGraph.importTexture("BackBuffer", backBuffer, gfx::ResourceState::Present);
    auto hDepthBuffer =
        renderGraph.importTexture("MainDepth", depthBuffer, gfx::ResourceState::DepthWrite);
    auto hShadowMap = renderGraph.importTexture(
        "ShadowMap", shadow.shadowMap, gfx::ResourceState::PixelShaderResource
    );
    auto hNormalRT = renderGraph.importTexture(
        "NormalRT", gbuffer.resources[GBuffer::Normal], gfx::ResourceState::Common
    );
    auto hAlbedoRT = renderGraph.importTexture(
        "AlbedoRT", gbuffer.resources[GBuffer::Albedo], gfx::ResourceState::Common
    );
    auto hMaterialRT = renderGraph.importTexture(
        "MaterialRT", gbuffer.resources[GBuffer::Material], gfx::ResourceState::Common
    );
    auto hMotionRT = renderGraph.importTexture(
        "MotionRT", gbuffer.resources[GBuffer::Motion], gfx::ResourceState::Common
    );
    auto hHdrRT =
        renderGraph.importTexture("HdrRT", bloom.hdrRT, gfx::ResourceState::PixelShaderResource);
    auto hCubemap = renderGraph.importTexture(
        "Cubemap", cubemapTexture, gfx::ResourceState::PixelShaderResource
    );

    // --- Shadow pass ---
    if (shadow.enabled) {
        // Shadow pass PerPassCB (index 1)
        PerPassCB* shadowPass = getPassMapped(1);
        shadowPass->viewProj = lightViewProj;
        shadowPass->cameraPos = vec4(0, 0, 0, 1);  // Not used in shadow pass

        renderGraph.addPass(
            "Shadow Pass",
            [&](rg::RenderGraphBuilder& builder) { builder.writeDepthStencil(hShadowMap); },
            [&, shadowPassAddr = getPassCBAddress(1)](
                gfx::ICommandList& cmdRef, rg::RenderGraphBuilder& builder
            ) {
                auto* cmd = static_cast<ID3D12GraphicsCommandList2*>(cmdRef.nativeHandle());
                PROFILE_ZONE_NAMED("Shadow Pass");
                PROFILE_GPU_ZONE(g_tracyD3d12Ctx, cmd, "GPU: Shadow");
                // Shadow pass binds the per-frame/per-pass CBVs BEFORE
                // shadow.render() runs (which is where bindPipeline auto-sets
                // the root sig). We need an RS bound to make the CBV bind
                // valid, so set it explicitly here.
                cmd->SetGraphicsRootSignature(
                    static_cast<ID3D12RootSignature*>(gfxDevice->bindlessRootSigNative())
                );
                bindPerFrameAndPass(cmd, shadowPassAddr);

                shadow.render(
                    cmdRef, scene.megaVBV, scene.megaIBV, scene.perObjectBuffer[curBackBufIdx],
                    sceneDrawCmds, 0
                );
            }
        );
    }

    // --- Cubemap pass ---
    if (cubemapEnabled && scene.anyReflective) {
        renderGraph.addPass(
            "Cubemap Pass",
            [&](rg::RenderGraphBuilder& builder) {
                builder.writeRenderTarget(hCubemap);
                builder.readTexture(hShadowMap);
            },
            [&](gfx::ICommandList& cmdRef, rg::RenderGraphBuilder& builder) {
                auto* cmd = static_cast<ID3D12GraphicsCommandList2*>(cmdRef.nativeHandle());
                PROFILE_ZONE_NAMED("Cubemap Pass");

                uint32_t cubemapBaseIdx =
                    scene.totalSlots;  // Place cubemap object data after main ones
                // Application member, cleared and reused per frame.
                this->nonReflectiveIndices.clear();
                for (uint32_t i = 0; i < scene.totalSlots; ++i) {
                    if (objectMapped[i].reflective < 0.5f &&
                        !(i < scene.isGizmoDraw.size() && scene.isGizmoDraw[i])) {
                        this->nonReflectiveIndices.push_back(i);
                    }
                }
                uint32_t nonReflCount = static_cast<uint32_t>(this->nonReflectiveIndices.size());
                if (nonReflCount == 0) {
                    // Fallback: capture reflective geometry too so the cubemap is not empty.
                    for (uint32_t i = 0; i < scene.totalSlots; ++i) {
                        if (!(i < scene.isGizmoDraw.size() && scene.isGizmoDraw[i])) {
                            this->nonReflectiveIndices.push_back(i);
                        }
                    }
                    nonReflCount = static_cast<uint32_t>(this->nonReflectiveIndices.size());
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
                        uint32_t srcIdx = this->nonReflectiveIndices[j];
                        PerObjectData& dst = objectMapped[faceObjectOffset + j];
                        dst = objectMapped[srcIdx];
                        dst.reflective = 0.0f;
                    }
                }

                D3D12_VIEWPORT cubeVP = { 0, 0, (float)cubemapResolution, (float)cubemapResolution,
                                          0, 1 };
                D3D12_RECT cubeScissor = { 0, 0, (LONG)cubemapResolution, (LONG)cubemapResolution };

                cmdRef.bindPipeline(this->pipelineState);
                bindSharedGeometry(cmd);
                bindSceneHeapAndObjects(cmd);

                auto perFrameAddr =
                    static_cast<ID3D12Resource*>(
                        gfxDevice->nativeResource(scene.perFrameBuffer[curBackBufIdx])
                    )
                        ->GetGPUVirtualAddress();
                cmd->SetGraphicsRootConstantBufferView(app_slots::bindlessPerFrameCB, perFrameAddr);

                BindlessIndices bi{};
                bi.drawDataIdx =
                    gfxDevice->bindlessSrvIndex(scene.perObjectBuffer[curBackBufIdx]);
                bi.shadowMapIdx = shadowSrvIdx;
                bi.envMapIdx = gfxDevice->bindlessSrvIndex(cubemapTexture);

                for (uint32_t face = 0; face < 6; ++face) {
                    D3D12_CPU_DESCRIPTOR_HANDLE faceRtv;
                    faceRtv.ptr = static_cast<SIZE_T>(gfxDevice->rtvHandle(cubemapTexture, face));
                    D3D12_CPU_DESCRIPTOR_HANDLE faceDsv;
                    faceDsv.ptr = static_cast<SIZE_T>(gfxDevice->dsvHandle(cubemapDepth, face));
                    FLOAT clearColor[] = { 0, 0, 0, 1 };
                    cmd->ClearRenderTargetView(faceRtv, clearColor, 0, nullptr);
                    cmd->ClearDepthStencilView(
                        faceDsv, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0,
                        nullptr
                    );
                    cmd->RSSetViewports(1, &cubeVP);
                    cmd->RSSetScissorRects(1, &cubeScissor);
                    cmd->OMSetRenderTargets(1, &faceRtv, true, &faceDsv);

                    cmd->SetGraphicsRootConstantBufferView(
                        app_slots::bindlessPerPassCB, getPassCBAddress(2 + face)
                    );

                    uint32_t faceObjectOffset = cubemapBaseIdx + face * nonReflCount;
                    for (uint32_t j = 0; j < nonReflCount; ++j) {
                        uint32_t srcIdx = this->nonReflectiveIndices[j];
                        bi.drawIndex = faceObjectOffset + j;
                        cmd->SetGraphicsRoot32BitConstants(app_slots::bindlessIndices, 16, &bi, 0);
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
            builder.writeRenderTarget(hNormalRT);
            builder.writeRenderTarget(hAlbedoRT);
            builder.writeRenderTarget(hMaterialRT);
            builder.writeRenderTarget(hMotionRT);
            builder.writeDepthStencil(hDepthBuffer);
        },
        [&, gbufferPassAddr =
                getPassCBAddress(8)](gfx::ICommandList& cmdRef, rg::RenderGraphBuilder& builder) {
            auto* cmd = static_cast<ID3D12GraphicsCommandList2*>(cmdRef.nativeHandle());
            PROFILE_ZONE_NAMED("G-Buffer Pass");
            PROFILE_GPU_ZONE(g_tracyD3d12Ctx, cmd, "GPU: G-Buffer");

            const float clearNormal[] = { 0.5f, 0.5f, 1.0f, 1.0f };
            const float clearZero[] = { 0, 0, 0, 0 };
            cmdRef.clearRenderTarget(gbuffer.resources[0], clearNormal);
            cmdRef.clearRenderTarget(gbuffer.resources[1], clearZero);
            cmdRef.clearRenderTarget(gbuffer.resources[2], clearZero);
            cmdRef.clearRenderTarget(gbuffer.resources[3], clearZero);
            cmdRef.clearDepthStencil(
                depthBuffer, gfx::ClearFlags::Depth | gfx::ClearFlags::Stencil, 1.0f, 0
            );

            beginPass(cmdRef, this->gbufferPSO, gbufferPassAddr);
            gfx::TextureHandle gbufferRTs[4] = {
                gbuffer.resources[0], gbuffer.resources[1], gbuffer.resources[2],
                gbuffer.resources[3]
            };
            cmdRef.setRenderTargets(
                std::span<const gfx::TextureHandle>(gbufferRTs, 4), depthBuffer
            );

            uint32_t occlusionQueryIndex = 0;
            BindlessIndices bi{};
            bi.drawDataIdx = gfxDevice->bindlessSrvIndex(scene.perObjectBuffer[curBackBufIdx]);
            for (const auto& dc : visibleSceneDrawCmds) {
                const bool queryThisDraw = recordOcclusionQueries &&
                                           occlusionQueryIndex < this->occlusionQueryCapacity &&
                                           dc.baseDrawIndex < scene.drawIndexToEntity.size();
                if (queryThisDraw) {
                    cmd->BeginQuery(
                        this->occlusionQueryHeap.Get(), D3D12_QUERY_TYPE_BINARY_OCCLUSION,
                        occlusionQueryIndex
                    );
                }
                bi.drawIndex = dc.baseDrawIndex;
                cmd->SetGraphicsRoot32BitConstants(app_slots::bindlessIndices, 16, &bi, 0);
                cmd->DrawIndexedInstanced(
                    dc.indexCount, dc.instanceCount, dc.indexOffset,
                    static_cast<INT>(dc.vertexOffset), 0
                );
                if (queryThisDraw) {
                    cmd->EndQuery(
                        this->occlusionQueryHeap.Get(), D3D12_QUERY_TYPE_BINARY_OCCLUSION,
                        occlusionQueryIndex
                    );
                    this->occlusionPendingEntityIds.push_back(
                        scene.drawIndexToEntity[dc.baseDrawIndex].id()
                    );
                    occlusionQueryIndex++;
                }
            }
            if (recordOcclusionQueries && occlusionQueryIndex > 0) {
                cmd->ResolveQueryData(
                    this->occlusionQueryHeap.Get(), D3D12_QUERY_TYPE_BINARY_OCCLUSION, 0,
                    occlusionQueryIndex, this->occlusionReadback.Get(), 0
                );
                this->occlusionPendingQueryCount = occlusionQueryIndex;
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
            [&](gfx::ICommandList& cmdRef, rg::RenderGraphBuilder& builder) {
                auto* cmd = static_cast<ID3D12GraphicsCommandList2*>(cmdRef.nativeHandle());
                PROFILE_ZONE_NAMED("SSAO Pass");
                PROFILE_GPU_ZONE(g_tracyD3d12Ctx, cmd, "GPU: SSAO");
                ssao.render(cmdRef, this->cam.view(), this->cam.proj(), clientWidth, clientHeight);
            }
        );
    }

    // --- Scene Pass ---
    uint32_t currentVertexCount = 0;
    uint32_t currentDrawCalls = 0;
    renderGraph.addPass(
        "Scene Pass",
        [&](rg::RenderGraphBuilder& builder) {
            builder.writeRenderTarget(hHdrRT);
            builder.writeDepthStencil(hDepthBuffer);
            builder.readTexture(hShadowMap);
            builder.readTexture(hCubemap);
            // SSAO blur RT is handled internally by SSAO for now, but we read it in shader
        },
        [&](gfx::ICommandList& cmdRef, rg::RenderGraphBuilder& builder) {
            auto* cmd = static_cast<ID3D12GraphicsCommandList2*>(cmdRef.nativeHandle());
            PROFILE_ZONE_NAMED("Scene Pass");
            PROFILE_GPU_ZONE(g_tracyD3d12Ctx, cmd, "GPU: Scene");
            // Clear to black — Rayleigh sky fills background in composite pass
            const float clearColor[] = { 0, 0, 0, 1 };
            cmdRef.clearRenderTarget(bloom.hdrRT, clearColor);
            cmdRef.clearDepthStencil(
                depthBuffer, gfx::ClearFlags::Depth | gfx::ClearFlags::Stencil, 1.0f, 0
            );

            beginPass(cmdRef, this->pipelineState, getPassCBAddress(0));
            gfx::TextureHandle sceneRTs[1] = { bloom.hdrRT };
            cmdRef.setRenderTargets(std::span<const gfx::TextureHandle>(sceneRTs, 1), depthBuffer);
            cmdRef.setStencilRef(1);
            BindlessIndices bi{};
            bi.drawDataIdx = gfxDevice->bindlessSrvIndex(scene.perObjectBuffer[curBackBufIdx]);
            bi.shadowMapIdx = shadowSrvIdx;
            bi.envMapIdx = gfxDevice->bindlessSrvIndex(cubemapTexture);
            bi.ssaoIdx = gfxDevice->bindlessSrvIndex(ssao.blurRT());
            bi.shadowSamplerIdx = 0;
            bi.envSamplerIdx = 1;
            for (uint32_t i = 0; i < static_cast<uint32_t>(visibleSceneDrawCmds.size()); ++i) {
                currentVertexCount +=
                    visibleSceneDrawCmds[i].indexCount * visibleSceneDrawCmds[i].instanceCount;
                currentDrawCalls++;
                bi.drawIndex = visibleSceneDrawCmds[i].baseDrawIndex;
                cmd->SetGraphicsRoot32BitConstants(app_slots::bindlessIndices, 16, &bi, 0);
                cmd->DrawIndexedInstanced(
                    visibleSceneDrawCmds[i].indexCount, visibleSceneDrawCmds[i].instanceCount,
                    visibleSceneDrawCmds[i].indexOffset,
                    static_cast<INT>(visibleSceneDrawCmds[i].vertexOffset), 0
                );
            }
        }
    );

    // --- Gizmo Pass (renders on top of scene geometry) ---
    if (selectedEntity.is_alive() && !gizmoDrawCmds.empty()) {
        renderGraph.addPass(
            "Gizmo Pass",
            [&](rg::RenderGraphBuilder& builder) {
                builder.writeRenderTarget(hHdrRT);
                builder.writeDepthStencil(hDepthBuffer);
            },
            [&, scenePassAddr = getPassCBAddress(0)](
                gfx::ICommandList& cmdRef, rg::RenderGraphBuilder& builder
            ) {
                auto* cmd = static_cast<ID3D12GraphicsCommandList2*>(cmdRef.nativeHandle());
                PROFILE_ZONE_NAMED("Gizmo Pass");
                // clear depth so gizmo renders on top
                cmdRef.clearDepthStencil(
                    depthBuffer, gfx::ClearFlags::Depth | gfx::ClearFlags::Stencil, 1.0f, 0
                );

                beginPass(cmdRef, this->pipelineState, scenePassAddr);
                gfx::TextureHandle gizmoRTs[1] = { bloom.hdrRT };
                cmdRef.setRenderTargets(
                    std::span<const gfx::TextureHandle>(gizmoRTs, 1), depthBuffer
                );

                BindlessIndices bi{};
                bi.drawDataIdx =
                    gfxDevice->bindlessSrvIndex(scene.perObjectBuffer[curBackBufIdx]);
                bi.shadowMapIdx = shadowSrvIdx;
                bi.envMapIdx = gfxDevice->bindlessSrvIndex(cubemapTexture);
                bi.ssaoIdx = gfxDevice->bindlessSrvIndex(ssao.blurRT());
                bi.shadowSamplerIdx = 0;
                bi.envSamplerIdx = 1;
                for (const auto& dc : gizmoDrawCmds) {
                    bi.drawIndex = dc.baseDrawIndex;
                    cmd->SetGraphicsRoot32BitConstants(app_slots::bindlessIndices, 16, &bi, 0);
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
                builder.writeRenderTarget(hHdrRT);
                builder.readTexture(hDepthBuffer, gfx::ResourceState::DepthRead);
            },
            [&](gfx::ICommandList& cmdRef, rg::RenderGraphBuilder& builder) {
                auto* cmd = static_cast<ID3D12GraphicsCommandList2*>(cmdRef.nativeHandle());
                // bindPipeline auto-binds the bindless root sig that gridPSO
                // is built against; no explicit SetGraphicsRootSignature needed.
                cmdRef.bindPipeline(gridPSO);
                cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                cmdRef.setViewport(viewport);
                cmdRef.setScissor(scissorRect);
                gfx::TextureHandle gridRTs[1] = { bloom.hdrRT };
                cmdRef.setRenderTargets(
                    std::span<const gfx::TextureHandle>(gridRTs, 1), depthBuffer
                );
                // Bind grid CB through the bindless PerPassCB slot (b2).
                cmd->SetGraphicsRootConstantBufferView(
                    app_slots::bindlessPerPassCB, getPassCBAddress(10)
                );
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
                builder.writeRenderTarget(hHdrRT);
                builder.readTexture(hDepthBuffer, gfx::ResourceState::DepthRead);
            },
            [&](gfx::ICommandList& cmdRef, rg::RenderGraphBuilder& builder) {
                PROFILE_ZONE_NAMED("Outline Pass");
                D3D12_CPU_DESCRIPTOR_HANDLE dsv;
                dsv.ptr = static_cast<SIZE_T>(gfxDevice->dsvHandle(depthBuffer));
                D3D12_CPU_DESCRIPTOR_HANDLE hdrRtv;
                hdrRtv.ptr = static_cast<SIZE_T>(gfxDevice->rtvHandle(bloom.hdrRT));

                auto perFrameAddr =
                    static_cast<ID3D12Resource*>(
                        gfxDevice->nativeResource(scene.perFrameBuffer[curBackBufIdx])
                    )
                        ->GetGPUVirtualAddress();
                auto perPassAddr = getPassCBAddress(0);

                OutlineRenderContext outlineCtx{};
                outlineCtx.vbv = scene.megaVBV;
                outlineCtx.ibv = scene.megaIBV;
                outlineCtx.perObjectBuffer = scene.perObjectBuffer[curBackBufIdx];
                outlineCtx.perFrameAddr = perFrameAddr;
                outlineCtx.perPassAddr = perPassAddr;
                outlineCtx.hdrRtv = hdrRtv.ptr;
                outlineCtx.dsv = dsv.ptr;
                outlineCtx.viewport = &this->viewport;
                outlineCtx.scissorRect = &this->scissorRect;

                outline.render(
                    cmdRef, outlineCtx, scene.drawCmds, scene.drawIndexToEntity, outlineHovered,
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
                gfx::ICommandList& cmdRef, rg::RenderGraphBuilder& builder
            ) {
                auto* cmd = static_cast<ID3D12GraphicsCommandList2*>(cmdRef.nativeHandle());
                PROFILE_ZONE_NAMED("ID Pass");
                D3D12_CPU_DESCRIPTOR_HANDLE idRtv{ picker.getRTV() };
                D3D12_CPU_DESCRIPTOR_HANDLE idDsv{ picker.getDSV() };
                FLOAT clearColor[] = { 0.0f, 0.0f, 0.0f, 0.0f };
                cmd->ClearRenderTargetView(idRtv, clearColor, 0, nullptr);
                cmd->ClearDepthStencilView(idDsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

                beginPass(cmdRef, picker.pso, idPassAddr);
                cmd->OMSetRenderTargets(1, &idRtv, true, &idDsv);

                BindlessIndices bi{};
                bi.drawDataIdx =
                    gfxDevice->bindlessSrvIndex(scene.perObjectBuffer[curBackBufIdx]);
                for (const auto& dc : visibleSceneDrawCmds) {
                    bi.drawIndex = dc.baseDrawIndex;
                    cmd->SetGraphicsRoot32BitConstants(app_slots::bindlessIndices, 16, &bi, 0);
                    cmd->DrawIndexedInstanced(
                        dc.indexCount, dc.instanceCount, dc.indexOffset,
                        static_cast<INT>(dc.vertexOffset), 0
                    );
                }
                picker.copyPickedPixel(
                    cmdRef, static_cast<uint32_t>(mousePos.x), static_cast<uint32_t>(mousePos.y)
                );
            }
        );
    }

    // --- Billboards Pass ---
    if (showLightBillboards) {
        renderGraph.addPass(
            "Billboards Pass",
            [&](rg::RenderGraphBuilder& builder) {
                builder.writeRenderTarget(hHdrRT);
                builder.readTexture(hDepthBuffer, gfx::ResourceState::DepthRead);
            },
            [&](gfx::ICommandList& cmdRef, rg::RenderGraphBuilder& builder) {
                auto* cmd = static_cast<ID3D12GraphicsCommandList2*>(cmdRef.nativeHandle());
                PROFILE_ZONE_NAMED("Billboards Pass");
                D3D12_CPU_DESCRIPTOR_HANDLE dsv;
                dsv.ptr = static_cast<SIZE_T>(gfxDevice->dsvHandle(depthBuffer));
                D3D12_CPU_DESCRIPTOR_HANDLE hdrRtv;
                hdrRtv.ptr = static_cast<SIZE_T>(gfxDevice->rtvHandle(bloom.hdrRT));
                {
                    D3D12_VIEWPORT d3dVp{ viewport.x,      viewport.y,        viewport.width,
                                          viewport.height, viewport.minDepth, viewport.maxDepth };
                    D3D12_RECT d3dSr{ scissorRect.x, scissorRect.y,
                                      scissorRect.x + scissorRect.width,
                                      scissorRect.y + scissorRect.height };
                    cmd->RSSetViewports(1, &d3dVp);
                    cmd->RSSetScissorRects(1, &d3dSr);
                }
                cmd->OMSetRenderTargets(1, &hdrRtv, true, &dsv);
                billboards.render(cmdRef, viewProj, vec3(cameraPos.x, cameraPos.y, cameraPos.z));
            }
        );
    }

    // --- Bloom + Composite Pass ---
    renderGraph.addPass(
        "Bloom Pass",
        [&](rg::RenderGraphBuilder& builder) {
            builder.readTexture(hHdrRT);
            builder.writeRenderTarget(hBackBuffer);
        },
        [&](gfx::ICommandList& cmdRef, rg::RenderGraphBuilder& builder) {
            auto* cmd = static_cast<ID3D12GraphicsCommandList2*>(cmdRef.nativeHandle());
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

            bloom.render(
                cmdRef, gfxDevice->rtvHandle(backBuffer), clientWidth, clientHeight, bloomThreshold,
                bloomIntensity, tonemapMode, skyParams
            );
        }
    );

    // --- ImGui Pass ---
    if (!this->runtimeConfig.skipImGui) {
        renderGraph.addPass(
            "ImGui Pass",
            [&](rg::RenderGraphBuilder& builder) { builder.writeRenderTarget(hBackBuffer); },
            [&](gfx::ICommandList& cmdRef, rg::RenderGraphBuilder& builder) {
                PROFILE_ZONE_NAMED("ImGui Pass");
                this->renderImGui(cmdRef);
            }
        );
    }

    // --- Present Pass (Transition back to PRESENT) ---
    renderGraph.addPass(
        "Present Pass",
        [&](rg::RenderGraphBuilder& builder) {
            builder.readTexture(hBackBuffer, gfx::ResourceState::Present);
        },
        [&](gfx::ICommandList& cmdRef, rg::RenderGraphBuilder& builder) {
            (void)cmdRef;
            // Just for transition
        }
    );

    // Execute the graph
    {
        // Wrap the legacy ID3D12GraphicsCommandList2 in a gfx::ICommandList
        // adapter so the render graph can speak the abstract interface.
        auto cmdAdapter = gfx::wrapNativeCommandList(gfxDevice.get(), cmdList.Get());
        renderGraph.execute(*cmdAdapter);
    }

    this->lastFrameVertexCount = currentVertexCount;
    this->lastFrameDrawCalls = currentDrawCalls;

    uint64_t submitFenceValue = this->cmdQueue.execCmdList(cmdList);
    this->frameFenceValues[this->curBackBufIdx] = submitFenceValue;
    picker.setPendingReadbackFence(submitFenceValue);
    if (this->occlusionPendingQueryCount > 0 && this->occlusionPendingFence == 0) {
        this->occlusionPendingFence = submitFenceValue;
    }
    // Signal Tracy's GPU fence AFTER submitting the command list so it comes after
    // all GPU zones in the queue, ensuring Collect() reads valid timestamp data.
    PROFILE_GPU_NEW_FRAME(g_tracyD3d12Ctx);

    const bool presentVsync = this->vsync && !this->runtimeConfig.skipImGui;
    this->gfxSwapChain->present(presentVsync);
    this->curBackBufIdx = this->gfxSwapChain->currentIndex();
    this->occlusionFrameIndex++;
    PROFILE_FRAME_MARK;

    if (this->runtimeConfig.screenshotFrame > 0) {
        this->frameCount++;
        if (this->frameCount == this->runtimeConfig.screenshotFrame) {
            spdlog::info("Saving screenshot and exiting...");
            HRESULT hr = DirectX::SaveWICTextureToFile(
                this->cmdQueue.queue.Get(), backBufferRes, GUID_ContainerFormatPng,
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
