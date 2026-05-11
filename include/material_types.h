#pragma once

#include "math_types.h"

#include <string>

enum class MaterialPreset : int
{
    Diffuse = 0,
    Metal = 1,
    Mirror = 2,
    Count = 3
};

struct Material
{
    vec4 albedo{ 0.8f, 0.8f, 0.8f, 1.0f };
    float roughness{ 0.4f };
    float metallic{ 0.0f };
    float emissiveStrength{ 0.0f };
    bool reflective{ false };
    vec4 emissive{ 0.0f, 0.0f, 0.0f, 0.0f };
    std::string name;
    // Bindless heap indices for PBR texture maps (-1 = no texture, use factor only).
    // Only baseColor is wired up in the first PBR pass; others reserved for future.
    int albedoTexId{ -1 };
    int normalTexId{ -1 };
    int mrTexId{ -1 };  // metallic-roughness (g=roughness, b=metallic in glTF)
    int emissiveTexId{ -1 };
};
