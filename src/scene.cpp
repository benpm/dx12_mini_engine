module;

#if defined(__clang__)
    #define FMT_CONSTEVAL
#endif

#include <Windows.h>
#include <d3d12.h>
#include <wrl.h>
#include <cassert>
#include <sstream>
#include <functional>
#include <algorithm>
#include <spdlog/spdlog.h>
#include <tiny_obj_loader.h>
#ifdef __clang__
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wswitch"
    #pragma clang diagnostic ignored "-Wunused-function"
    #pragma clang diagnostic ignored "-Wunused-variable"
    #pragma clang diagnostic ignored "-Wmissing-field-initializers"
    #pragma clang diagnostic ignored "-Wsign-compare"
    #pragma clang diagnostic ignored "-Wnullability-completeness"
    #pragma clang diagnostic ignored "-Wdeprecated-literal-operator"
#endif
#include <tiny_gltf.h>
#ifdef __clang__
    #pragma clang diagnostic pop
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wswitch"
#endif
#include "d3dx12.h"
#ifdef __clang__
    #pragma clang diagnostic pop
#endif
#include "resource.h"

module scene;

using Microsoft::WRL::ComPtr;

// ---------------------------------------------------------------------------
// Static helpers
// ---------------------------------------------------------------------------

static std::string GetResourceString(int resourceId)
{
    HRSRC hRes = FindResource(nullptr, MAKEINTRESOURCE(resourceId), RT_RCDATA);
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
}

static mat4 NodeTransform(const tinygltf::Node& node)
{
    if (node.matrix.size() == 16) {
        const auto& m = node.matrix;
        return mat4(
            (float)m[0], (float)m[1], (float)m[2], (float)m[3], (float)m[4], (float)m[5],
            (float)m[6], (float)m[7], (float)m[8], (float)m[9], (float)m[10], (float)m[11],
            (float)m[12], (float)m[13], (float)m[14], (float)m[15]
        );
    }
    mat4 S;
    mat4 R;
    mat4 T;
    if (node.scale.size() == 3) {
        S = scale((float)node.scale[0], (float)node.scale[1], (float)node.scale[2]);
    }
    if (node.rotation.size() == 4) {
        R = rotateQuaternion(
            (float)node.rotation[0], (float)node.rotation[1], (float)node.rotation[2],
            (float)node.rotation[3]
        );
    }
    if (node.translation.size() == 3) {
        T = translate(
            (float)node.translation[0], (float)node.translation[1], (float)node.translation[2]
        );
    }
    return S * R * T;
}

template <size_t N>
static std::vector<std::array<float, N>> AccessorToFloatN(
    const tinygltf::Model& model,
    int accessorIdx
)
{
    const auto& acc = model.accessors[accessorIdx];
    const auto& bv = model.bufferViews[acc.bufferView];
    const auto& buf = model.buffers[bv.buffer];
    const uint8_t* src = buf.data.data() + bv.byteOffset + acc.byteOffset;
    size_t stride = bv.byteStride ? bv.byteStride : (N * sizeof(float));
    std::vector<std::array<float, N>> out(acc.count);
    for (size_t i = 0; i < acc.count; ++i) {
        const float* f = reinterpret_cast<const float*>(src + i * stride);
        for (size_t j = 0; j < N; ++j) {
            out[i][j] = f[j];
        }
    }
    return out;
}

static std::vector<uint32_t> AccessorToIndices(const tinygltf::Model& model, int accessorIdx)
{
    const auto& acc = model.accessors[accessorIdx];
    const auto& bv = model.bufferViews[acc.bufferView];
    const auto& buf = model.buffers[bv.buffer];
    const uint8_t* src = buf.data.data() + bv.byteOffset + acc.byteOffset;
    std::vector<uint32_t> out(acc.count);
    for (size_t i = 0; i < acc.count; ++i) {
        switch (acc.componentType) {
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                out[i] = src[i];
                break;
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
                out[i] = reinterpret_cast<const uint16_t*>(src)[i];
                break;
            default:
                out[i] = reinterpret_cast<const uint32_t*>(src)[i];
                break;
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Scene::createMegaBuffers
// ---------------------------------------------------------------------------

void Scene::createMegaBuffers(ID3D12Device2* device)
{
    constexpr uint32_t initialVertexCapacity = 1'000'000;
    constexpr uint32_t initialIndexCapacity = 4'000'000;

    megaVBCapacity = initialVertexCapacity;
    megaIBCapacity = initialIndexCapacity;
    megaVBUsed = 0;
    megaIBUsed = 0;

    {
        const CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
        const CD3DX12_RESOURCE_DESC desc =
            CD3DX12_RESOURCE_DESC::Buffer(megaVBCapacity * sizeof(VertexPBR));
        chkDX(device->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr,
            IID_PPV_ARGS(&megaVB)
        ));
    }
    megaVBV.BufferLocation = megaVB->GetGPUVirtualAddress();
    megaVBV.SizeInBytes = megaVBCapacity * sizeof(VertexPBR);
    megaVBV.StrideInBytes = sizeof(VertexPBR);

    {
        const CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
        const CD3DX12_RESOURCE_DESC desc =
            CD3DX12_RESOURCE_DESC::Buffer(megaIBCapacity * sizeof(uint32_t));
        chkDX(device->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr,
            IID_PPV_ARGS(&megaIB)
        ));
    }
    megaIBV.BufferLocation = megaIB->GetGPUVirtualAddress();
    megaIBV.Format = DXGI_FORMAT_R32_UINT;
    megaIBV.SizeInBytes = megaIBCapacity * sizeof(uint32_t);

    spdlog::info(
        "Created mega-buffers: VB {}MB, IB {}MB",
        megaVBCapacity * sizeof(VertexPBR) / (1024 * 1024),
        megaIBCapacity * sizeof(uint32_t) / (1024 * 1024)
    );
}

// ---------------------------------------------------------------------------
// Scene::createDrawDataBuffers
// ---------------------------------------------------------------------------

void Scene::createDrawDataBuffers(ID3D12Device2* device)
{
    for (uint32_t i = 0; i < nBuffers; ++i) {
        const CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
        const CD3DX12_RESOURCE_DESC desc =
            CD3DX12_RESOURCE_DESC::Buffer(maxDrawsPerFrame * sizeof(SceneConstantBuffer));
        chkDX(device->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&drawDataBuffer[i])
        ));
        void* mapped = nullptr;
        chkDX(drawDataBuffer[i]->Map(0, nullptr, &mapped));
        drawDataMapped[i] = static_cast<SceneConstantBuffer*>(mapped);
    }

    {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.NumDescriptors = nBuffers;
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        chkDX(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&sceneSrvHeap)));
    }
    sceneSrvDescSize =
        device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    for (uint32_t i = 0; i < nBuffers; ++i) {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.NumElements = maxDrawsPerFrame;
        srvDesc.Buffer.StructureByteStride = sizeof(SceneConstantBuffer);
        srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

        CD3DX12_CPU_DESCRIPTOR_HANDLE handle(
            sceneSrvHeap->GetCPUDescriptorHandleForHeapStart(), static_cast<INT>(i),
            sceneSrvDescSize
        );
        device->CreateShaderResourceView(drawDataBuffer[i].Get(), &srvDesc, handle);
    }

    spdlog::info("Created draw-data structured buffers ({} max draws)", maxDrawsPerFrame);
}

// ---------------------------------------------------------------------------
// Scene::appendToMegaBuffers
// ---------------------------------------------------------------------------

MeshRef Scene::appendToMegaBuffers(
    ComPtr<ID3D12GraphicsCommandList2> cmdList,
    const std::vector<VertexPBR>& vertices,
    const std::vector<uint32_t>& indices,
    int materialIdx,
    std::vector<ComPtr<ID3D12Resource>>& temps
)
{
    uint32_t numVerts = static_cast<uint32_t>(vertices.size());
    uint32_t numIndices = static_cast<uint32_t>(indices.size());

    assert(megaVBUsed + numVerts <= megaVBCapacity && "Mega VB capacity exceeded");
    assert(megaIBUsed + numIndices <= megaIBCapacity && "Mega IB capacity exceeded");

    {
        size_t byteSize = numVerts * sizeof(VertexPBR);
        size_t dstOffset = megaVBUsed * sizeof(VertexPBR);
        ComPtr<ID3D12Resource> upload;
        const CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
        const CD3DX12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(byteSize);
        // NOTE: upload heap resource must be obtained from the calling code's device
        // We need the device here — get it from the existing megaVB resource's device
        ComPtr<ID3D12Device> dev;
        megaVB->GetDevice(IID_PPV_ARGS(&dev));
        chkDX(dev->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&upload)
        ));
        void* mapped = nullptr;
        chkDX(upload->Map(0, nullptr, &mapped));
        memcpy(mapped, vertices.data(), byteSize);
        upload->Unmap(0, nullptr);
        cmdList->CopyBufferRegion(megaVB.Get(), dstOffset, upload.Get(), 0, byteSize);
        temps.push_back(std::move(upload));
    }

    {
        size_t byteSize = numIndices * sizeof(uint32_t);
        size_t dstOffset = megaIBUsed * sizeof(uint32_t);
        ComPtr<ID3D12Resource> upload;
        const CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
        const CD3DX12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(byteSize);
        ComPtr<ID3D12Device> dev;
        megaIB->GetDevice(IID_PPV_ARGS(&dev));
        chkDX(dev->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&upload)
        ));
        void* mapped = nullptr;
        chkDX(upload->Map(0, nullptr, &mapped));
        memcpy(mapped, indices.data(), byteSize);
        upload->Unmap(0, nullptr);
        cmdList->CopyBufferRegion(megaIB.Get(), dstOffset, upload.Get(), 0, byteSize);
        temps.push_back(std::move(upload));
    }

    MeshRef ref;
    ref.vertexOffset = megaVBUsed;
    ref.indexOffset = megaIBUsed;
    ref.indexCount = numIndices;
    ref.materialIndex = materialIdx;

    megaVBUsed += numVerts;
    megaIBUsed += numIndices;

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
    megaVBUsed = 0;
    megaIBUsed = 0;
    ecsWorld.delete_with<MeshRef>();
    spawnableMeshRefs.clear();
    spawnTimer = 0.0f;
}

// ---------------------------------------------------------------------------
// Scene::loadTeapot
// ---------------------------------------------------------------------------

void Scene::loadTeapot(ID3D12Device2* device, CommandQueue& cmdQueue)
{
    auto cmdList = cmdQueue.getCmdList();
    std::vector<ComPtr<ID3D12Resource>> temps;

    std::string objData = GetResourceString(IDR_TEAPOT_OBJ);
    if (objData.empty()) {
        spdlog::error("Failed to load teapot OBJ from resource");
        return;
    }
    std::istringstream objStream(objData);

    class ResourceMaterialReader : public tinyobj::MaterialReader
    {
       public:
        bool operator()(
            const std::string&,
            std::vector<tinyobj::material_t>* mats,
            std::map<std::string, int>* matMap,
            std::string* warn,
            std::string* err
        ) override
        {
            std::string mtlData = GetResourceString(IDR_TEAPOT_MTL);
            if (mtlData.empty()) {
                if (warn) {
                    *warn = "Material resource not found";
                }
                return false;
            }
            std::istringstream mtlStream(mtlData);
            tinyobj::LoadMtl(matMap, mats, &mtlStream, warn, err);
            return true;
        }
    };
    ResourceMaterialReader matReader;

    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> objMats;
    std::string warn, err;
    if (!tinyobj::LoadObj(&attrib, &shapes, &objMats, &warn, &err, &objStream, &matReader)) {
        spdlog::error("tinyobj error: {}", err);
    }
    if (!warn.empty()) {
        spdlog::warn("tinyobj warn: {}", warn);
    }

    std::vector<VertexPBR> verts;
    std::vector<uint32_t> indices;
    for (const auto& shape : shapes) {
        for (const auto& idx : shape.mesh.indices) {
            VertexPBR v{};
            v.position = { attrib.vertices[3 * idx.vertex_index + 0],
                           attrib.vertices[3 * idx.vertex_index + 1],
                           attrib.vertices[3 * idx.vertex_index + 2] };
            v.normal = (idx.normal_index >= 0)
                           ? vec3{ attrib.normals[3 * idx.normal_index + 0],
                                   attrib.normals[3 * idx.normal_index + 1],
                                   attrib.normals[3 * idx.normal_index + 2] }
                           : vec3{ 0.0f, 1.0f, 0.0f };
            if (idx.texcoord_index >= 0) {
                v.uv = { attrib.texcoords[2 * idx.texcoord_index + 0],
                         attrib.texcoords[2 * idx.texcoord_index + 1] };
            }
            verts.push_back(v);
            indices.push_back(static_cast<uint32_t>(indices.size()));
        }
    }

    Material defMat;
    defMat.name = "Teapot";
    defMat.roughness = 0.3f;
    defMat.metallic = 0.0f;
    materials.push_back(defMat);

    MeshRef meshRef = appendToMegaBuffers(cmdList, verts, indices, 0, temps);
    spawnableMeshRefs.push_back(meshRef);
    Transform tf;
    ecsWorld.entity().set(tf).set(meshRef);

    uint64_t fv = cmdQueue.execCmdList(cmdList);
    cmdQueue.waitForFenceVal(fv);
}

// ---------------------------------------------------------------------------
// Scene::loadGltf
// ---------------------------------------------------------------------------

bool Scene::loadGltf(const std::string& path, ID3D12Device2* device, CommandQueue& cmdQueue)
{
    spdlog::info("loadGltf: {}", path);

    tinygltf::TinyGLTF loader;
    tinygltf::Model model;
    std::string err, warn;

    bool ok = false;
    if (path.size() >= 4 &&
        (path.substr(path.size() - 4) == ".glb" || path.substr(path.size() - 4) == ".GLB")) {
        ok = loader.LoadBinaryFromFile(&model, &err, &warn, path);
    } else {
        ok = loader.LoadASCIIFromFile(&model, &err, &warn, path);
    }

    if (!warn.empty()) {
        spdlog::warn("tinygltf: {}", warn);
    }
    if (!ok) {
        spdlog::error("tinygltf error: {}", err);
        return false;
    }

    clearScene(cmdQueue);

    if (model.materials.empty()) {
        Material def;
        def.name = "Default";
        materials.push_back(def);
    } else {
        for (const auto& gm : model.materials) {
            Material mat;
            mat.name = gm.name.empty() ? "Material" : gm.name;
            const auto& pbr = gm.pbrMetallicRoughness;
            if (pbr.baseColorFactor.size() == 4) {
                mat.albedo = { (float)pbr.baseColorFactor[0], (float)pbr.baseColorFactor[1],
                               (float)pbr.baseColorFactor[2], (float)pbr.baseColorFactor[3] };
            }
            mat.roughness = (float)pbr.roughnessFactor;
            mat.metallic = (float)pbr.metallicFactor;
            if (gm.emissiveFactor.size() == 3) {
                mat.emissive = { (float)gm.emissiveFactor[0], (float)gm.emissiveFactor[1],
                                 (float)gm.emissiveFactor[2], 0.0f };
                float maxE = std::max({ mat.emissive.x, mat.emissive.y, mat.emissive.z });
                if (maxE > 0.001f) {
                    mat.emissiveStrength = maxE;
                    mat.emissive.x /= maxE;
                    mat.emissive.y /= maxE;
                    mat.emissive.z /= maxE;
                }
            }
            materials.push_back(mat);
        }
    }

    auto cmdList = cmdQueue.getCmdList();
    std::vector<ComPtr<ID3D12Resource>> uploadTemps;

    const int sceneIdx = model.defaultScene >= 0 ? model.defaultScene : 0;
    if (sceneIdx >= (int)model.scenes.size()) {
        spdlog::error("No scenes in GLB");
        return false;
    }

    std::function<void(int, mat4)> visitNode = [&](int nodeIdx, mat4 parentTf) {
        const auto& node = model.nodes[nodeIdx];
        mat4 worldTf = NodeTransform(node) * parentTf;

        if (node.mesh >= 0) {
            const auto& gMesh = model.meshes[node.mesh];
            for (const auto& prim : gMesh.primitives) {
                if (prim.mode != TINYGLTF_MODE_TRIANGLES) {
                    continue;
                }

                auto posIt = prim.attributes.find("POSITION");
                if (posIt == prim.attributes.end()) {
                    continue;
                }
                auto positions = AccessorToFloatN<3>(model, posIt->second);

                std::vector<std::array<float, 3>> normals;
                auto normIt = prim.attributes.find("NORMAL");
                if (normIt != prim.attributes.end()) {
                    normals = AccessorToFloatN<3>(model, normIt->second);
                }

                std::vector<std::array<float, 2>> uvs;
                auto uvIt = prim.attributes.find("TEXCOORD_0");
                if (uvIt != prim.attributes.end()) {
                    uvs = AccessorToFloatN<2>(model, uvIt->second);
                }

                size_t numVerts = positions.size();
                std::vector<VertexPBR> verts(numVerts);
                for (size_t i = 0; i < numVerts; ++i) {
                    verts[i].position = { positions[i][0], positions[i][1], positions[i][2] };
                    verts[i].normal = normals.size() > i
                                          ? vec3{ normals[i][0], normals[i][1], normals[i][2] }
                                          : vec3{ 0.0f, 1.0f, 0.0f };
                    verts[i].uv =
                        uvs.size() > i ? vec2{ uvs[i][0], uvs[i][1] } : vec2{ 0.0f, 0.0f };
                }

                std::vector<uint32_t> indices;
                if (prim.indices >= 0) {
                    indices = AccessorToIndices(model, prim.indices);
                } else {
                    indices.resize(numVerts);
                    for (size_t i = 0; i < numVerts; ++i) {
                        indices[i] = (uint32_t)i;
                    }
                }

                int matIdx = (prim.material >= 0 && prim.material < (int)materials.size())
                                 ? prim.material
                                 : 0;
                MeshRef meshRef =
                    appendToMegaBuffers(cmdList, verts, indices, matIdx, uploadTemps);
                spawnableMeshRefs.push_back(meshRef);
                Transform tf;
                tf.world = worldTf;
                ecsWorld.entity().set(tf).set(meshRef);
            }
        }

        for (int child : node.children) {
            visitNode(child, worldTf);
        }
    };

    for (int nodeIdx : model.scenes[sceneIdx].nodes) {
        visitNode(nodeIdx, mat4{});
    }

    uint64_t fv = cmdQueue.execCmdList(cmdList);
    cmdQueue.waitForFenceVal(fv);

    spdlog::info(
        "Loaded GLB: {} entity(ies), {} material(s)", ecsWorld.count<MeshRef>(), materials.size()
    );
    return true;
}
