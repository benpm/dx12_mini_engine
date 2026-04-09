// Separate TU for glaze so its heavy templates don't mix with Windows.h/DirectX headers
#include <glaze/glaze.hpp>

#include "camera_types.h"
#include "config_data.h"
#include "scene_data.h"
#include "terrain_types.h"

// glz::meta specializations for engine types with non-aggregate bases (user-declared constructors)
template <> struct glz::meta<OrbitCamera>
{
    using T = OrbitCamera;
    static constexpr auto value = glz::object(
        "fov",
        &T::fov,
        "nearPlane",
        &T::nearPlane,
        "farPlane",
        &T::farPlane,
        "yaw",
        &T::yaw,
        "pitch",
        &T::pitch,
        "radius",
        &T::radius
    );
};

template <> struct glz::meta<TerrainParams>
{
    using T = TerrainParams;
    static constexpr auto value = glz::object(
        "gridSize",
        &T::gridSize,
        "worldSize",
        &T::worldSize,
        "heightScale",
        &T::heightScale,
        "octaves",
        &T::octaves,
        "frequency",
        &T::frequency,
        "seed",
        &T::seed,
        "materialAlbedo",
        &T::materialAlbedo,
        "materialRoughness",
        &T::materialRoughness,
        "positionY",
        &T::positionY
    );
};

template <> struct glz::meta<vec2>
{
    using T = vec2;
    static constexpr auto value = glz::object("x", &T::x, "y", &T::y);
};

template <> struct glz::meta<vec3>
{
    using T = vec3;
    static constexpr auto value = glz::object("x", &T::x, "y", &T::y, "z", &T::z);
};

template <> struct glz::meta<vec4>
{
    using T = vec4;
    static constexpr auto value = glz::object("x", &T::x, "y", &T::y, "z", &T::z, "w", &T::w);
};

template <> struct glz::meta<PointLight>
{
    using T = PointLight;
    static constexpr auto value =
        glz::object("center", &T::center, "amp", &T::amp, "freq", &T::freq, "color", &T::color);
};

template <> struct glz::meta<Animated>
{
    using T = Animated;
    static constexpr auto value = glz::object(
        "speed",
        &T::speed,
        "orbitRadius",
        &T::orbitRadius,
        "orbitY",
        &T::orbitY,
        "initialScale",
        &T::initialScale,
        "rotAxis",
        &T::rotAxis,
        "rotAngle",
        &T::rotAngle,
        "pulsePhase",
        &T::pulsePhase
    );
};

template <> struct glz::meta<Material>
{
    using T = Material;
    static constexpr auto value = glz::object(
        "name",
        &T::name,
        "albedo",
        &T::albedo,
        "roughness",
        &T::roughness,
        "metallic",
        &T::metallic,
        "emissiveStrength",
        &T::emissiveStrength,
        "reflective",
        &T::reflective,
        "emissive",
        &T::emissive
    );
};

template <> struct glz::meta<InstanceGroupData>
{
    using T = InstanceGroupData;
    static constexpr auto value = glz::object(
        "meshName",
        &T::meshName,
        "materialName",
        &T::materialName,
        "positions",
        &T::positions,
        "scales",
        &T::scales,
        "albedoOverrides",
        &T::albedoOverrides
    );
};

bool readConfigJson(const std::string& path, ConfigData& out, std::string& err)
{
    std::string buf;
    // Read file contents, then parse with unknown keys allowed (config may have obsolete keys)
    auto fec = glz::file_to_buffer(buf, path);
    if (fec != glz::error_code::none) {
        err = "Failed to read file";
        return false;
    }
    auto ec = glz::read<glz::opts{ .error_on_unknown_keys = false }>(out, buf);
    if (ec) {
        err = glz::format_error(ec, buf);
        return false;
    }
    return true;
}

bool writeConfigJson(const std::string& path, const ConfigData& data, std::string& err)
{
    auto ec = glz::write_file_json<glz::opts{ .prettify = true }>(data, path, std::string{});
    if (ec) {
        err = glz::format_error(ec, std::string{});
        return false;
    }
    return true;
}

bool readSceneJson(const std::string& path, SceneFileData& out, std::string& err)
{
    std::string buf;
    auto ec = glz::read_file_json(out, path, buf);
    if (ec) {
        err = glz::format_error(ec, buf);
        return false;
    }
    return true;
}

bool writeSceneJson(const std::string& path, const SceneFileData& data, std::string& err)
{
    auto ec = glz::write_file_json<glz::opts{ .prettify = true }>(data, path, std::string{});
    if (ec) {
        err = glz::format_error(ec, std::string{});
        return false;
    }
    return true;
}
