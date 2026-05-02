module;

#if defined(__clang__)
    #define FMT_CONSTEVAL
#endif

#include <d3d12.h>
#include <DirectXCollision.h>
#include <DirectXMath.h>
#include <flecs.h>
#include <spdlog/spdlog.h>
#include <tiny_gltf.h>
#include <tiny_obj_loader.h>
#include <Windows.h>
#include <wrl.h>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <map>
#include <string>
#include <vector>
#include "d3dx12_clean.h"
#include "resource.h"

module scene;

using Microsoft::WRL::ComPtr;
using namespace DirectX;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void fillPerObject(PerObjectData& pod, const Material& mat, const vec4& albedo)
{
    pod.albedo = albedo;
    pod.roughness = mat.roughness;
    pod.metallic = mat.metallic;
    pod.emissiveStrength = mat.emissiveStrength;
    pod.reflective = mat.reflective ? 1.0f : 0.0f;
    pod.emissive = mat.emissive;
}

// ---------------------------------------------------------------------------
// Scene::populateDrawCommands
// ---------------------------------------------------------------------------

void Scene::populateDrawCommands(
    uint32_t curBackBufIdx,
    const mat4& matModel,
    const vec3& cameraPos,
    const DirectX::BoundingFrustum& frustum
)
{
    drawCmds.clear();
    drawIndexToEntity.clear();
    anyReflective = false;

    uint32_t drawIdx = 0;
    PerObjectData* objectMapped = this->perObjectMapped[curBackBufIdx];

    // Regular entities with MeshRef (LodMesh overrides mesh selection if present)
    drawQuery.each([&](flecs::entity e, const Transform& tf, const MeshRef& meshRef) {
        assert(drawIdx < Scene::maxDrawsPerFrame / 3);

        MeshRef mesh = meshRef;
        if (auto lodPtr = e.try_get<LodMesh>(); lodPtr && !lodPtr->levels.empty()) {
            vec3 pos(tf.world._41, tf.world._42, tf.world._43);
            float d2 = (pos.x - cameraPos.x) * (pos.x - cameraPos.x) +
                       (pos.y - cameraPos.y) * (pos.y - cameraPos.y) +
                       (pos.z - cameraPos.z) * (pos.z - cameraPos.z);
            mesh = lodPtr->levels.back().mesh;
            for (const auto& level : lodPtr->levels) {
                if (d2 < level.distanceThreshold * level.distanceThreshold) {
                    mesh = level.mesh;
                    break;
                }
            }
        }

        const Material& mat = materials[mesh.materialIndex];

        PerObjectData& pod = objectMapped[drawIdx];
        pod.model = tf.world * matModel;

        if (e.has<PrevTransform>()) {
            pod.prevModel = e.get<PrevTransform>().world * matModel;
        } else {
            pod.prevModel = pod.model;
        }

        vec4 albedo = mesh.albedoOverride.w > 0.0f ? mesh.albedoOverride : mat.albedo;
        fillPerObject(pod, mat, albedo);

        if (mat.reflective && !anyReflective) {
            anyReflective = true;
            reflectivePos = vec3(pod.model._41, pod.model._42, pod.model._43);
        }

        drawCmds.push_back({ mesh.indexCount, mesh.indexOffset, mesh.vertexOffset, 1, drawIdx });
        drawIndexToEntity.push_back(e);
        drawIdx++;
    });

    // Instanced groups
    instanceQuery.each([&](flecs::entity e, const Transform& /*groupTf*/,
                           const InstanceGroup& group) {
        uint32_t N = static_cast<uint32_t>(group.transforms.size());
        if (N == 0) {
            return;
        }

        assert(drawIdx + N <= Scene::maxDrawsPerFrame / 3);
        const Material& mat = materials[group.mesh.materialIndex];
        uint32_t baseIdx = drawIdx;

        const bool hasPig = e.has<PrevInstanceGroup>();

        for (uint32_t i = 0; i < N; ++i) {
            PerObjectData& pod = objectMapped[drawIdx];
            pod.model = group.transforms[i] * matModel;
            if (hasPig) {
                const auto& pig = e.get<PrevInstanceGroup>();
                if (i < pig.transforms.size()) {
                    pod.prevModel = pig.transforms[i] * matModel;
                } else {
                    pod.prevModel = pod.model;
                }
            } else {
                pod.prevModel = pod.model;
            }
            vec4 albedo = (i < group.albedoOverrides.size() && group.albedoOverrides[i].w > 0.0f)
                              ? group.albedoOverrides[i]
                              : mat.albedo;
            fillPerObject(pod, mat, albedo);

            if (i < group.roughnessOverrides.size()) {
                pod.roughness = group.roughnessOverrides[i];
            }
            if (i < group.metallicOverrides.size()) {
                pod.metallic = group.metallicOverrides[i];
            }
            if (i < group.emissiveStrengthOverrides.size()) {
                pod.emissiveStrength = group.emissiveStrengthOverrides[i];
            }

            drawIndexToEntity.push_back(e);
            drawIdx++;
        }
        drawCmds.push_back(
            { group.mesh.indexCount, group.mesh.indexOffset, group.mesh.vertexOffset, N, baseIdx }
        );
    });

    totalSlots = drawIdx;

    // Fill shadow pass draw data (identical but at offset totalSlots)
    for (uint32_t i = 0; i < totalSlots; ++i) {
        objectMapped[totalSlots + i] = objectMapped[i];
    }
    // Fill cubemap pass draw data (identical but at offset 2*totalSlots)
    for (uint32_t i = 0; i < totalSlots; ++i) {
        objectMapped[2 * totalSlots + i] = objectMapped[i];
    }

    // Mark gizmo draws for filtering in render passes
    isGizmoDraw.resize(drawIndexToEntity.size());
    for (uint32_t i = 0; i < drawIndexToEntity.size(); ++i) {
        isGizmoDraw[i] = drawIndexToEntity[i].has<GizmoArrow>();
    }
}

// ---------------------------------------------------------------------------
// Scene::createMegaBuffers
// ---------------------------------------------------------------------------

void Scene::createMegaBuffers(gfx::IDevice& dev)
{
    spdlog::info(
        "Scene::createMegaBuffers (VB={} KB, IB={} KB)", megaVBCapacity * sizeof(VertexPBR) / 1024,
        megaIBCapacity * sizeof(uint32_t) / 1024
    );

    {
        size_t byteSize = megaVBCapacity * sizeof(VertexPBR);
        gfx::BufferDesc bd{};
        bd.size = byteSize;
        bd.usage = gfx::BufferUsage::Vertex;
        bd.debugName = "scene_megaVB";
        megaVB = dev.createBuffer(bd);
        auto* res = static_cast<ID3D12Resource*>(dev.nativeResource(megaVB));
        megaVBV.BufferLocation = res->GetGPUVirtualAddress();
        megaVBV.StrideInBytes = sizeof(VertexPBR);
        megaVBV.SizeInBytes = (UINT)byteSize;
    }

    {
        size_t byteSize = megaIBCapacity * sizeof(uint32_t);
        gfx::BufferDesc bd{};
        bd.size = byteSize;
        bd.usage = gfx::BufferUsage::Index;
        bd.debugName = "scene_megaIB";
        megaIB = dev.createBuffer(bd);
        auto* res = static_cast<ID3D12Resource*>(dev.nativeResource(megaIB));
        megaIBV.BufferLocation = res->GetGPUVirtualAddress();
        megaIBV.Format = DXGI_FORMAT_R32_UINT;
        megaIBV.SizeInBytes = (UINT)byteSize;
    }
}

// ---------------------------------------------------------------------------
// Scene::createDrawDataBuffers
// ---------------------------------------------------------------------------

void Scene::createDrawDataBuffers(gfx::IDevice& dev)
{
    spdlog::info("Scene::createDrawDataBuffers (triple-buffered, {} slots)", maxDrawsPerFrame);

    for (int i = 0; i < nBuffers; ++i) {
        // Per-object structured buffer (Upload heap, persistently mapped).
        // Registered in the gfx bindless SRV heap via Structured usage.
        size_t byteSize = maxDrawsPerFrame * sizeof(PerObjectData);
        {
            gfx::BufferDesc bd{};
            bd.size = byteSize;
            bd.usage = gfx::BufferUsage::Upload | gfx::BufferUsage::Structured;
            bd.structuredStride = sizeof(PerObjectData);
            bd.debugName = "scene_perObject";
            perObjectBuffer[i] = dev.createBuffer(bd);
            perObjectMapped[i] = static_cast<PerObjectData*>(dev.map(perObjectBuffer[i]));
        }

        // Per-frame CB (aligned to 256-byte boundary as required by CBV)
        const uint64_t perFrameSize = (sizeof(PerFrameCB) + 255) & ~255;
        {
            gfx::BufferDesc bd{};
            bd.size = perFrameSize;
            bd.usage = gfx::BufferUsage::Upload;
            bd.debugName = "scene_perFrame";
            perFrameBuffer[i] = dev.createBuffer(bd);
            perFrameMapped[i] = static_cast<PerFrameCB*>(dev.map(perFrameBuffer[i]));
        }

        // Per-pass CBs (array of maxPassesPerFrame slots of 256 bytes)
        const uint64_t passStride = (sizeof(PerPassCB) + 255) & ~255;
        {
            gfx::BufferDesc bd{};
            bd.size = maxPassesPerFrame * passStride;
            bd.usage = gfx::BufferUsage::Upload;
            bd.debugName = "scene_perPass";
            perPassBuffer[i] = dev.createBuffer(bd);
            perPassMapped[i] = static_cast<PerPassCB*>(dev.map(perPassBuffer[i]));
        }
    }
}

// ---------------------------------------------------------------------------
// Scene::appendToMegaBuffers
// ---------------------------------------------------------------------------

MeshRef Scene::appendToMegaBuffers(
    gfx::IDevice& dev,
    CommandQueue& cmdQueue,
    const std::vector<VertexPBR>& vertices,
    const std::vector<uint32_t>& indices,
    int materialIdx
)
{
    uint32_t numVerts = static_cast<uint32_t>(vertices.size());
    uint32_t numIndices = static_cast<uint32_t>(indices.size());

    assert(megaVBUsed + numVerts <= megaVBCapacity && "Mega VB capacity exceeded");
    assert(megaIBUsed + numIndices <= megaIBCapacity && "Mega IB capacity exceeded");

    dev.uploadBuffer(
        megaVB, vertices.data(), numVerts * sizeof(VertexPBR), megaVBUsed * sizeof(VertexPBR)
    );
    dev.uploadBuffer(
        megaIB, indices.data(), numIndices * sizeof(uint32_t), megaIBUsed * sizeof(uint32_t)
    );

    MeshRef ref;
    ref.vertexOffset = megaVBUsed;
    ref.vertexCount = numVerts;
    ref.indexOffset = megaIBUsed;
    ref.indexCount = numIndices;
    ref.materialIndex = materialIdx;

    megaVBUsed += numVerts;
    megaIBUsed += numIndices;

    buildBlasForMesh(dev, cmdQueue, ref);

    return ref;
}

// ---------------------------------------------------------------------------
// Scene::clearScene
// ---------------------------------------------------------------------------

void Scene::clearScene(CommandQueue& cmdQueue)
{
    cmdQueue.flush();
    materials.clear();
    selectedMaterialIdx = 0;
    for (auto& pi : presetIdx) {
        pi = -1;
    }
    megaVBUsed = 0;
    megaIBUsed = 0;
    ecsWorld.delete_with<MeshRef>();
    ecsWorld.delete_with<InstanceGroup>();
    spawnableMeshRefs.clear();
    spawnableMeshNames.clear();
    spawnTimer = 0.0f;

    blasMap.clear();
    tlasBuffer.Reset();
    tlasScratch.Reset();
    tlasInstances.Reset();
}

// ---------------------------------------------------------------------------
// Scene::loadTeapot
// ---------------------------------------------------------------------------

void Scene::loadTeapot(gfx::IDevice& dev, CommandQueue& cmdQueue, bool includeCompanion)
{
    auto loadRes = [](int resId) -> std::string {
        HRSRC hRes = FindResource(nullptr, MAKEINTRESOURCE(resId), RT_RCDATA);
        if (!hRes) {
            return "";
        }
        HGLOBAL hMem = LoadResource(nullptr, hRes);
        if (!hMem) {
            return "";
        }
        DWORD size = SizeofResource(nullptr, hRes);
        void* data = LockResource(hMem);
        if (!data) {
            return "";
        }
        return std::string(static_cast<const char*>(data), size);
    };

    std::string objData = loadRes(IDR_TEAPOT_OBJ);
    std::string mtlData = loadRes(IDR_TEAPOT_MTL);
    if (objData.empty()) {
        spdlog::error("Failed to load teapot OBJ from resource");
        return;
    }

    tinyobj::ObjReader reader;
    tinyobj::ObjReaderConfig reader_config;
    reader_config.triangulate = true;

    if (!reader.ParseFromString(objData, mtlData, reader_config)) {
        spdlog::error("TinyObj error: {}", reader.Error());
        return;
    }

    const auto& attrib = reader.GetAttrib();
    const auto& shapes = reader.GetShapes();

    std::vector<VertexPBR> verts;
    std::vector<uint32_t> indices;

    for (const auto& shape : shapes) {
        for (const auto& index : shape.mesh.indices) {
            VertexPBR v{};
            if (index.vertex_index >= 0 && 3 * index.vertex_index + 2 < attrib.vertices.size()) {
                v.position = { attrib.vertices[3 * index.vertex_index + 0],
                               attrib.vertices[3 * index.vertex_index + 1],
                               attrib.vertices[3 * index.vertex_index + 2] };
            }
            if (index.normal_index >= 0 && 3 * index.normal_index + 2 < attrib.normals.size()) {
                v.normal = { attrib.normals[3 * index.normal_index + 0],
                             attrib.normals[3 * index.normal_index + 1],
                             attrib.normals[3 * index.normal_index + 2] };
            }
            if (index.texcoord_index >= 0 &&
                2 * index.texcoord_index + 1 < attrib.texcoords.size()) {
                v.uv = { attrib.texcoords[2 * index.texcoord_index + 0],
                         1.0f - attrib.texcoords[2 * index.texcoord_index + 1] };
            }
            verts.push_back(v);
            indices.push_back((uint32_t)indices.size());
        }
    }

    int base = (int)materials.size();
    Material diff, metal, mirror;
    diff.name = "TeapotDiffuse";
    diff.albedo = { 0.8f, 0.8f, 0.8f, 1.0f };
    diff.roughness = 0.8f;
    diff.metallic = 0.0f;

    metal.name = "TeapotMetal";
    metal.albedo = { 0.25f, 0.25f, 0.25f, 1.0f };
    metal.roughness = 0.2f;
    metal.metallic = 1.0f;

    mirror.name = "TeapotMirror";
    mirror.albedo = { 0.9f, 0.9f, 0.9f, 1.0f };
    mirror.roughness = 0.02f;
    mirror.metallic = 1.0f;
    mirror.reflective = true;

    materials.push_back(diff);
    materials.push_back(metal);
    materials.push_back(mirror);

    presetIdx[static_cast<int>(MaterialPreset::Diffuse)] = base;
    presetIdx[static_cast<int>(MaterialPreset::Metal)] = base + 1;
    presetIdx[static_cast<int>(MaterialPreset::Mirror)] = base + 2;

    MeshRef meshRef = appendToMegaBuffers(dev, cmdQueue, verts, indices, base);
    spawnableMeshRefs.push_back(meshRef);
    spawnableMeshNames.push_back("Teapot");

    // Rough bounding sphere for teapot (radius ~2.5 centered near origin)
    BoundingVolume teapotBV;
    teapotBV.sphere.center = { 0, 1.25f, 0 };
    teapotBV.sphere.radius = 2.5f;

    // Central teapot: mirror
    MeshRef mirrorRef = meshRef;
    mirrorRef.materialIndex = base + 2;
    ecsWorld.entity("CentralTeapot")
        .set(Transform{ scale(1.5f, 1.5f, 1.5f) * translate(0, 0, 0) })
        .set(mirrorRef)
        .set(teapotBV)
        .add<Pickable>();

    // Non-reflective companion so reflections have content
    if (includeCompanion) {
        MeshRef companionRef = meshRef;
        companionRef.materialIndex = base;
        ecsWorld.entity("CompanionTeapot")
            .set(Transform{ translate(5, 0, 0) })
            .set(companionRef)
            .set(teapotBV)
            .add<Pickable>();
    }
}

// ---------------------------------------------------------------------------
// Scene::loadGltf
// ---------------------------------------------------------------------------

bool Scene::loadGltf(
    const std::string& path,
    gfx::IDevice& dev,
    CommandQueue& cmdQueue,
    bool append
)
{
    if (!append) {
        clearScene(cmdQueue);
        loadTeapot(dev, cmdQueue, false);
    }

    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    std::string err, warn;
    bool ret = loader.LoadBinaryFromFile(&model, &err, &warn, path);
    if (!ret) {
        ret = loader.LoadASCIIFromFile(&model, &err, &warn, path);
    }
    if (!warn.empty()) {
        spdlog::warn("glTF Load warn: {}", warn);
    }
    if (!ret) {
        spdlog::error("glTF Load error: {}", err);
        return false;
    }

    int materialBaseIdx = (int)materials.size();
    for (const auto& gMat : model.materials) {
        Material mat;
        mat.name = gMat.name;
        auto& pbr = gMat.pbrMetallicRoughness;
        mat.albedo = { (float)pbr.baseColorFactor[0], (float)pbr.baseColorFactor[1],
                       (float)pbr.baseColorFactor[2], (float)pbr.baseColorFactor[3] };
        mat.roughness = (float)pbr.roughnessFactor;
        mat.metallic = (float)pbr.metallicFactor;
        mat.emissive = { (float)gMat.emissiveFactor[0], (float)gMat.emissiveFactor[1],
                         (float)gMat.emissiveFactor[2], 1.0f };
        mat.emissiveStrength = 1.0f;  // tinygltf uses factor directly, could scale here
        materials.push_back(mat);
    }

    for (const auto& gMesh : model.meshes) {
        for (const auto& prim : gMesh.primitives) {
            if (prim.mode != TINYGLTF_MODE_TRIANGLES) {
                continue;
            }

            std::vector<VertexPBR> verts;
            const float* posPtr = nullptr;
            const float* normPtr = nullptr;
            const float* uvPtr = nullptr;
            size_t count = 0;

            if (prim.attributes.contains("POSITION")) {
                const auto& acc = model.accessors[prim.attributes.find("POSITION")->second];
                const auto& bv = model.bufferViews[acc.bufferView];
                posPtr = reinterpret_cast<const float*>(
                    &(model.buffers[bv.buffer].data[acc.byteOffset + bv.byteOffset])
                );
                count = acc.count;
            }
            if (prim.attributes.contains("NORMAL")) {
                const auto& acc = model.accessors[prim.attributes.find("NORMAL")->second];
                const auto& bv = model.bufferViews[acc.bufferView];
                normPtr = reinterpret_cast<const float*>(
                    &(model.buffers[bv.buffer].data[acc.byteOffset + bv.byteOffset])
                );
            }
            if (prim.attributes.contains("TEXCOORD_0")) {
                const auto& acc = model.accessors[prim.attributes.find("TEXCOORD_0")->second];
                const auto& bv = model.bufferViews[acc.bufferView];
                uvPtr = reinterpret_cast<const float*>(
                    &(model.buffers[bv.buffer].data[acc.byteOffset + bv.byteOffset])
                );
            }

            for (size_t i = 0; i < count; ++i) {
                VertexPBR v;
                v.position = { posPtr[3 * i + 0], posPtr[3 * i + 1], posPtr[3 * i + 2] };
                v.normal = normPtr
                               ? vec3(normPtr[3 * i + 0], normPtr[3 * i + 1], normPtr[3 * i + 2])
                               : vec3(0, 1, 0);
                v.uv = uvPtr ? vec2(uvPtr[2 * i + 0], uvPtr[2 * i + 1]) : vec2(0, 0);
                verts.push_back(v);
            }

            std::vector<uint32_t> indices;
            if (prim.indices >= 0) {
                const auto& acc = model.accessors[prim.indices];
                const auto& bv = model.bufferViews[acc.bufferView];
                const auto& buf = model.buffers[bv.buffer];
                const uint8_t* data = &buf.data[acc.byteOffset + bv.byteOffset];
                for (size_t i = 0; i < acc.count; ++i) {
                    if (acc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
                        indices.push_back(*reinterpret_cast<const uint32_t*>(data + i * 4));
                    } else if (acc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
                        indices.push_back(*reinterpret_cast<const uint16_t*>(data + i * 2));
                    } else if (acc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
                        indices.push_back(*reinterpret_cast<const uint8_t*>(data + i));
                    }
                }
            } else {
                for (size_t i = 0; i < count; ++i) {
                    indices.push_back((uint32_t)i);
                }
            }

            int matIdx = (prim.material >= 0) ? materialBaseIdx + prim.material : 0;
            MeshRef meshRef = appendToMegaBuffers(dev, cmdQueue, verts, indices, matIdx);
            spawnableMeshRefs.push_back(meshRef);
            spawnableMeshNames.push_back(
                gMesh.name.empty() ? "GLB mesh " + std::to_string(spawnableMeshNames.size())
                                   : gMesh.name
            );

            // Calculate bounding sphere for the mesh
            vec3 minP(1e30f), maxP(-1e30f);
            for (const auto& v : verts) {
                minP.x = std::min(minP.x, v.position.x);
                minP.y = std::min(minP.y, v.position.y);
                minP.z = std::min(minP.z, v.position.z);
                maxP.x = std::max(maxP.x, v.position.x);
                maxP.y = std::max(maxP.y, v.position.y);
                maxP.z = std::max(maxP.z, v.position.z);
            }
            vec3 center = (minP + maxP) * 0.5f;
            float maxDist2 = 0.0f;
            for (const auto& v : verts) {
                vec3 d = v.position - center;
                maxDist2 = std::max(maxDist2, dot(d, d));
            }
            BoundingVolume bv;
            bv.sphere.center = center;
            bv.sphere.radius = std::sqrt(maxDist2);

            // Instantiate
            Transform tf;
            tf.world = mat4{};
            ecsWorld.entity().set(tf).set(meshRef).set(bv).add<Pickable>();
        }
    }

    spdlog::info(
        "Loaded GLB: {} entity(ies), {} material(s)", ecsWorld.count<MeshRef>(), materials.size()
    );
    return true;
}

// ---------------------------------------------------------------------------
// Scene Raytracing
// ---------------------------------------------------------------------------

void Scene::buildBlasForMesh(gfx::IDevice& dev, CommandQueue& cmdQueue, MeshRef& mesh)
{
    auto* device = nativeDev(dev);
    uint64_t key = (static_cast<uint64_t>(mesh.vertexOffset) << 32) | mesh.indexOffset;
    if (blasMap.contains(key)) {
        return;
    }

    ComPtr<ID3D12Device5> rtDevice;
    chkDX(device->QueryInterface(IID_PPV_ARGS(&rtDevice)));

    auto cmdList = cmdQueue.getCmdList();
    ComPtr<ID3D12GraphicsCommandList4> rtCmdList;
    chkDX(cmdList.As(&rtCmdList));

    D3D12_RAYTRACING_GEOMETRY_DESC geomDesc = {};
    geomDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
    geomDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
    auto* megaVBRes = static_cast<ID3D12Resource*>(dev.nativeResource(megaVB));
    auto* megaIBRes = static_cast<ID3D12Resource*>(dev.nativeResource(megaIB));
    geomDesc.Triangles.VertexBuffer.StartAddress =
        megaVBRes->GetGPUVirtualAddress() + mesh.vertexOffset * sizeof(VertexPBR);
    geomDesc.Triangles.VertexBuffer.StrideInBytes = sizeof(VertexPBR);
    geomDesc.Triangles.VertexCount = mesh.vertexCount;
    geomDesc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
    geomDesc.Triangles.IndexBuffer =
        megaIBRes->GetGPUVirtualAddress() + mesh.indexOffset * sizeof(uint32_t);
    geomDesc.Triangles.IndexCount = mesh.indexCount;
    geomDesc.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    inputs.NumDescs = 1;
    inputs.pGeometryDescs = &geomDesc;
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info = {};
    rtDevice->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);

    ComPtr<ID3D12Resource> scratch;
    {
        const CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
        const CD3DX12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(
            info.ScratchDataSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
        );
        chkDX(device->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr,
            IID_PPV_ARGS(&scratch)
        ));
    }

    ComPtr<ID3D12Resource> blas;
    {
        const CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
        const CD3DX12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(
            info.ResultDataMaxSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
        );
        chkDX(device->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, nullptr, IID_PPV_ARGS(&blas)
        ));
    }

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
    buildDesc.Inputs = inputs;
    buildDesc.ScratchAccelerationStructureData = scratch->GetGPUVirtualAddress();
    buildDesc.DestAccelerationStructureData = blas->GetGPUVirtualAddress();

    rtCmdList->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);

    CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::UAV(blas.Get());
    cmdList->ResourceBarrier(1, &barrier);

    uint64_t fv = cmdQueue.execCmdList(cmdList);
    cmdQueue.waitForFenceVal(fv);  // scratch must stay alive until build completes

    blasMap[key] = { blas };
    spdlog::debug("Built BLAS for mesh at vertexOffset {}", mesh.vertexOffset);
}

void Scene::updateLightBuffer(gfx::IDevice& dev, CommandQueue& cmdQueue)
{
    std::vector<LightData> lights;
    lightQuery.each([&](flecs::entity e, PointLight& pl) {
        if (lights.size() >= maxLightsRRT) {
            return;
        }

        vec3 worldPos = pl.center;
        if (e.has<Transform>()) {
            auto tf = e.get<Transform>();
            worldPos = vec3(tf.world._41, tf.world._42, tf.world._43);
        }

        LightData ld;
        ld.position = worldPos;
        ld.intensity = 1.0f;  // TODO: Plumb brightness
        ld.color = vec3(pl.color.x, pl.color.y, pl.color.z);
        ld.radius = 20.0f;  // TODO: Plumb radius
        lights.push_back(ld);
    });

    activeLightCount = static_cast<uint32_t>(lights.size());
    if (activeLightCount == 0) {
        return;
    }

    size_t bufferSize = maxLightsRRT * sizeof(LightData);
    if (!lightBuffer.isValid()) {
        gfx::BufferDesc bd{};
        bd.size = bufferSize;
        bd.usage = gfx::BufferUsage::Upload;
        bd.debugName = "scene_lightBuffer";
        lightBuffer = dev.createBuffer(bd);
    }

    void* mapped = dev.map(lightBuffer);
    memcpy(mapped, lights.data(), lights.size() * sizeof(LightData));
    dev.unmap(lightBuffer);
}

void Scene::updateTLAS(gfx::IDevice& dev, CommandQueue& cmdQueue, uint32_t curBackBufIdx)
{
    auto* device = nativeDev(dev);
    std::vector<D3D12_RAYTRACING_INSTANCE_DESC> instanceDescs;

    auto addInstance = [&](const mat4& world, const MeshRef& mesh) {
        uint64_t key = (static_cast<uint64_t>(mesh.vertexOffset) << 32) | mesh.indexOffset;
        if (!blasMap.contains(key)) {
            return;
        }

        D3D12_RAYTRACING_INSTANCE_DESC desc = {};
        mat4 transposed = transpose(world);
        memcpy(desc.Transform, &transposed, sizeof(float) * 12);
        desc.InstanceID = 0;
        desc.InstanceMask = 0xFF;
        desc.InstanceContributionToHitGroupIndex = 0;
        desc.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
        desc.AccelerationStructure = blasMap[key].buffer->GetGPUVirtualAddress();
        instanceDescs.push_back(desc);
    };

    drawQuery.each([&](flecs::entity e, const Transform& tf, const MeshRef& mesh) {
        addInstance(tf.world, mesh);
    });

    instanceQuery.each([&](flecs::entity e, const Transform&, const InstanceGroup& group) {
        for (const auto& world : group.transforms) {
            addInstance(world, group.mesh);
        }
    });

    if (instanceDescs.empty()) {
        return;
    }

    ComPtr<ID3D12Device5> rtDevice;
    chkDX(device->QueryInterface(IID_PPV_ARGS(&rtDevice)));

    size_t instanceBufferSize = instanceDescs.size() * sizeof(D3D12_RAYTRACING_INSTANCE_DESC);

    if (!tlasInstances || tlasInstances->GetDesc().Width < instanceBufferSize) {
        const CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
        const CD3DX12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(instanceBufferSize * 2);
        chkDX(device->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&tlasInstances)
        ));
    }

    void* mapped = nullptr;
    chkDX(tlasInstances->Map(0, nullptr, &mapped));
    memcpy(mapped, instanceDescs.data(), instanceBufferSize);
    tlasInstances->Unmap(0, nullptr);

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    inputs.NumDescs = static_cast<UINT>(instanceDescs.size());
    inputs.InstanceDescs = tlasInstances->GetGPUVirtualAddress();
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info = {};
    rtDevice->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);

    if (!tlasBuffer || tlasBuffer->GetDesc().Width < info.ResultDataMaxSizeInBytes) {
        const CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
        const CD3DX12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(
            info.ResultDataMaxSizeInBytes * 2, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
        );
        chkDX(device->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, nullptr,
            IID_PPV_ARGS(&tlasBuffer)
        ));
    }
    if (!tlasScratch || tlasScratch->GetDesc().Width < info.ScratchDataSizeInBytes) {
        const CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
        const CD3DX12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(
            info.ScratchDataSizeInBytes * 2, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
        );
        chkDX(device->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr,
            IID_PPV_ARGS(&tlasScratch)
        ));
    }

    auto cmdList = cmdQueue.getCmdList();
    ComPtr<ID3D12GraphicsCommandList4> rtCmdList;
    chkDX(cmdList.As(&rtCmdList));

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
    buildDesc.Inputs = inputs;
    buildDesc.ScratchAccelerationStructureData = tlasScratch->GetGPUVirtualAddress();
    buildDesc.DestAccelerationStructureData = tlasBuffer->GetGPUVirtualAddress();

    rtCmdList->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);

    CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::UAV(tlasBuffer.Get());
    cmdList->ResourceBarrier(1, &barrier);

    cmdQueue.execCmdList(cmdList);
}
