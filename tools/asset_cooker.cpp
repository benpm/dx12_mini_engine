// asset_cooker — CLI tool that walks an asset directory, parses every GLB
// with tinygltf, and emits a JSON manifest listing each model's meshes,
// materials, primitive counts, and bounding boxes. This is the metadata half
// of a full cooker pipeline; binary blob output (KTX2 textures, pre-packed
// mega VB/IB) is staged for a follow-up.
//
// Usage: asset_cooker.exe <input-dir> <output-manifest.json>
//
// Manifest format (hand-written so we don't need glaze as a dep here):
// {
//   "version": 1,
//   "assets": [
//     {
//       "path": "...glb",
//       "meshes": [{ "name": "...", "primitives": N, "vertices": N, "indices": N }],
//       "materials": [{ "name": "...", "hasBaseColorTex": true, ... }],
//       "boundsMin": [x, y, z],
//       "boundsMax": [x, y, z]
//     }
//   ]
// }

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <tiny_gltf.h>

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace fs = std::filesystem;

struct MeshInfo
{
    std::string name;
    int primitives = 0;
    uint32_t vertices = 0;
    uint32_t indices = 0;
};

struct MaterialInfo
{
    std::string name;
    bool hasBaseColorTex = false;
    bool hasNormalTex = false;
    bool hasMrTex = false;
    bool hasEmissiveTex = false;
};

struct AssetInfo
{
    std::string path;
    std::vector<MeshInfo> meshes;
    std::vector<MaterialInfo> materials;
    float boundsMin[3] = { 0, 0, 0 };
    float boundsMax[3] = { 0, 0, 0 };
};

static std::string escapeJson(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 2);
    for (char c : s) {
        switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                out += c;
        }
    }
    return out;
}

static bool cookOne(const fs::path& glbPath, AssetInfo& out)
{
    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    std::string err, warn;
    bool ok = loader.LoadBinaryFromFile(&model, &err, &warn, glbPath.string());
    if (!ok) {
        ok = loader.LoadASCIIFromFile(&model, &err, &warn, glbPath.string());
    }
    if (!ok) {
        std::cerr << "FAIL " << glbPath << ": " << err << "\n";
        return false;
    }

    out.path = glbPath.string();
    float gMin[3] = { std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity(),
                      std::numeric_limits<float>::infinity() };
    float gMax[3] = { -std::numeric_limits<float>::infinity(),
                      -std::numeric_limits<float>::infinity(),
                      -std::numeric_limits<float>::infinity() };

    for (const auto& m : model.meshes) {
        MeshInfo mi;
        mi.name = m.name;
        mi.primitives = static_cast<int>(m.primitives.size());
        for (const auto& p : m.primitives) {
            auto posIt = p.attributes.find("POSITION");
            if (posIt != p.attributes.end()) {
                const auto& acc = model.accessors[posIt->second];
                mi.vertices += static_cast<uint32_t>(acc.count);
                if (acc.minValues.size() == 3 && acc.maxValues.size() == 3) {
                    for (int k = 0; k < 3; ++k) {
                        gMin[k] = std::min(gMin[k], static_cast<float>(acc.minValues[k]));
                        gMax[k] = std::max(gMax[k], static_cast<float>(acc.maxValues[k]));
                    }
                }
            }
            if (p.indices >= 0) {
                mi.indices += static_cast<uint32_t>(model.accessors[p.indices].count);
            }
        }
        out.meshes.push_back(std::move(mi));
    }
    for (const auto& gm : model.materials) {
        MaterialInfo mat;
        mat.name = gm.name;
        mat.hasBaseColorTex = gm.pbrMetallicRoughness.baseColorTexture.index >= 0;
        mat.hasNormalTex = gm.normalTexture.index >= 0;
        mat.hasMrTex = gm.pbrMetallicRoughness.metallicRoughnessTexture.index >= 0;
        mat.hasEmissiveTex = gm.emissiveTexture.index >= 0;
        out.materials.push_back(std::move(mat));
    }
    for (int k = 0; k < 3; ++k) {
        out.boundsMin[k] = std::isfinite(gMin[k]) ? gMin[k] : 0.0f;
        out.boundsMax[k] = std::isfinite(gMax[k]) ? gMax[k] : 0.0f;
    }
    return true;
}

int main(int argc, char** argv)
{
    if (argc < 3) {
        std::cerr << "Usage: asset_cooker <input-dir> <output-manifest.json>\n";
        return 1;
    }
    fs::path inDir(argv[1]);
    fs::path outFile(argv[2]);
    if (!fs::is_directory(inDir)) {
        std::cerr << "Input dir does not exist: " << inDir << "\n";
        return 2;
    }

    std::vector<AssetInfo> assets;
    for (auto it = fs::recursive_directory_iterator(inDir); it != fs::recursive_directory_iterator();
         ++it) {
        if (it->is_regular_file() && it->path().extension() == ".glb") {
            AssetInfo info;
            if (cookOne(it->path(), info)) {
                std::cout << "OK " << it->path() << "  (" << info.meshes.size() << " meshes, "
                          << info.materials.size() << " materials)\n";
                assets.push_back(std::move(info));
            }
        }
    }

    std::ofstream out(outFile, std::ios::binary);
    if (!out) {
        std::cerr << "Cannot write " << outFile << "\n";
        return 3;
    }
    out << "{\n  \"version\": 1,\n  \"assets\": [\n";
    for (size_t i = 0; i < assets.size(); ++i) {
        const auto& a = assets[i];
        out << "    {\n";
        out << "      \"path\": \"" << escapeJson(a.path) << "\",\n";
        out << "      \"boundsMin\": [" << a.boundsMin[0] << ", " << a.boundsMin[1] << ", "
            << a.boundsMin[2] << "],\n";
        out << "      \"boundsMax\": [" << a.boundsMax[0] << ", " << a.boundsMax[1] << ", "
            << a.boundsMax[2] << "],\n";
        out << "      \"meshes\": [\n";
        for (size_t j = 0; j < a.meshes.size(); ++j) {
            const auto& m = a.meshes[j];
            out << "        { \"name\": \"" << escapeJson(m.name) << "\", \"primitives\": "
                << m.primitives << ", \"vertices\": " << m.vertices << ", \"indices\": "
                << m.indices << " }" << (j + 1 < a.meshes.size() ? "," : "") << "\n";
        }
        out << "      ],\n      \"materials\": [\n";
        for (size_t j = 0; j < a.materials.size(); ++j) {
            const auto& m = a.materials[j];
            out << "        { \"name\": \"" << escapeJson(m.name)
                << "\", \"hasBaseColorTex\": " << (m.hasBaseColorTex ? "true" : "false")
                << ", \"hasNormalTex\": " << (m.hasNormalTex ? "true" : "false")
                << ", \"hasMrTex\": " << (m.hasMrTex ? "true" : "false")
                << ", \"hasEmissiveTex\": " << (m.hasEmissiveTex ? "true" : "false") << " }"
                << (j + 1 < a.materials.size() ? "," : "") << "\n";
        }
        out << "      ]\n    }" << (i + 1 < assets.size() ? "," : "") << "\n";
    }
    out << "  ]\n}\n";
    std::cout << "Wrote manifest: " << outFile << "  (" << assets.size() << " assets)\n";
    return 0;
}
