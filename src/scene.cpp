module;

#if defined(__clang__)
    #define FMT_CONSTEVAL
#endif

#include <d3d12.h>
#include <directxtk12/ResourceUploadBatch.h>
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
    pod.albedoTexId = mat.albedoTexId;
    pod.normalTexId = mat.normalTexId;
    pod.mrTexId = mat.mrTexId;
    pod.emissiveTexId = mat.emissiveTexId;
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
    isGizmoDraw.clear();
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
        isGizmoDraw.push_back(e.has<GizmoArrow>());
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
            isGizmoDraw.push_back(false);  // instance groups are not gizmo arrows
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
}

// ---------------------------------------------------------------------------
// Scene::createMegaBuffers
// ---------------------------------------------------------------------------

void Scene::createMegaBuffers(gfx::IDevice& dev)
{
    devForDestroy = &dev;  // stash for clearScene / ~Scene to release owned gfx handles
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
        megaVBV.gpuAddress = res->GetGPUVirtualAddress();
        megaVBV.strideInBytes = sizeof(VertexPBR);
        megaVBV.sizeInBytes = (uint32_t)byteSize;
    }

    {
        size_t byteSize = megaIBCapacity * sizeof(uint32_t);
        gfx::BufferDesc bd{};
        bd.size = byteSize;
        bd.usage = gfx::BufferUsage::Index;
        bd.debugName = "scene_megaIB";
        megaIB = dev.createBuffer(bd);
        auto* res = static_cast<ID3D12Resource*>(dev.nativeResource(megaIB));
        megaIBV.gpuAddress = res->GetGPUVirtualAddress();
        megaIBV.format = gfx::IndexFormat::Uint32;
        megaIBV.sizeInBytes = (uint32_t)byteSize;
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

    // Release adopted PBR textures back to gfx. Safe to call destroy on each
    // because the cmdQueue.flush above guarantees no in-flight references.
    if (devForDestroy) {
        for (auto h : ownedTextureHandles) {
            if (h.isValid()) {
                devForDestroy->destroy(h);
            }
        }
    }
    ownedTextureHandles.clear();

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
    bool append,
    bool instantiate
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

    // --- Load embedded glTF images into GPU textures, registered in the
    // global bindless heap. tinygltf has already decoded each image to
    // raw RGBA in `image.image` via stb_image. We upload via DirectXTK's
    // ResourceUploadBatch (the same path billboard.cpp uses for the
    // light sprite), then register an external SRV. ---
    std::vector<int> imageBindlessIdx(model.images.size(), -1);
    if (!model.images.empty()) {
        auto* d3dDev = static_cast<ID3D12Device2*>(dev.nativeHandle());
        auto* queue = static_cast<ID3D12CommandQueue*>(dev.graphicsQueue()->nativeHandle());
        DirectX::ResourceUploadBatch upload(d3dDev);
        upload.Begin(D3D12_COMMAND_LIST_TYPE_DIRECT);

        for (size_t i = 0; i < model.images.size(); ++i) {
            const auto& img = model.images[i];
            if (img.image.empty() || img.width <= 0 || img.height <= 0) {
                continue;
            }

            // tinygltf typically gives us 4 components (RGBA). If 3, expand to 4
            // here so we can use the universal R8G8B8A8 path.
            const uint8_t* srcBytes = img.image.data();
            std::vector<uint8_t> rgbaCopy;
            if (img.component == 3) {
                rgbaCopy.resize(size_t(img.width) * img.height * 4);
                for (size_t p = 0; p < size_t(img.width) * img.height; ++p) {
                    rgbaCopy[p * 4 + 0] = img.image[p * 3 + 0];
                    rgbaCopy[p * 4 + 1] = img.image[p * 3 + 1];
                    rgbaCopy[p * 4 + 2] = img.image[p * 3 + 2];
                    rgbaCopy[p * 4 + 3] = 255;
                }
                srcBytes = rgbaCopy.data();
            } else if (img.component != 4) {
                spdlog::warn(
                    "glTF image {} has {} components (need 3 or 4), skipping", i, img.component
                );
                continue;
            }

            // Create a default-heap R8G8B8A8_UNORM_SRGB texture (sRGB for albedo;
            // future work: detect linear maps like normal/MR and use UNORM there).
            D3D12_RESOURCE_DESC rd = CD3DX12_RESOURCE_DESC::Tex2D(
                DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, UINT64(img.width), UINT(img.height), 1, 1
            );
            auto heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
            ComPtr<ID3D12Resource> tex;
            HRESULT hr = d3dDev->CreateCommittedResource(
                &heap, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                IID_PPV_ARGS(&tex)
            );
            if (FAILED(hr)) {
                spdlog::warn("glTF: CreateCommittedResource for image {} failed 0x{:08x}", i, hr);
                continue;
            }
            std::wstring wname(img.name.begin(), img.name.end());
            if (wname.empty()) {
                wname = L"gltf_image";
            }
            tex->SetName(wname.c_str());

            D3D12_SUBRESOURCE_DATA sub{};
            sub.pData = srcBytes;
            sub.RowPitch = LONG_PTR(img.width) * 4;
            sub.SlicePitch = sub.RowPitch * img.height;
            upload.Upload(tex.Get(), 0, &sub, 1);
            upload.Transition(
                tex.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
            );

            // Hand the resource to gfx so lifetime + SRV slot are managed
            // through the same pendingDestroys/fence path as engine-allocated
            // textures. We don't need to keep our own ComPtr after this.
            gfx::TextureHandle handle =
                dev.adoptTexture(tex.Get(), gfx::Format::RGBA8UnormSrgb, /*mipLevels=*/1);
            ownedTextureHandles.push_back(handle);
            imageBindlessIdx[i] = static_cast<int>(dev.bindlessSrvIndex(handle));
        }

        auto fut = upload.End(queue);
        fut.wait();
    }

    // Map a glTF texture-info index → bindless heap index via the texture's source image.
    auto textureBindless = [&](int textureIdx) -> int {
        if (textureIdx < 0 || textureIdx >= int(model.textures.size())) {
            return -1;
        }
        int imgIdx = model.textures[textureIdx].source;
        if (imgIdx < 0 || imgIdx >= int(imageBindlessIdx.size())) {
            return -1;
        }
        return imageBindlessIdx[imgIdx];
    };

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
        mat.albedoTexId = textureBindless(pbr.baseColorTexture.index);
        mat.normalTexId = textureBindless(gMat.normalTexture.index);
        mat.mrTexId = textureBindless(pbr.metallicRoughnessTexture.index);
        mat.emissiveTexId = textureBindless(gMat.emissiveTexture.index);
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

            // Instantiate (skip when instantiate=false; caller will spawn entities explicitly)
            if (instantiate) {
                Transform tf;
                tf.world = mat4{};
                ecsWorld.entity().set(tf).set(meshRef).set(bv).add<Pickable>();
            }
        }
    }

    // --- Skeletons (glTF skins) ---
    int skeletonBaseIdx = static_cast<int>(skeletons.size());
    for (const auto& gSkin : model.skins) {
        Skeleton sk;
        sk.name = gSkin.name;
        sk.joints.reserve(gSkin.joints.size());
        // Per-joint inverse bind matrices live in a single accessor; mat4 stride.
        const float* ibmData = nullptr;
        size_t ibmCount = 0;
        if (gSkin.inverseBindMatrices >= 0) {
            const auto& acc = model.accessors[gSkin.inverseBindMatrices];
            const auto& bv = model.bufferViews[acc.bufferView];
            ibmData = reinterpret_cast<const float*>(
                &model.buffers[bv.buffer].data[acc.byteOffset + bv.byteOffset]
            );
            ibmCount = acc.count;
        }
        // glTF stores joints as node indices. Build a node-index→local-index
        // map first so parent lookup can be local-space.
        std::vector<int> nodeToLocal(model.nodes.size(), -1);
        for (size_t j = 0; j < gSkin.joints.size(); ++j) {
            nodeToLocal[gSkin.joints[j]] = static_cast<int>(j);
        }
        for (size_t j = 0; j < gSkin.joints.size(); ++j) {
            const auto& gNode = model.nodes[gSkin.joints[j]];
            SkeletonJoint joint;
            joint.name = gNode.name;
            // Find parent: any glTF node that lists this joint in its children
            // and is itself in the skin's joint list. O(N^2) — fine for now.
            for (size_t p = 0; p < model.nodes.size(); ++p) {
                if (nodeToLocal[p] < 0) {
                    continue;
                }
                for (int c : model.nodes[p].children) {
                    if (c == gSkin.joints[j]) {
                        joint.parent = nodeToLocal[p];
                        break;
                    }
                }
                if (joint.parent >= 0) {
                    break;
                }
            }
            if (gNode.translation.size() == 3) {
                joint.localTranslation = { (float)gNode.translation[0], (float)gNode.translation[1],
                                           (float)gNode.translation[2] };
            }
            if (gNode.rotation.size() == 4) {
                joint.localRotation = { (float)gNode.rotation[0], (float)gNode.rotation[1],
                                        (float)gNode.rotation[2], (float)gNode.rotation[3] };
            }
            if (gNode.scale.size() == 3) {
                joint.localScale = { (float)gNode.scale[0], (float)gNode.scale[1],
                                     (float)gNode.scale[2] };
            }
            if (ibmData && j < ibmCount) {
                std::memcpy(&joint.inverseBindMatrix, ibmData + j * 16, sizeof(float) * 16);
            }
            sk.joints.push_back(std::move(joint));
        }
        skeletons.push_back(std::move(sk));
    }

    // --- Animation clips ---
    for (const auto& gAnim : model.animations) {
        AnimationClip clip;
        clip.name = gAnim.name;
        for (const auto& gChan : gAnim.channels) {
            if (gChan.target_node < 0 || gChan.sampler < 0) {
                continue;
            }
            // Map target node → joint index inside the first matching skeleton.
            int jointIdx = -1;
            int sklIdx = -1;
            for (size_t si = 0; si < model.skins.size(); ++si) {
                const auto& gSkin = model.skins[si];
                for (size_t j = 0; j < gSkin.joints.size(); ++j) {
                    if (gSkin.joints[j] == gChan.target_node) {
                        jointIdx = static_cast<int>(j);
                        sklIdx = skeletonBaseIdx + static_cast<int>(si);
                        break;
                    }
                }
                if (jointIdx >= 0) {
                    break;
                }
            }
            if (jointIdx < 0) {
                continue;  // Channel targets a non-skin node; skipped for now.
            }

            AnimationChannel ch;
            ch.jointIndex = jointIdx;
            if (gChan.target_path == "translation") {
                ch.path = AnimationChannel::Path::Translation;
            } else if (gChan.target_path == "rotation") {
                ch.path = AnimationChannel::Path::Rotation;
            } else if (gChan.target_path == "scale") {
                ch.path = AnimationChannel::Path::Scale;
            } else {
                continue;  // weights (morph targets) not yet supported
            }

            const auto& gSampler = gAnim.samplers[gChan.sampler];
            // Input (timestamps).
            const auto& inAcc = model.accessors[gSampler.input];
            const auto& inBv = model.bufferViews[inAcc.bufferView];
            const float* inData = reinterpret_cast<const float*>(
                &model.buffers[inBv.buffer].data[inAcc.byteOffset + inBv.byteOffset]
            );
            ch.timestamps.assign(inData, inData + inAcc.count);
            // Output (values).
            const auto& outAcc = model.accessors[gSampler.output];
            const auto& outBv = model.bufferViews[outAcc.bufferView];
            const float* outData = reinterpret_cast<const float*>(
                &model.buffers[outBv.buffer].data[outAcc.byteOffset + outBv.byteOffset]
            );
            int stride = (ch.path == AnimationChannel::Path::Rotation) ? 4 : 3;
            ch.values.assign(outData, outData + outAcc.count * stride);
            if (!ch.timestamps.empty()) {
                clip.duration = std::max(clip.duration, ch.timestamps.back());
            }
            clip.channels.push_back(std::move(ch));
            (void)sklIdx;  // first-skin assumption — multi-skin clips defer
        }
        animations.push_back(std::move(clip));
    }

    spdlog::info(
        "Loaded GLB: {} entity(ies), {} material(s), {} skin(s), {} animation(s)",
        ecsWorld.count<MeshRef>(), materials.size(), model.skins.size(), model.animations.size()
    );
    return true;
}

// ---------------------------------------------------------------------------
// Scene Raytracing
// ---------------------------------------------------------------------------

void Scene::buildBlasForMesh(gfx::IDevice& dev, CommandQueue& cmdQueue, MeshRef& mesh)
{
    auto* device = static_cast<ID3D12Device2*>(dev.nativeHandle());
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

// ---------------------------------------------------------------------------
// Scene::computeSkinningMatrices — sample animation channels, propagate parent
// transforms, multiply by inverse bind. CPU-side groundwork; the future GPU
// skinning shader uploads this matrix array to a structured buffer.
// ---------------------------------------------------------------------------

namespace
{
    // Binary search for the keyframe interval covering `t`. Returns the lower
    // index and an alpha in [0, 1] for interpolation. Clamps at the ends.
    void findKeyframe(
        const std::vector<float>& times, float t, size_t& outLo, float& outAlpha
    )
    {
        if (times.empty()) {
            outLo = 0;
            outAlpha = 0.0f;
            return;
        }
        if (t <= times.front()) {
            outLo = 0;
            outAlpha = 0.0f;
            return;
        }
        if (t >= times.back()) {
            outLo = times.size() - 1;
            outAlpha = 0.0f;  // hold last value
            return;
        }
        // upper_bound returns first > t; lower index is one back.
        auto it = std::upper_bound(times.begin(), times.end(), t);
        size_t hi = static_cast<size_t>(it - times.begin());
        size_t lo = hi - 1;
        float span = times[hi] - times[lo];
        outLo = lo;
        outAlpha = span > 0.0f ? (t - times[lo]) / span : 0.0f;
    }

    mat4 composeLocal(const SkeletonJoint& j)
    {
        // Compose T * R * S using DirectXMath. quaternion is (x, y, z, w).
        XMVECTOR q = XMVectorSet(
            j.localRotation.x, j.localRotation.y, j.localRotation.z, j.localRotation.w
        );
        XMMATRIX m = XMMatrixScaling(j.localScale.x, j.localScale.y, j.localScale.z) *
                     XMMatrixRotationQuaternion(q) *
                     XMMatrixTranslation(j.localTranslation.x, j.localTranslation.y,
                                         j.localTranslation.z);
        mat4 out;
        XMStoreFloat4x4(reinterpret_cast<XMFLOAT4X4*>(&out), m);
        return out;
    }
}  // namespace

uint32_t Scene::computeSkinningMatrices(
    const Animator& a, std::vector<mat4>& outMatrices
) const
{
    if (a.skeletonIdx < 0 || a.skeletonIdx >= static_cast<int>(skeletons.size())) {
        return 0;
    }
    const auto& skl = skeletons[a.skeletonIdx];
    const uint32_t jointCount = static_cast<uint32_t>(skl.joints.size());
    if (jointCount == 0) {
        return 0;
    }
    outMatrices.assign(jointCount, mat4{});

    // 1) Start from the skeleton's bind-pose TRS (already on each joint).
    //    For each animation channel that targets a joint, overwrite TRS by
    //    interpolating between the surrounding keyframes.
    std::vector<SkeletonJoint> jointsLocal = skl.joints;
    if (a.currentClip >= 0 && a.currentClip < static_cast<int>(animations.size())) {
        const auto& clip = animations[a.currentClip];
        for (const auto& ch : clip.channels) {
            if (ch.jointIndex < 0 || ch.jointIndex >= static_cast<int>(jointCount) ||
                ch.timestamps.empty()) {
                continue;
            }
            size_t lo = 0;
            float alpha = 0.0f;
            findKeyframe(ch.timestamps, a.time, lo, alpha);
            const int stride = (ch.path == AnimationChannel::Path::Rotation) ? 4 : 3;
            const size_t hi = std::min(lo + 1, ch.timestamps.size() - 1);
            const float* va = &ch.values[lo * stride];
            const float* vb = &ch.values[hi * stride];
            switch (ch.path) {
                case AnimationChannel::Path::Translation: {
                    jointsLocal[ch.jointIndex].localTranslation = {
                        va[0] * (1.0f - alpha) + vb[0] * alpha,
                        va[1] * (1.0f - alpha) + vb[1] * alpha,
                        va[2] * (1.0f - alpha) + vb[2] * alpha,
                    };
                    break;
                }
                case AnimationChannel::Path::Scale: {
                    jointsLocal[ch.jointIndex].localScale = {
                        va[0] * (1.0f - alpha) + vb[0] * alpha,
                        va[1] * (1.0f - alpha) + vb[1] * alpha,
                        va[2] * (1.0f - alpha) + vb[2] * alpha,
                    };
                    break;
                }
                case AnimationChannel::Path::Rotation: {
                    XMVECTOR qa = XMVectorSet(va[0], va[1], va[2], va[3]);
                    XMVECTOR qb = XMVectorSet(vb[0], vb[1], vb[2], vb[3]);
                    XMVECTOR qs = XMQuaternionSlerp(qa, qb, alpha);
                    XMFLOAT4 result;
                    XMStoreFloat4(&result, qs);
                    jointsLocal[ch.jointIndex].localRotation = { result.x, result.y, result.z,
                                                                 result.w };
                    break;
                }
            }
        }
    }

    // 2) Compose local matrices, then walk parents top-down. The glTF skin
    //    builder above guarantees that parent indices are strictly less than
    //    the child index (skin joints are in hierarchy order); if a future
    //    importer breaks that invariant we'll need an explicit topological sort.
    std::vector<mat4> world(jointCount);
    for (uint32_t i = 0; i < jointCount; ++i) {
        mat4 local = composeLocal(jointsLocal[i]);
        int parent = jointsLocal[i].parent;
        if (parent < 0 || parent >= static_cast<int>(i)) {
            world[i] = local;
        } else {
            XMMATRIX p = XMLoadFloat4x4(reinterpret_cast<const XMFLOAT4X4*>(&world[parent]));
            XMMATRIX l = XMLoadFloat4x4(reinterpret_cast<const XMFLOAT4X4*>(&local));
            XMMATRIX w = l * p;  // row-vector math: child applies first
            XMStoreFloat4x4(reinterpret_cast<XMFLOAT4X4*>(&world[i]), w);
        }
    }

    // 3) Final skinning matrix = world * inverseBind. Vertex shader multiplies
    //    (sum over influences) skinningMatrix[joint] * vertexLocalPos.
    for (uint32_t i = 0; i < jointCount; ++i) {
        XMMATRIX w = XMLoadFloat4x4(reinterpret_cast<const XMFLOAT4X4*>(&world[i]));
        XMMATRIX ib =
            XMLoadFloat4x4(reinterpret_cast<const XMFLOAT4X4*>(&skl.joints[i].inverseBindMatrix));
        XMMATRIX sk = ib * w;
        XMStoreFloat4x4(reinterpret_cast<XMFLOAT4X4*>(&outMatrices[i]), sk);
    }
    return jointCount;
}

// ---------------------------------------------------------------------------
// Scene::setupSystems / progress
// ---------------------------------------------------------------------------

void Scene::setupSystems()
{
    // Store previous transforms for motion vectors (runs before everything)
    ecsWorld.system<const Transform, PrevTransform>("StorePrevTransforms")
        .kind(flecs::OnUpdate)
        .each([](const Transform& tf, PrevTransform& ptf) { ptf.world = tf.world; });

    ecsWorld.system<const InstanceGroup, PrevInstanceGroup>("StorePrevInstanceTransforms")
        .kind(flecs::OnUpdate)
        .each([](const InstanceGroup& ig, PrevInstanceGroup& pig) {
            pig.transforms = ig.transforms;
        });

    // Ensure new entities have previous transforms
    ecsWorld.system<const Transform>("EnsurePrevTransform")
        .kind(flecs::PostLoad)
        .each([](flecs::entity e, const Transform& tf) {
            if (!e.has<PrevTransform>()) {
                e.set<PrevTransform>({ tf.world });
            }
        });

    ecsWorld.system<const InstanceGroup>("EnsurePrevInstanceTransform")
        .kind(flecs::PostLoad)
        .each([](flecs::entity e, const InstanceGroup& ig) {
            if (!e.has<PrevInstanceGroup>()) {
                e.set<PrevInstanceGroup>({ ig.transforms });
            }
        });

    // Animation systems
    ecsWorld.system<Transform, Animated>("AnimateOrbitingEntities")
        .kind(flecs::OnUpdate)
        .each([](flecs::iter& it, size_t, Transform& tf, Animated& anim) {
            float dt = it.delta_time();
            anim.orbitAngle += anim.speed * dt;
            float curTime = it.world().get<GlobalTime>().time;
            float pulse = 1.0f + 0.15f * std::sin(curTime * 2.0f + anim.pulsePhase);
            float s = anim.initialScale * pulse;
            vec3 pos(
                anim.orbitRadius * std::cos(anim.orbitAngle), anim.orbitY,
                anim.orbitRadius * std::sin(anim.orbitAngle)
            );
            tf.world = scale(s, s, s) * rotateAxis(anim.rotAxis, anim.rotAngle) *
                       translate(pos.x, pos.y, pos.z);
        });

    ecsWorld.system<InstanceGroup, InstanceAnimation>("AnimateInstancedGroups")
        .kind(flecs::OnUpdate)
        .each([](flecs::iter& it, size_t, InstanceGroup& group, InstanceAnimation& ia) {
            float dt = it.delta_time();
            ia.currentAngle += ia.rotationSpeed * dt;
            mat4 rot = rotateAxis(vec3(0.f, 1.f, 0.f), ia.currentAngle);
            for (size_t i = 0; i < group.transforms.size(); ++i) {
                float s = ia.scales[i];
                vec3 p = ia.positions[i];
                group.transforms[i] = scale(s, s, s) * rot * translate(p.x, p.y, p.z);
            }
        });

    // Skeletal animator — advances clip time; joint matrix evaluation +
    // GPU upload land in the next pass.
    ecsWorld.system<Animator>("AdvanceAnimators")
        .kind(flecs::OnUpdate)
        .each([this](flecs::iter& it, size_t, Animator& a) {
            if (!a.playing || a.currentClip < 0 ||
                a.currentClip >= static_cast<int>(animations.size())) {
                return;
            }
            float dur = animations[a.currentClip].duration;
            if (dur <= 0.0f) {
                return;
            }
            a.time += it.delta_time() * a.playbackSpeed;
            // Loop the clip.
            while (a.time >= dur) {
                a.time -= dur;
            }
        });
}

void Scene::progress(float dt)
{
    ecsWorld.progress(dt);
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
    auto* device = static_cast<ID3D12Device2*>(dev.nativeHandle());
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
