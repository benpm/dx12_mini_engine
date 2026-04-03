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

    // --- Fill structured buffer (scene + shadow draw data) ---
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
        scb.shadowBias = shadow.bias;
        scb.shadowMapTexelSize = 1.0f / static_cast<float>(ShadowRenderer::mapSize);
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
    if (shadow.enabled) {
        for (uint32_t i = 0; i < totalSlots; ++i) {
            SceneConstantBuffer& shadowScb = mapped[totalSlots + i];
            shadowScb.model = mapped[i].model;
            shadowScb.viewProj = lightViewProj;
        }
    }

    this->lastFrameObjectCount = totalSlots;

    // --- Render Graph Setup ---
    renderGraph.reset();
    auto hBackBuffer =
        renderGraph.importTexture("BackBuffer", backBuffer.Get(), D3D12_RESOURCE_STATE_PRESENT);
    auto hDepthBuffer = renderGraph.importTexture(
        "MainDepth", depthBuffer.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE
    );
    auto hShadowMap = renderGraph.importTexture(
        "ShadowMap", shadow.shadowMap.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
    );
    auto hNormalRT = renderGraph.importTexture(
        "NormalRT", ssao.normalRT.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
    );
    auto hHdrRT = renderGraph.importTexture(
        "HdrRT", bloom.hdrRenderTarget.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
    );
    auto hCubemap = renderGraph.importTexture(
        "Cubemap", cubemapTexture.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
    );

    // --- Shadow pass ---
    if (shadow.enabled) {
        renderGraph.addPass(
            "Shadow Pass",
            [&](rg::RenderGraphBuilder& builder) {
                builder.writeDepthStencil(
                    hShadowMap, shadow.dsvHeap->GetCPUDescriptorHandleForHeapStart()
                );
            },
            [&](ID3D12GraphicsCommandList2* cmd, rg::RenderGraphBuilder& builder) {
                PROFILE_ZONE_NAMED("Shadow Pass");
                PROFILE_GPU_ZONE(g_tracyD3d12Ctx, cmd, "GPU: Shadow");
                cmd->SetGraphicsRootSignature(this->rootSignature.Get());
                shadow.render(
                    cmd, scene.megaVBV, scene.megaIBV, scene.sceneSrvHeap.Get(),
                    scene.sceneSrvDescSize, curBackBufIdx, drawCmds, totalSlots
                );
            }
        );
    }

    // --- Cubemap pass ---
    if (cubemapEnabled && anyReflective) {
        renderGraph.addPass(
            "Cubemap Pass",
            [&](rg::RenderGraphBuilder& builder) {
                builder.writeRenderTarget(
                    hCubemap, cubemapRtvHeap->GetCPUDescriptorHandleForHeapStart()
                );
                builder.readTexture(hShadowMap);
            },
            [&, totalSlots, reflectivePos](ID3D12GraphicsCommandList2* cmd,
                                          rg::RenderGraphBuilder& builder) {
                PROFILE_ZONE_NAMED("Cubemap Pass");

                uint32_t cubemapBaseIdx = 2 * totalSlots;
                std::vector<uint32_t> nonReflectiveIndices;
                for (uint32_t i = 0; i < totalSlots; ++i) {
                    if (mapped[i].reflective < 0.5f) {
                        nonReflectiveIndices.push_back(i);
                    }
                }
                uint32_t nonReflCount = static_cast<uint32_t>(nonReflectiveIndices.size());
                if (nonReflCount == 0) {
                    return;
                }

                // Build 6 cubemap face view-projection matrices (LH, 90° FOV)
                XMVECTOR eyePos =
                    XMVectorSet(reflectivePos.x, reflectivePos.y, reflectivePos.z, 1.0f);
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

                D3D12_VIEWPORT cubeVP = {
                    0, 0, (float)cubemapResolution, (float)cubemapResolution, 0, 1
                };
                D3D12_RECT cubeScissor = { 0, 0, (LONG)cubemapResolution, (LONG)cubemapResolution };
                UINT rtvSize =
                    device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
                UINT dsvSize =
                    device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

                cmd->SetPipelineState(this->pipelineState.Get());
                cmd->SetGraphicsRootSignature(this->rootSignature.Get());
                cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                cmd->IASetVertexBuffers(0, 1, &scene.megaVBV);
                cmd->IASetIndexBuffer(&scene.megaIBV);

                ID3D12DescriptorHeap* sceneHeaps[] = { scene.sceneSrvHeap.Get() };
                cmd->SetDescriptorHeaps(1, sceneHeaps);
                CD3DX12_GPU_DESCRIPTOR_HANDLE srvGpuHandle(
                    scene.sceneSrvHeap->GetGPUDescriptorHandleForHeapStart(),
                    static_cast<INT>(curBackBufIdx), scene.sceneSrvDescSize
                );
                cmd->SetGraphicsRootDescriptorTable(0, srvGpuHandle);
                CD3DX12_GPU_DESCRIPTOR_HANDLE shadowSrv(
                    scene.sceneSrvHeap->GetGPUDescriptorHandleForHeapStart(),
                    static_cast<INT>(Scene::nBuffers), scene.sceneSrvDescSize
                );
                cmd->SetGraphicsRootDescriptorTable(2, shadowSrv);
                CD3DX12_GPU_DESCRIPTOR_HANDLE cubeSrv(
                    scene.sceneSrvHeap->GetGPUDescriptorHandleForHeapStart(),
                    static_cast<INT>(Scene::nBuffers + 1), scene.sceneSrvDescSize
                );
                cmd->SetGraphicsRootDescriptorTable(3, cubeSrv);

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
                    this->clearRTV(cmd, faceRtv, clearColor);
                    this->clearDepth(cmd, faceDsv);
                    cmd->RSSetViewports(1, &cubeVP);
                    cmd->RSSetScissorRects(1, &cubeScissor);
                    cmd->OMSetRenderTargets(1, &faceRtv, true, &faceDsv);

                    uint32_t faceOffset = cubemapBaseIdx + face * nonReflCount;
                    for (uint32_t j = 0; j < nonReflCount; ++j) {
                        uint32_t drawDataIdx = faceOffset + j;
                        uint32_t srcIdx = nonReflectiveIndices[j];
                        cmd->SetGraphicsRoot32BitConstant(1, drawDataIdx, 0);
                        cmd->DrawIndexedInstanced(
                            drawCmds[srcIdx].indexCount, 1, drawCmds[srcIdx].indexOffset,
                            static_cast<INT>(drawCmds[srcIdx].vertexOffset), 0
                        );
                    }
                }
            }
        );
    }

    // --- Normal Pre-pass ---
    if (ssao.enabled) {
        renderGraph.addPass(
            "Normal Pre-pass",
            [&](rg::RenderGraphBuilder& builder) {
                builder.writeRenderTarget(hNormalRT, ssao.normalRtvCpu());
                builder.writeDepthStencil(
                    hDepthBuffer, dsvHeap->GetCPUDescriptorHandleForHeapStart()
                );
            },
            [&](ID3D12GraphicsCommandList2* cmd, rg::RenderGraphBuilder& builder) {
                PROFILE_ZONE_NAMED("Normal Pre-pass");
                PROFILE_GPU_ZONE(g_tracyD3d12Ctx, cmd, "GPU: Normal Pre-pass");
                auto normalRtv = ssao.normalRtvCpu();
                auto dsv = this->dsvHeap->GetCPUDescriptorHandleForHeapStart();
                FLOAT clearNormal[] = { 0.5f, 0.5f, 1.0f, 1.0f };
                cmd->ClearRenderTargetView(normalRtv, clearNormal, 0, nullptr);
                this->clearDepth(cmd, dsv);

                cmd->SetPipelineState(this->normalPSO.Get());
                cmd->SetGraphicsRootSignature(this->rootSignature.Get());
                cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                cmd->RSSetViewports(1, &this->viewport);
                cmd->RSSetScissorRects(1, &this->scissorRect);
                cmd->OMSetRenderTargets(1, &normalRtv, true, &dsv);
                cmd->IASetVertexBuffers(0, 1, &scene.megaVBV);
                cmd->IASetIndexBuffer(&scene.megaIBV);

                ID3D12DescriptorHeap* heaps[] = { scene.sceneSrvHeap.Get() };
                cmd->SetDescriptorHeaps(1, heaps);
                CD3DX12_GPU_DESCRIPTOR_HANDLE srvGpuHandle(
                    scene.sceneSrvHeap->GetGPUDescriptorHandleForHeapStart(),
                    static_cast<INT>(curBackBufIdx), scene.sceneSrvDescSize
                );
                cmd->SetGraphicsRootDescriptorTable(0, srvGpuHandle);

                for (const auto& dc : drawCmds) {
                    cmd->SetGraphicsRoot32BitConstant(1, dc.baseDrawIndex, 0);
                    cmd->DrawIndexedInstanced(
                        dc.indexCount, dc.instanceCount, dc.indexOffset,
                        static_cast<INT>(dc.vertexOffset), 0
                    );
                }
            }
        );

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
            FLOAT clearColor[] = { bgColor.x, bgColor.y, bgColor.z, 1.0f };
            auto hdrRtv = bloom.bloomRtvHeap->GetCPUDescriptorHandleForHeapStart();
            cmd->ClearRenderTargetView(hdrRtv, clearColor, 0, nullptr);
            auto dsv = this->dsvHeap->GetCPUDescriptorHandleForHeapStart();
            this->clearDepth(cmd, dsv);

            cmd->SetPipelineState(this->pipelineState.Get());
            cmd->SetGraphicsRootSignature(this->rootSignature.Get());
            cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            cmd->RSSetViewports(1, &this->viewport);
            cmd->RSSetScissorRects(1, &this->scissorRect);
            cmd->OMSetRenderTargets(1, &hdrRtv, true, &dsv);
            cmd->IASetVertexBuffers(0, 1, &scene.megaVBV);
            cmd->IASetIndexBuffer(&scene.megaIBV);

            ID3D12DescriptorHeap* heaps[] = { scene.sceneSrvHeap.Get() };
            cmd->SetDescriptorHeaps(1, heaps);
            CD3DX12_GPU_DESCRIPTOR_HANDLE srvGpuHandle(
                scene.sceneSrvHeap->GetGPUDescriptorHandleForHeapStart(),
                static_cast<INT>(curBackBufIdx), scene.sceneSrvDescSize
            );
            cmd->SetGraphicsRootDescriptorTable(0, srvGpuHandle);

            CD3DX12_GPU_DESCRIPTOR_HANDLE shadowSrvHandle(
                scene.sceneSrvHeap->GetGPUDescriptorHandleForHeapStart(),
                static_cast<INT>(Scene::nBuffers), scene.sceneSrvDescSize
            );
            cmd->SetGraphicsRootDescriptorTable(2, shadowSrvHandle);

            CD3DX12_GPU_DESCRIPTOR_HANDLE cubemapSrvHandle(
                scene.sceneSrvHeap->GetGPUDescriptorHandleForHeapStart(),
                static_cast<INT>(Scene::nBuffers + 1), scene.sceneSrvDescSize
            );
            cmd->SetGraphicsRootDescriptorTable(3, cubemapSrvHandle);

            CD3DX12_GPU_DESCRIPTOR_HANDLE ssaoSrvHandle(
                scene.sceneSrvHeap->GetGPUDescriptorHandleForHeapStart(),
                static_cast<INT>(Scene::nBuffers + 2), scene.sceneSrvDescSize
            );
            cmd->SetGraphicsRootDescriptorTable(5, ssaoSrvHandle);

            cmd->OMSetStencilRef(1);
            for (uint32_t i = 0; i < static_cast<uint32_t>(drawCmds.size()); ++i) {
                currentVertexCount += drawCmds[i].indexCount * drawCmds[i].instanceCount;
                cmd->SetGraphicsRoot32BitConstant(1, drawCmds[i].baseDrawIndex, 0);
                cmd->DrawIndexedInstanced(
                    drawCmds[i].indexCount, drawCmds[i].instanceCount, drawCmds[i].indexOffset,
                    static_cast<INT>(drawCmds[i].vertexOffset), 0
                );
            }
        }
    );

    // --- Outline Pass ---
    if (hoveredEntity || selectedEntity) {
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
                outline.render(
                    cmd, this->rootSignature.Get(), scene.megaVBV, scene.megaIBV,
                    scene.sceneSrvHeap.Get(), scene.sceneSrvDescSize, curBackBufIdx, hdrRtv, dsv,
                    this->viewport, this->scissorRect, drawCmds, drawIndexToEntity, hoveredEntity,
                    selectedEntity
                );
            }
        );
    }

    // --- ID Pass ---
    renderGraph.addPass(
        "ID Pass",
        [&](rg::RenderGraphBuilder& builder) {
            // ID picker has its own resources, but we want to make sure we're in a good state
        },
        [&](ID3D12GraphicsCommandList2* cmd, rg::RenderGraphBuilder& builder) {
            PROFILE_ZONE_NAMED("ID Pass");
            auto idRtv = picker.getRTV();
            auto idDsv = picker.getDSV();
            FLOAT clearColor[] = { static_cast<float>(ObjectPicker::invalidID), 0.0f, 0.0f, 0.0f };
            cmd->ClearRenderTargetView(idRtv, clearColor, 0, nullptr);
            cmd->ClearDepthStencilView(idDsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

            cmd->SetPipelineState(picker.pso.Get());
            cmd->SetGraphicsRootSignature(this->rootSignature.Get());
            cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            cmd->RSSetViewports(1, &this->viewport);
            cmd->RSSetScissorRects(1, &this->scissorRect);
            cmd->OMSetRenderTargets(1, &idRtv, true, &idDsv);
            cmd->IASetVertexBuffers(0, 1, &scene.megaVBV);
            cmd->IASetIndexBuffer(&scene.megaIBV);

            ID3D12DescriptorHeap* heaps[] = { scene.sceneSrvHeap.Get() };
            cmd->SetDescriptorHeaps(1, heaps);
            CD3DX12_GPU_DESCRIPTOR_HANDLE srvGpuHandle(
                scene.sceneSrvHeap->GetGPUDescriptorHandleForHeapStart(),
                static_cast<INT>(curBackBufIdx), scene.sceneSrvDescSize
            );
            cmd->SetGraphicsRootDescriptorTable(0, srvGpuHandle);

            for (const auto& dc : drawCmds) {
                cmd->SetGraphicsRoot32BitConstant(1, dc.baseDrawIndex, 0);
                cmd->DrawIndexedInstanced(
                    dc.indexCount, dc.instanceCount, dc.indexOffset,
                    static_cast<INT>(dc.vertexOffset), 0
                );
            }
            picker.copyPickedPixel(cmd, static_cast<uint32_t>(mousePos.x),
                                   static_cast<uint32_t>(mousePos.y));
        }
    );

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
            skyParams.camForward =
                normalize(vec3(target3.x - camPos3.x, target3.y - camPos3.y, target3.z - camPos3.z));
            vec3 worldUp(0.0f, 1.0f, 0.0f);
            skyParams.camRight = normalize(cross(worldUp, skyParams.camForward));
            skyParams.camUp = cross(skyParams.camForward, skyParams.camRight);
            skyParams.sunDir = normalize(-dirLightDir);
            skyParams.aspectRatio =
                static_cast<float>(clientWidth) / static_cast<float>(clientHeight);
            skyParams.tanHalfFov = std::tan(cam.fov * 0.5f);

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
