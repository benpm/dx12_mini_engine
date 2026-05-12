// Isolated TU for Lua C API calls — same pattern as glaze_impl.cpp.
// Includes lua.h directly, provides extern "C" functions consumed by the module.

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#if defined(__clang__)
    #define FMT_CONSTEVAL
#endif

#include <spdlog/spdlog.h>

#include <flecs.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>
#include "audio_capi.h"
#include "ecs_types.h"
#include "material_types.h"
#include "math_types.h"

// ---------------------------------------------------------------------------
// Registry keys for engine pointers
// ---------------------------------------------------------------------------
static const char* kRegScene = "engine_scene";
static const char* kRegSelected = "engine_selected";
static const char* kRegPendingDestroys = "engine_pending_destroys";
static const char* kRegDt = "engine_dt";
static const char* kRegTime = "engine_time";
static const char* kRegFrameCount = "engine_frame_count";
static const char* kRegAudio = "engine_audio";
static const char* kRegApp = "engine_app";
static const char* kRegHud = "engine_hud";
static const char* kRegParticles = "engine_particles";
static const char* kRegPhysics = "engine_physics";
static const char* kRegScenePtr = "engine_scene_ptr";  // opaque Scene*

// Helper: retrieve Scene* from Lua registry
static flecs::world* getEcsWorld(lua_State* L)
{
    lua_getfield(L, LUA_REGISTRYINDEX, kRegScene);
    auto* w = static_cast<flecs::world*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return w;
}

static std::vector<Material>* getMaterials(lua_State* L)
{
    lua_getfield(L, LUA_REGISTRYINDEX, "engine_materials");
    auto* m = static_cast<std::vector<Material>*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return m;
}

static std::vector<MeshRef>* getSpawnableMeshRefs(lua_State* L)
{
    lua_getfield(L, LUA_REGISTRYINDEX, "engine_mesh_refs");
    auto* m = static_cast<std::vector<MeshRef>*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return m;
}

static std::vector<std::string>* getSpawnableMeshNames(lua_State* L)
{
    lua_getfield(L, LUA_REGISTRYINDEX, "engine_mesh_names");
    auto* m = static_cast<std::vector<std::string>*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return m;
}

static std::vector<uint64_t>* getPendingDestroys(lua_State* L)
{
    lua_getfield(L, LUA_REGISTRYINDEX, kRegPendingDestroys);
    auto* v = static_cast<std::vector<uint64_t>*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return v;
}

// ---------------------------------------------------------------------------
// Entity Operations
// ---------------------------------------------------------------------------
static int l_create_entity(lua_State* L)
{
    auto* w = getEcsWorld(L);
    if (!w) {
        return luaL_error(L, "engine not initialized");
    }
    auto e = w->entity();
    lua_pushinteger(L, static_cast<lua_Integer>(e.id()));
    return 1;
}

static int l_destroy_entity(lua_State* L)
{
    uint64_t id = static_cast<uint64_t>(luaL_checkinteger(L, 1));
    auto* destroys = getPendingDestroys(L);
    if (destroys) {
        destroys->push_back(id);
    }
    return 0;
}

static int l_entity_is_alive(lua_State* L)
{
    auto* w = getEcsWorld(L);
    if (!w) {
        return luaL_error(L, "engine not initialized");
    }
    uint64_t id = static_cast<uint64_t>(luaL_checkinteger(L, 1));
    auto e = w->entity(id);
    lua_pushboolean(L, e.is_alive());
    return 1;
}

static int l_get_selected_entity(lua_State* L)
{
    lua_getfield(L, LUA_REGISTRYINDEX, kRegSelected);
    uint64_t id = static_cast<uint64_t>(lua_tointeger(L, -1));
    lua_pop(L, 1);
    if (id == 0) {
        lua_pushnil(L);
    } else {
        lua_pushinteger(L, static_cast<lua_Integer>(id));
    }
    return 1;
}

static int l_set_selected_entity(lua_State* L)
{
    lua_Integer id = luaL_checkinteger(L, 1);
    lua_pushinteger(L, id);
    lua_setfield(L, LUA_REGISTRYINDEX, kRegSelected);
    return 0;
}

// ---------------------------------------------------------------------------
// Transform
// ---------------------------------------------------------------------------
static int l_get_position(lua_State* L)
{
    auto* w = getEcsWorld(L);
    if (!w) {
        return luaL_error(L, "engine not initialized");
    }
    uint64_t id = static_cast<uint64_t>(luaL_checkinteger(L, 1));
    auto e = w->entity(id);
    if (!e.is_alive() || !e.has<Transform>()) {
        lua_pushnumber(L, 0);
        lua_pushnumber(L, 0);
        lua_pushnumber(L, 0);
        return 3;
    }
    const auto& tf = e.get<Transform>();
    lua_pushnumber(L, tf.world._41);
    lua_pushnumber(L, tf.world._42);
    lua_pushnumber(L, tf.world._43);
    return 3;
}

static int l_set_position(lua_State* L)
{
    auto* w = getEcsWorld(L);
    if (!w) {
        return luaL_error(L, "engine not initialized");
    }
    uint64_t id = static_cast<uint64_t>(luaL_checkinteger(L, 1));
    float x = static_cast<float>(luaL_checknumber(L, 2));
    float y = static_cast<float>(luaL_checknumber(L, 3));
    float z = static_cast<float>(luaL_checknumber(L, 4));
    auto e = w->entity(id);
    if (!e.is_alive()) {
        return 0;
    }
    if (!e.has<Transform>()) {
        Transform tf;
        tf.world = translate(x, y, z);
        e.set(tf);
    } else {
        auto& tf = e.get_mut<Transform>();
        tf.world._41 = x;
        tf.world._42 = y;
        tf.world._43 = z;
    }
    return 0;
}

static int l_get_scale(lua_State* L)
{
    auto* w = getEcsWorld(L);
    if (!w) {
        return luaL_error(L, "engine not initialized");
    }
    uint64_t id = static_cast<uint64_t>(luaL_checkinteger(L, 1));
    auto e = w->entity(id);
    if (!e.is_alive() || !e.has<Transform>()) {
        lua_pushnumber(L, 1);
        lua_pushnumber(L, 1);
        lua_pushnumber(L, 1);
        return 3;
    }
    const auto& tf = e.get<Transform>();
    float sx = std::sqrt(
        tf.world._11 * tf.world._11 + tf.world._12 * tf.world._12 + tf.world._13 * tf.world._13
    );
    float sy = std::sqrt(
        tf.world._21 * tf.world._21 + tf.world._22 * tf.world._22 + tf.world._23 * tf.world._23
    );
    float sz = std::sqrt(
        tf.world._31 * tf.world._31 + tf.world._32 * tf.world._32 + tf.world._33 * tf.world._33
    );
    lua_pushnumber(L, sx);
    lua_pushnumber(L, sy);
    lua_pushnumber(L, sz);
    return 3;
}

static int l_set_scale(lua_State* L)
{
    auto* w = getEcsWorld(L);
    if (!w) {
        return luaL_error(L, "engine not initialized");
    }
    uint64_t id = static_cast<uint64_t>(luaL_checkinteger(L, 1));
    float s = static_cast<float>(luaL_checknumber(L, 2));
    auto e = w->entity(id);
    if (!e.is_alive() || !e.has<Transform>()) {
        return 0;
    }
    auto& tf = e.get_mut<Transform>();
    float px = tf.world._41, py = tf.world._42, pz = tf.world._43;
    tf.world = scale(s, s, s) * translate(px, py, pz);
    return 0;
}

static int l_set_rotation(lua_State* L)
{
    auto* w = getEcsWorld(L);
    if (!w) {
        return luaL_error(L, "engine not initialized");
    }
    uint64_t id = static_cast<uint64_t>(luaL_checkinteger(L, 1));
    float ax = static_cast<float>(luaL_checknumber(L, 2));
    float ay = static_cast<float>(luaL_checknumber(L, 3));
    float az = static_cast<float>(luaL_checknumber(L, 4));
    float angle = static_cast<float>(luaL_checknumber(L, 5));
    auto e = w->entity(id);
    if (!e.is_alive() || !e.has<Transform>()) {
        return 0;
    }
    auto& tf = e.get_mut<Transform>();
    float px = tf.world._41, py = tf.world._42, pz = tf.world._43;
    float sx = std::sqrt(
        tf.world._11 * tf.world._11 + tf.world._12 * tf.world._12 + tf.world._13 * tf.world._13
    );
    tf.world = scale(sx) * rotateAxis(vec3(ax, ay, az), angle) * translate(px, py, pz);
    return 0;
}

// ---------------------------------------------------------------------------
// MeshRef / Materials
// ---------------------------------------------------------------------------
static int l_get_material_index(lua_State* L)
{
    auto* w = getEcsWorld(L);
    if (!w) {
        return luaL_error(L, "engine not initialized");
    }
    uint64_t id = static_cast<uint64_t>(luaL_checkinteger(L, 1));
    auto e = w->entity(id);
    if (!e.is_alive() || !e.has<MeshRef>()) {
        lua_pushinteger(L, -1);
        return 1;
    }
    lua_pushinteger(L, e.get<MeshRef>().materialIndex);
    return 1;
}

static int l_set_material_index(lua_State* L)
{
    auto* w = getEcsWorld(L);
    if (!w) {
        return luaL_error(L, "engine not initialized");
    }
    uint64_t id = static_cast<uint64_t>(luaL_checkinteger(L, 1));
    int idx = static_cast<int>(luaL_checkinteger(L, 2));
    auto e = w->entity(id);
    if (!e.is_alive() || !e.has<MeshRef>()) {
        return 0;
    }
    e.get_mut<MeshRef>().materialIndex = idx;
    return 0;
}

static int l_set_albedo_override(lua_State* L)
{
    auto* w = getEcsWorld(L);
    if (!w) {
        return luaL_error(L, "engine not initialized");
    }
    uint64_t id = static_cast<uint64_t>(luaL_checkinteger(L, 1));
    float r = static_cast<float>(luaL_checknumber(L, 2));
    float g = static_cast<float>(luaL_checknumber(L, 3));
    float b = static_cast<float>(luaL_checknumber(L, 4));
    float a = static_cast<float>(luaL_checknumber(L, 5));
    auto e = w->entity(id);
    if (!e.is_alive() || !e.has<MeshRef>()) {
        return 0;
    }
    e.get_mut<MeshRef>().albedoOverride = vec4(r, g, b, a);
    return 0;
}

static int l_get_material_count(lua_State* L)
{
    auto* mats = getMaterials(L);
    lua_pushinteger(L, mats ? static_cast<lua_Integer>(mats->size()) : 0);
    return 1;
}

static int l_set_material_albedo(lua_State* L)
{
    auto* mats = getMaterials(L);
    if (!mats) {
        return luaL_error(L, "engine not initialized");
    }
    int idx = static_cast<int>(luaL_checkinteger(L, 1));
    if (idx < 0 || idx >= static_cast<int>(mats->size())) {
        return luaL_error(L, "invalid index");
    }
    (*mats)[idx].albedo = { static_cast<float>(luaL_checknumber(L, 2)),
                            static_cast<float>(luaL_checknumber(L, 3)),
                            static_cast<float>(luaL_checknumber(L, 4)),
                            static_cast<float>(luaL_checknumber(L, 5)) };
    return 0;
}

static int l_set_material_roughness(lua_State* L)
{
    auto* mats = getMaterials(L);
    if (!mats) {
        return luaL_error(L, "engine not initialized");
    }
    int idx = static_cast<int>(luaL_checkinteger(L, 1));
    if (idx < 0 || idx >= static_cast<int>(mats->size())) {
        return luaL_error(L, "invalid index");
    }
    (*mats)[idx].roughness = static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

static int l_set_material_metallic(lua_State* L)
{
    auto* mats = getMaterials(L);
    if (!mats) {
        return luaL_error(L, "engine not initialized");
    }
    int idx = static_cast<int>(luaL_checkinteger(L, 1));
    if (idx < 0 || idx >= static_cast<int>(mats->size())) {
        return luaL_error(L, "invalid index");
    }
    (*mats)[idx].metallic = static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}

static int l_set_material_emissive(lua_State* L)
{
    auto* mats = getMaterials(L);
    if (!mats) {
        return luaL_error(L, "engine not initialized");
    }
    int idx = static_cast<int>(luaL_checkinteger(L, 1));
    if (idx < 0 || idx >= static_cast<int>(mats->size())) {
        return luaL_error(L, "invalid index");
    }
    (*mats)[idx].emissive = { static_cast<float>(luaL_checknumber(L, 2)),
                              static_cast<float>(luaL_checknumber(L, 3)),
                              static_cast<float>(luaL_checknumber(L, 4)), 1.0f };
    (*mats)[idx].emissiveStrength = static_cast<float>(luaL_checknumber(L, 5));
    return 0;
}

// ---------------------------------------------------------------------------
// Component Management
// ---------------------------------------------------------------------------
static int l_has_component(lua_State* L)
{
    auto* w = getEcsWorld(L);
    if (!w) {
        return luaL_error(L, "engine not initialized");
    }
    uint64_t id = static_cast<uint64_t>(luaL_checkinteger(L, 1));
    const char* name = luaL_checkstring(L, 2);
    auto e = w->entity(id);
    if (!e.is_alive()) {
        lua_pushboolean(L, false);
        return 1;
    }
    bool has = false;
    if (std::strcmp(name, "Transform") == 0) {
        has = e.has<Transform>();
    } else if (std::strcmp(name, "MeshRef") == 0) {
        has = e.has<MeshRef>();
    } else if (std::strcmp(name, "Animated") == 0) {
        has = e.has<Animated>();
    } else if (std::strcmp(name, "Pickable") == 0) {
        has = e.has<Pickable>();
    } else if (std::strcmp(name, "PointLight") == 0) {
        has = e.has<PointLight>();
    } else if (std::strcmp(name, "Scripted") == 0) {
        has = e.has<Scripted>();
    } else if (std::strcmp(name, "InstanceGroup") == 0) {
        has = e.has<InstanceGroup>();
    }
    lua_pushboolean(L, has);
    return 1;
}

static int l_add_pickable(lua_State* L)
{
    auto* w = getEcsWorld(L);
    if (!w) {
        return luaL_error(L, "engine not initialized");
    }
    uint64_t id = static_cast<uint64_t>(luaL_checkinteger(L, 1));
    auto e = w->entity(id);
    if (e.is_alive()) {
        e.add<Pickable>();
    }
    return 0;
}

static int l_remove_pickable(lua_State* L)
{
    auto* w = getEcsWorld(L);
    if (!w) {
        return luaL_error(L, "engine not initialized");
    }
    uint64_t id = static_cast<uint64_t>(luaL_checkinteger(L, 1));
    auto e = w->entity(id);
    if (e.is_alive()) {
        e.remove<Pickable>();
    }
    return 0;
}

static int l_add_animated(lua_State* L)
{
    auto* w = getEcsWorld(L);
    if (!w) {
        return luaL_error(L, "engine not initialized");
    }
    uint64_t id = static_cast<uint64_t>(luaL_checkinteger(L, 1));
    float speed = static_cast<float>(luaL_checknumber(L, 2));
    float radius = static_cast<float>(luaL_checknumber(L, 3));
    float y = static_cast<float>(luaL_checknumber(L, 4));
    auto e = w->entity(id);
    if (!e.is_alive()) {
        return 0;
    }
    Animated a{};
    a.speed = speed;
    a.orbitRadius = radius;
    a.orbitY = y;
    a.initialScale = 1.0f;
    a.rotAxis = vec3(0, 1, 0);
    e.set(a);
    return 0;
}

static int l_remove_animated(lua_State* L)
{
    auto* w = getEcsWorld(L);
    if (!w) {
        return luaL_error(L, "engine not initialized");
    }
    uint64_t id = static_cast<uint64_t>(luaL_checkinteger(L, 1));
    auto e = w->entity(id);
    if (e.is_alive()) {
        e.remove<Animated>();
    }
    return 0;
}

static int l_add_point_light(lua_State* L)
{
    auto* w = getEcsWorld(L);
    if (!w) {
        return luaL_error(L, "engine not initialized");
    }
    uint64_t id = static_cast<uint64_t>(luaL_checkinteger(L, 1));
    PointLight pl{};
    pl.center = { static_cast<float>(luaL_checknumber(L, 2)),
                  static_cast<float>(luaL_checknumber(L, 3)),
                  static_cast<float>(luaL_checknumber(L, 4)) };
    pl.color = { static_cast<float>(luaL_checknumber(L, 5)),
                 static_cast<float>(luaL_checknumber(L, 6)),
                 static_cast<float>(luaL_checknumber(L, 7)),
                 static_cast<float>(luaL_checknumber(L, 8)) };
    auto e = w->entity(id);
    if (e.is_alive()) {
        e.set(pl);
    }
    return 0;
}

static int l_get_light_color(lua_State* L)
{
    auto* w = getEcsWorld(L);
    if (!w) {
        return luaL_error(L, "engine not initialized");
    }
    uint64_t id = static_cast<uint64_t>(luaL_checkinteger(L, 1));
    auto e = w->entity(id);
    if (!e.is_alive() || !e.has<PointLight>()) {
        lua_pushnumber(L, 0);
        lua_pushnumber(L, 0);
        lua_pushnumber(L, 0);
        lua_pushnumber(L, 0);
        return 4;
    }
    const auto& pl = e.get<PointLight>();
    lua_pushnumber(L, pl.color.x);
    lua_pushnumber(L, pl.color.y);
    lua_pushnumber(L, pl.color.z);
    lua_pushnumber(L, pl.color.w);
    return 4;
}

static int l_set_light_color(lua_State* L)
{
    auto* w = getEcsWorld(L);
    if (!w) {
        return luaL_error(L, "engine not initialized");
    }
    uint64_t id = static_cast<uint64_t>(luaL_checkinteger(L, 1));
    auto e = w->entity(id);
    if (!e.is_alive() || !e.has<PointLight>()) {
        return 0;
    }
    e.get_mut<PointLight>().color = { static_cast<float>(luaL_checknumber(L, 2)),
                                      static_cast<float>(luaL_checknumber(L, 3)),
                                      static_cast<float>(luaL_checknumber(L, 4)),
                                      static_cast<float>(luaL_checknumber(L, 5)) };
    return 0;
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------
static int l_get_all_entities(lua_State* L)
{
    auto* w = getEcsWorld(L);
    if (!w) {
        return luaL_error(L, "engine not initialized");
    }
    lua_newtable(L);
    int idx = 1;
    auto q = w->query_builder<const Transform, const MeshRef>().build();
    q.each([&](flecs::entity e, const Transform&, const MeshRef&) {
        lua_pushinteger(L, static_cast<lua_Integer>(e.id()));
        lua_rawseti(L, -2, idx++);
    });
    return 1;
}

static int l_get_entities_with(lua_State* L)
{
    auto* w = getEcsWorld(L);
    if (!w) {
        return luaL_error(L, "engine not initialized");
    }
    const char* name = luaL_checkstring(L, 1);
    lua_newtable(L);
    int idx = 1;

    auto collect = [&](auto&& query) {
        query.each([&](flecs::entity e, auto&&...) {
            lua_pushinteger(L, static_cast<lua_Integer>(e.id()));
            lua_rawseti(L, -2, idx++);
        });
    };

    if (std::strcmp(name, "Animated") == 0) {
        collect(w->query_builder<const Animated>().build());
    } else if (std::strcmp(name, "Pickable") == 0) {
        collect(w->query_builder<const Pickable>().build());
    } else if (std::strcmp(name, "PointLight") == 0) {
        collect(w->query_builder<const PointLight>().build());
    } else if (std::strcmp(name, "Scripted") == 0) {
        collect(w->query_builder<const Scripted>().build());
    } else if (std::strcmp(name, "MeshRef") == 0) {
        collect(w->query_builder<const MeshRef>().build());
    } else if (std::strcmp(name, "Transform") == 0) {
        collect(w->query_builder<const Transform>().build());
    }

    return 1;
}

static int l_get_entity_count(lua_State* L)
{
    auto* w = getEcsWorld(L);
    if (!w) {
        return luaL_error(L, "engine not initialized");
    }
    int count = 0;
    auto q = w->query_builder<const Transform, const MeshRef>().build();
    q.each([&](flecs::entity, const Transform&, const MeshRef&) { ++count; });
    lua_pushinteger(L, count);
    return 1;
}

// ---------------------------------------------------------------------------
// Spawning
// ---------------------------------------------------------------------------
static int l_get_mesh_names(lua_State* L)
{
    auto* names = getSpawnableMeshNames(L);
    if (!names) {
        lua_newtable(L);
        return 1;
    }
    lua_newtable(L);
    for (size_t i = 0; i < names->size(); ++i) {
        lua_pushstring(L, (*names)[i].c_str());
        lua_rawseti(L, -2, static_cast<int>(i + 1));
    }
    return 1;
}

static int l_get_mesh_count(lua_State* L)
{
    auto* refs = getSpawnableMeshRefs(L);
    lua_pushinteger(L, refs ? static_cast<lua_Integer>(refs->size()) : 0);
    return 1;
}

static int l_spawn_entity(lua_State* L)
{
    auto* w = getEcsWorld(L);
    auto* refs = getSpawnableMeshRefs(L);
    if (!w || !refs || refs->empty()) {
        return luaL_error(L, "engine not initialized or no meshes");
    }

    int meshIdx = static_cast<int>(luaL_checkinteger(L, 1));
    int matIdx = static_cast<int>(luaL_checkinteger(L, 2));
    float x = static_cast<float>(luaL_checknumber(L, 3));
    float y = static_cast<float>(luaL_checknumber(L, 4));
    float z = static_cast<float>(luaL_checknumber(L, 5));
    float s = static_cast<float>(luaL_optnumber(L, 6, 1.0));

    meshIdx = std::clamp(meshIdx, 0, static_cast<int>(refs->size()) - 1);
    MeshRef mesh = (*refs)[meshIdx];

    auto* mats = getMaterials(L);
    if (mats && !mats->empty()) {
        matIdx = std::clamp(matIdx, 0, static_cast<int>(mats->size()) - 1);
        mesh.materialIndex = matIdx;
    }

    Transform tf;
    tf.world = scale(s) * translate(x, y, z);
    auto e = w->entity().set(tf).set(mesh).add<Pickable>();
    lua_pushinteger(L, static_cast<lua_Integer>(e.id()));
    return 1;
}

// ---------------------------------------------------------------------------
// Editor Actions
// ---------------------------------------------------------------------------
static int l_execute_action(lua_State* L)
{
    // This is a stub — the actual action execution is handled by the module
    // layer which has access to the EditorAction system. We store the action
    // name in a registry field for the module to pick up.
    const char* name = luaL_checkstring(L, 1);
    lua_pushstring(L, name);
    lua_setfield(L, LUA_REGISTRYINDEX, "engine_pending_action");
    return 0;
}

// ---------------------------------------------------------------------------
// Utilities
// ---------------------------------------------------------------------------
static int l_log(lua_State* L)
{
    const char* msg = luaL_checkstring(L, 1);
    spdlog::info("[lua] {}", msg);
    return 0;
}

static int l_log_warn(lua_State* L)
{
    const char* msg = luaL_checkstring(L, 1);
    spdlog::warn("[lua] {}", msg);
    return 0;
}

static int l_log_error(lua_State* L)
{
    const char* msg = luaL_checkstring(L, 1);
    spdlog::error("[lua] {}", msg);
    return 0;
}

static int l_get_dt(lua_State* L)
{
    lua_getfield(L, LUA_REGISTRYINDEX, kRegDt);
    return 1;
}

static int l_get_time(lua_State* L)
{
    lua_getfield(L, LUA_REGISTRYINDEX, kRegTime);
    return 1;
}

static int l_get_frame_count(lua_State* L)
{
    lua_getfield(L, LUA_REGISTRYINDEX, kRegFrameCount);
    return 1;
}

static int l_play_sound(lua_State* L)
{
    const char* path = luaL_checkstring(L, 1);
    float volume = static_cast<float>(luaL_optnumber(L, 2, 1.0));
    lua_getfield(L, LUA_REGISTRYINDEX, kRegAudio);
    void* audio = lua_touserdata(L, -1);
    lua_pop(L, 1);
    int ok = engine_audio_play_sound(audio, path, volume);
    lua_pushboolean(L, ok);
    return 1;
}

static int l_save_scene(lua_State* L)
{
    const char* path = luaL_checkstring(L, 1);
    lua_getfield(L, LUA_REGISTRYINDEX, kRegApp);
    void* app = lua_touserdata(L, -1);
    lua_pop(L, 1);
    lua_pushboolean(L, engine_app_queue_scene_save(app, path));
    return 1;
}

static int l_load_scene(lua_State* L)
{
    const char* path = luaL_checkstring(L, 1);
    lua_getfield(L, LUA_REGISTRYINDEX, kRegApp);
    void* app = lua_touserdata(L, -1);
    lua_pop(L, 1);
    lua_pushboolean(L, engine_app_queue_scene_load(app, path));
    return 1;
}

static int l_save_game(lua_State* L)
{
    const char* slot = luaL_checkstring(L, 1);
    char path[260];
    if (!engine_save_slot_path(slot, path, sizeof(path))) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_getfield(L, LUA_REGISTRYINDEX, kRegApp);
    void* app = lua_touserdata(L, -1);
    lua_pop(L, 1);
    lua_pushboolean(L, engine_app_queue_scene_save(app, path));
    return 1;
}

static int l_load_game(lua_State* L)
{
    const char* slot = luaL_checkstring(L, 1);
    char path[260];
    if (!engine_save_slot_path(slot, path, sizeof(path))) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_getfield(L, LUA_REGISTRYINDEX, kRegApp);
    void* app = lua_touserdata(L, -1);
    lua_pop(L, 1);
    lua_pushboolean(L, engine_app_queue_scene_load(app, path));
    return 1;
}

static void* getHud(lua_State* L)
{
    lua_getfield(L, LUA_REGISTRYINDEX, kRegHud);
    void* h = lua_touserdata(L, -1);
    lua_pop(L, 1);
    return h;
}

static int l_hud_clear(lua_State* L)
{
    engine_hud_clear(getHud(L));
    return 0;
}

static int l_hud_text(lua_State* L)
{
    float x = static_cast<float>(luaL_checknumber(L, 1));
    float y = static_cast<float>(luaL_checknumber(L, 2));
    const char* text = luaL_checkstring(L, 3);
    unsigned int color = static_cast<unsigned int>(luaL_optinteger(L, 4, 0xFFFFFFFFu));
    float scale = static_cast<float>(luaL_optnumber(L, 5, 1.0));
    engine_hud_text(getHud(L), x, y, text, color, scale);
    return 0;
}

static int l_hud_rect(lua_State* L)
{
    float x = static_cast<float>(luaL_checknumber(L, 1));
    float y = static_cast<float>(luaL_checknumber(L, 2));
    float w = static_cast<float>(luaL_checknumber(L, 3));
    float h = static_cast<float>(luaL_checknumber(L, 4));
    unsigned int color = static_cast<unsigned int>(luaL_optinteger(L, 5, 0xFF808080u));
    bool filled = lua_toboolean(L, 6);
    if (filled) {
        engine_hud_filled_rect(getHud(L), x, y, w, h, color);
    } else {
        engine_hud_outline_rect(getHud(L), x, y, w, h, color);
    }
    return 0;
}

static void* getParticles(lua_State* L)
{
    lua_getfield(L, LUA_REGISTRYINDEX, kRegParticles);
    void* p = lua_touserdata(L, -1);
    lua_pop(L, 1);
    return p;
}

static int l_spawn_particles(lua_State* L)
{
    float x = static_cast<float>(luaL_checknumber(L, 1));
    float y = static_cast<float>(luaL_checknumber(L, 2));
    float z = static_cast<float>(luaL_checknumber(L, 3));
    int count = static_cast<int>(luaL_checkinteger(L, 4));
    unsigned int color = static_cast<unsigned int>(luaL_optinteger(L, 5, 0xFF22AAFFu));
    float life = static_cast<float>(luaL_optnumber(L, 6, 1.5));
    engine_particles_emit(getParticles(L), x, y, z, count, color, life);
    return 0;
}

static int l_clear_particles(lua_State* L)
{
    engine_particles_clear(getParticles(L));
    return 0;
}

static int l_particle_count(lua_State* L)
{
    lua_pushinteger(L, engine_particles_alive_count(getParticles(L)));
    return 1;
}

static void* getPhysics(lua_State* L)
{
    lua_getfield(L, LUA_REGISTRYINDEX, kRegPhysics);
    void* p = lua_touserdata(L, -1);
    lua_pop(L, 1);
    return p;
}

static int l_add_box_body(lua_State* L)
{
    float px = (float)luaL_checknumber(L, 1);
    float py = (float)luaL_checknumber(L, 2);
    float pz = (float)luaL_checknumber(L, 3);
    float hx = (float)luaL_optnumber(L, 4, 0.5);
    float hy = (float)luaL_optnumber(L, 5, 0.5);
    float hz = (float)luaL_optnumber(L, 6, 0.5);
    bool dynamic = lua_toboolean(L, 7);
    float mass = (float)luaL_optnumber(L, 8, 1.0);
    lua_pushinteger(
        L, engine_physics_create_box(getPhysics(L), px, py, pz, hx, hy, hz, dynamic ? 1 : 0, mass)
    );
    return 1;
}

static int l_add_sphere_body(lua_State* L)
{
    float px = (float)luaL_checknumber(L, 1);
    float py = (float)luaL_checknumber(L, 2);
    float pz = (float)luaL_checknumber(L, 3);
    float r = (float)luaL_optnumber(L, 4, 0.5);
    bool dynamic = lua_toboolean(L, 5);
    float mass = (float)luaL_optnumber(L, 6, 1.0);
    lua_pushinteger(
        L, engine_physics_create_sphere(getPhysics(L), px, py, pz, r, dynamic ? 1 : 0, mass)
    );
    return 1;
}

static int l_add_capsule_body(lua_State* L)
{
    float px = (float)luaL_checknumber(L, 1);
    float py = (float)luaL_checknumber(L, 2);
    float pz = (float)luaL_checknumber(L, 3);
    float halfHeight = (float)luaL_optnumber(L, 4, 0.6);  // ~1.2m cylinder ≈ human torso
    float radius = (float)luaL_optnumber(L, 5, 0.3);
    bool dynamic = lua_toboolean(L, 6);
    float mass = (float)luaL_optnumber(L, 7, 70.0);  // ~70kg adult human default
    lua_pushinteger(
        L,
        engine_physics_create_capsule(
            getPhysics(L), px, py, pz, halfHeight, radius, dynamic ? 1 : 0, mass
        )
    );
    return 1;
}

static int l_remove_body(lua_State* L)
{
    unsigned int id = (unsigned int)luaL_checkinteger(L, 1);
    engine_physics_destroy_body(getPhysics(L), id);
    return 0;
}

static int l_get_body_position(lua_State* L)
{
    unsigned int id = (unsigned int)luaL_checkinteger(L, 1);
    float x = 0, y = 0, z = 0;
    engine_physics_get_body_position(getPhysics(L), id, &x, &y, &z);
    lua_pushnumber(L, x);
    lua_pushnumber(L, y);
    lua_pushnumber(L, z);
    return 3;
}

static int l_get_body_rotation(lua_State* L)
{
    unsigned int id = (unsigned int)luaL_checkinteger(L, 1);
    float x = 0, y = 0, z = 0, w = 1;
    engine_physics_get_body_rotation(getPhysics(L), id, &x, &y, &z, &w);
    lua_pushnumber(L, x);
    lua_pushnumber(L, y);
    lua_pushnumber(L, z);
    lua_pushnumber(L, w);
    return 4;
}

static int l_apply_force(lua_State* L)
{
    unsigned int id = (unsigned int)luaL_checkinteger(L, 1);
    float fx = (float)luaL_checknumber(L, 2);
    float fy = (float)luaL_checknumber(L, 3);
    float fz = (float)luaL_checknumber(L, 4);
    engine_physics_apply_force(getPhysics(L), id, fx, fy, fz);
    return 0;
}

static int l_apply_impulse(lua_State* L)
{
    unsigned int id = (unsigned int)luaL_checkinteger(L, 1);
    float ix = (float)luaL_checknumber(L, 2);
    float iy = (float)luaL_checknumber(L, 3);
    float iz = (float)luaL_checknumber(L, 4);
    engine_physics_apply_impulse(getPhysics(L), id, ix, iy, iz);
    return 0;
}

static int l_set_body_position(lua_State* L)
{
    unsigned int id = (unsigned int)luaL_checkinteger(L, 1);
    float px = (float)luaL_checknumber(L, 2);
    float py = (float)luaL_checknumber(L, 3);
    float pz = (float)luaL_checknumber(L, 4);
    engine_physics_set_body_position(getPhysics(L), id, px, py, pz);
    return 0;
}

static int l_get_linear_velocity(lua_State* L)
{
    unsigned int id = (unsigned int)luaL_checkinteger(L, 1);
    float x = 0, y = 0, z = 0;
    engine_physics_get_linear_velocity(getPhysics(L), id, &x, &y, &z);
    lua_pushnumber(L, x);
    lua_pushnumber(L, y);
    lua_pushnumber(L, z);
    return 3;
}

static int l_set_linear_velocity(lua_State* L)
{
    unsigned int id = (unsigned int)luaL_checkinteger(L, 1);
    float vx = (float)luaL_checknumber(L, 2);
    float vy = (float)luaL_checknumber(L, 3);
    float vz = (float)luaL_checknumber(L, 4);
    engine_physics_set_linear_velocity(getPhysics(L), id, vx, vy, vz);
    return 0;
}

static int l_get_angular_velocity(lua_State* L)
{
    unsigned int id = (unsigned int)luaL_checkinteger(L, 1);
    float x = 0, y = 0, z = 0;
    engine_physics_get_angular_velocity(getPhysics(L), id, &x, &y, &z);
    lua_pushnumber(L, x);
    lua_pushnumber(L, y);
    lua_pushnumber(L, z);
    return 3;
}

static int l_set_angular_velocity(lua_State* L)
{
    unsigned int id = (unsigned int)luaL_checkinteger(L, 1);
    float wx = (float)luaL_checknumber(L, 2);
    float wy = (float)luaL_checknumber(L, 3);
    float wz = (float)luaL_checknumber(L, 4);
    engine_physics_set_angular_velocity(getPhysics(L), id, wx, wy, wz);
    return 0;
}

// engine.add_convex_hull_body(mesh_idx, px, py, pz, scale=1.0, dynamic=true,
//                             mass=1.0, max_points=32, tolerance=0.05) -> body_id
// Looks up the mesh's CPU-cached positions, sub-samples to max_points, scales
// each sample, and hands the result to engine_physics_create_convex_hull.
static int l_add_convex_hull_body(lua_State* L)
{
    int meshIdx = (int)luaL_checkinteger(L, 1);
    float px = (float)luaL_checknumber(L, 2);
    float py = (float)luaL_checknumber(L, 3);
    float pz = (float)luaL_checknumber(L, 4);
    float scale = (float)luaL_optnumber(L, 5, 1.0);
    bool dynamic = lua_isnoneornil(L, 6) ? true : lua_toboolean(L, 6) != 0;
    float mass = (float)luaL_optnumber(L, 7, 1.0);
    int maxPoints = (int)luaL_optinteger(L, 8, 32);
    float tolerance = (float)luaL_optnumber(L, 9, 0.05);

    lua_getfield(L, LUA_REGISTRYINDEX, kRegScenePtr);
    void* scenePtr = lua_touserdata(L, -1);
    lua_pop(L, 1);

    const float* allPositions = nullptr;
    unsigned int totalCount = 0;
    if (!engine_scene_get_mesh_positions(scenePtr, meshIdx, &allPositions, &totalCount)) {
        lua_pushinteger(L, 0);
        return 1;
    }

    // Sub-sample to at most maxPoints by stride, then scale each sample. This
    // keeps Jolt's hull build cheap and gives Lua a knob over hull fidelity.
    if (maxPoints < 4) maxPoints = 4;
    if (maxPoints > 256) maxPoints = 256;
    unsigned int step = totalCount <= (unsigned int)maxPoints
                            ? 1
                            : totalCount / (unsigned int)maxPoints;
    if (step == 0) step = 1;

    // Storage on the stack is fine here — even at the 256 cap that's 3 KB.
    float scaled[256 * 3];
    unsigned int sampled = 0;
    for (unsigned int i = 0; i < totalCount && sampled < (unsigned int)maxPoints; i += step) {
        scaled[sampled * 3 + 0] = allPositions[i * 3 + 0] * scale;
        scaled[sampled * 3 + 1] = allPositions[i * 3 + 1] * scale;
        scaled[sampled * 3 + 2] = allPositions[i * 3 + 2] * scale;
        ++sampled;
    }
    if (sampled < 4) {
        lua_pushinteger(L, 0);
        return 1;
    }

    lua_getfield(L, LUA_REGISTRYINDEX, kRegPhysics);
    void* physicsPtr = lua_touserdata(L, -1);
    lua_pop(L, 1);

    unsigned int body = engine_physics_create_convex_hull(
        physicsPtr, scaled, sampled, sizeof(float) * 3, px, py, pz, dynamic ? 1 : 0, mass,
        tolerance
    );
    lua_pushinteger(L, body);
    return 1;
}

static int l_attach_rigid_body(lua_State* L)
{
    auto* w = getEcsWorld(L);
    if (!w) {
        lua_pushboolean(L, 0);
        return 1;
    }
    uint64_t entityId = (uint64_t)luaL_checkinteger(L, 1);
    unsigned int bodyId = (unsigned int)luaL_checkinteger(L, 2);
    flecs::entity e(*w, entityId);
    if (!e.is_alive()) {
        lua_pushboolean(L, 0);
        return 1;
    }
    e.set(RigidBody{ bodyId });
    lua_pushboolean(L, 1);
    return 1;
}

// engine.add_mesh_collider(entity, dynamic=true, mass=1.0, maxPoints=32,
//                          tolerance=0.05) -> body_id
// Pulls the entity's MeshRef + Transform, finds the matching slot in
// Scene::spawnableMeshRefs, builds a convex-hull body sized to match the
// entity's world transform (position + uniform scale), and attaches it.
// One-stop for "make this rendered entity solid" — wraps add_convex_hull_body
// + attach_rigid_body so script callers don't have to thread the mesh index
// and world pose around manually.
static int l_add_mesh_collider(lua_State* L)
{
    auto* w = getEcsWorld(L);
    auto* refs = getSpawnableMeshRefs(L);
    if (!w || !refs) {
        lua_pushinteger(L, 0);
        return 1;
    }
    uint64_t entityId = (uint64_t)luaL_checkinteger(L, 1);
    bool dynamic = lua_isnoneornil(L, 2) ? true : lua_toboolean(L, 2) != 0;
    float mass = (float)luaL_optnumber(L, 3, 1.0);
    int maxPoints = (int)luaL_optinteger(L, 4, 32);
    float tolerance = (float)luaL_optnumber(L, 5, 0.05);

    flecs::entity e(*w, entityId);
    if (!e.is_alive() || !e.has<Transform>() || !e.has<MeshRef>()) {
        lua_pushinteger(L, 0);
        return 1;
    }
    auto tf = e.get<Transform>();
    auto mr = e.get<MeshRef>();

    // Find the mesh index that matches this MeshRef. The vertex+index offsets
    // uniquely identify a mesh in the mega-buffer.
    int meshIdx = -1;
    for (size_t i = 0; i < refs->size(); ++i) {
        if ((*refs)[i].vertexOffset == mr.vertexOffset
            && (*refs)[i].indexOffset == mr.indexOffset) {
            meshIdx = static_cast<int>(i);
            break;
        }
    }
    if (meshIdx < 0) {
        lua_pushinteger(L, 0);
        return 1;
    }

    // Extract translation + uniform scale from the world matrix (same approach
    // as Application's RigidBody → Transform sync).
    float px = tf.world._41;
    float py = tf.world._42;
    float pz = tf.world._43;
    float scale = std::sqrt(
        tf.world._11 * tf.world._11 + tf.world._12 * tf.world._12 +
        tf.world._13 * tf.world._13
    );
    if (scale == 0.0f) scale = 1.0f;

    // Sub-sample positions and feed them through the existing convex-hull path.
    lua_getfield(L, LUA_REGISTRYINDEX, kRegScenePtr);
    void* scenePtr = lua_touserdata(L, -1);
    lua_pop(L, 1);
    const float* allPositions = nullptr;
    unsigned int totalCount = 0;
    if (!engine_scene_get_mesh_positions(scenePtr, meshIdx, &allPositions, &totalCount) ||
        totalCount < 4) {
        lua_pushinteger(L, 0);
        return 1;
    }
    if (maxPoints < 4) maxPoints = 4;
    if (maxPoints > 256) maxPoints = 256;
    unsigned int step = totalCount <= (unsigned int)maxPoints
                            ? 1
                            : totalCount / (unsigned int)maxPoints;
    if (step == 0) step = 1;
    float scaled[256 * 3];
    unsigned int sampled = 0;
    for (unsigned int i = 0; i < totalCount && sampled < (unsigned int)maxPoints; i += step) {
        scaled[sampled * 3 + 0] = allPositions[i * 3 + 0] * scale;
        scaled[sampled * 3 + 1] = allPositions[i * 3 + 1] * scale;
        scaled[sampled * 3 + 2] = allPositions[i * 3 + 2] * scale;
        ++sampled;
    }
    if (sampled < 4) {
        lua_pushinteger(L, 0);
        return 1;
    }

    lua_getfield(L, LUA_REGISTRYINDEX, kRegPhysics);
    void* physicsPtr = lua_touserdata(L, -1);
    lua_pop(L, 1);

    unsigned int body = engine_physics_create_convex_hull(
        physicsPtr, scaled, sampled, sizeof(float) * 3, px, py, pz, dynamic ? 1 : 0, mass,
        tolerance
    );
    if (body != 0) {
        e.set(RigidBody{ body });
    }
    lua_pushinteger(L, body);
    return 1;
}

static int l_raycast(lua_State* L)
{
    float ox = (float)luaL_checknumber(L, 1);
    float oy = (float)luaL_checknumber(L, 2);
    float oz = (float)luaL_checknumber(L, 3);
    float dx = (float)luaL_checknumber(L, 4);
    float dy = (float)luaL_checknumber(L, 5);
    float dz = (float)luaL_checknumber(L, 6);
    float maxDist = (float)luaL_optnumber(L, 7, 100.0);
    float hx = 0, hy = 0, hz = 0, hd = 0;
    int hit = engine_physics_raycast(
        getPhysics(L), ox, oy, oz, dx, dy, dz, maxDist, &hx, &hy, &hz, &hd
    );
    lua_pushboolean(L, hit);
    lua_pushnumber(L, hx);
    lua_pushnumber(L, hy);
    lua_pushnumber(L, hz);
    lua_pushnumber(L, hd);
    return 5;
}

// ---------------------------------------------------------------------------
// Registration table
// ---------------------------------------------------------------------------
static const luaL_Reg engineFuncs[] = {
    // Entity operations
    { "create_entity", l_create_entity },
    { "destroy_entity", l_destroy_entity },
    { "entity_is_alive", l_entity_is_alive },
    { "get_selected_entity", l_get_selected_entity },
    { "set_selected_entity", l_set_selected_entity },
    // Transform
    { "get_position", l_get_position },
    { "set_position", l_set_position },
    { "get_scale", l_get_scale },
    { "set_scale", l_set_scale },
    { "set_rotation", l_set_rotation },
    // MeshRef / Materials
    { "get_material_index", l_get_material_index },
    { "set_material_index", l_set_material_index },
    { "set_albedo_override", l_set_albedo_override },
    { "get_material_count", l_get_material_count },
    { "set_material_albedo", l_set_material_albedo },
    { "set_material_roughness", l_set_material_roughness },
    { "set_material_metallic", l_set_material_metallic },
    { "set_material_emissive", l_set_material_emissive },
    // Component management
    { "has_component", l_has_component },
    { "add_pickable", l_add_pickable },
    { "remove_pickable", l_remove_pickable },
    { "add_animated", l_add_animated },
    { "remove_animated", l_remove_animated },
    { "add_point_light", l_add_point_light },
    { "get_light_color", l_get_light_color },
    { "set_light_color", l_set_light_color },
    // Queries
    { "get_all_entities", l_get_all_entities },
    { "get_entities_with", l_get_entities_with },
    { "get_entity_count", l_get_entity_count },
    // Spawning
    { "get_mesh_names", l_get_mesh_names },
    { "get_mesh_count", l_get_mesh_count },
    { "spawn_entity", l_spawn_entity },
    // Editor actions
    { "execute_action", l_execute_action },
    // Utilities
    { "log", l_log },
    { "log_warn", l_log_warn },
    { "log_error", l_log_error },
    { "get_dt", l_get_dt },
    { "get_time", l_get_time },
    { "get_frame_count", l_get_frame_count },
    // Audio
    { "play_sound", l_play_sound },
    // Scene save/load
    { "save_scene", l_save_scene },
    { "load_scene", l_load_scene },
    { "save_game", l_save_game },
    { "load_game", l_load_game },
    // HUD
    { "hud_clear", l_hud_clear },
    { "hud_text", l_hud_text },
    { "hud_rect", l_hud_rect },
    // Particles
    { "spawn_particles", l_spawn_particles },
    { "clear_particles", l_clear_particles },
    { "particle_count", l_particle_count },
    // Physics
    { "add_box_body", l_add_box_body },
    { "add_sphere_body", l_add_sphere_body },
    { "add_capsule_body", l_add_capsule_body },
    { "add_convex_hull_body", l_add_convex_hull_body },
    { "remove_body", l_remove_body },
    { "get_body_position", l_get_body_position },
    { "get_body_rotation", l_get_body_rotation },
    { "raycast", l_raycast },
    { "apply_force", l_apply_force },
    { "apply_impulse", l_apply_impulse },
    { "set_body_position", l_set_body_position },
    { "get_linear_velocity", l_get_linear_velocity },
    { "set_linear_velocity", l_set_linear_velocity },
    { "get_angular_velocity", l_get_angular_velocity },
    { "set_angular_velocity", l_set_angular_velocity },
    { "attach_rigid_body", l_attach_rigid_body },
    { "add_mesh_collider", l_add_mesh_collider },
    { nullptr, nullptr }
};

// ---------------------------------------------------------------------------
// Public API for the module layer
// ---------------------------------------------------------------------------
lua_State* luaScripting_createState()
{
    lua_State* L = luaL_newstate();
    if (!L) {
        return nullptr;
    }
    luaL_openlibs(L);

    // Register engine.* table
    luaL_newlib(L, engineFuncs);
    lua_setglobal(L, "engine");

    return L;
}

void luaScripting_destroyState(lua_State* L)
{
    if (L) {
        lua_close(L);
    }
}

void luaScripting_setScenePointers(
    lua_State* L,
    flecs::world* ecsWorld,
    std::vector<Material>* materials,
    std::vector<MeshRef>* meshRefs,
    std::vector<std::string>* meshNames,
    std::vector<uint64_t>* pendingDestroys
)
{
    lua_pushlightuserdata(L, ecsWorld);
    lua_setfield(L, LUA_REGISTRYINDEX, kRegScene);
    lua_pushlightuserdata(L, materials);
    lua_setfield(L, LUA_REGISTRYINDEX, "engine_materials");
    lua_pushlightuserdata(L, meshRefs);
    lua_setfield(L, LUA_REGISTRYINDEX, "engine_mesh_refs");
    lua_pushlightuserdata(L, meshNames);
    lua_setfield(L, LUA_REGISTRYINDEX, "engine_mesh_names");
    lua_pushlightuserdata(L, pendingDestroys);
    lua_setfield(L, LUA_REGISTRYINDEX, kRegPendingDestroys);
}

void luaScripting_setAudioSystem(lua_State* L, void* audioSystem)
{
    lua_pushlightuserdata(L, audioSystem);
    lua_setfield(L, LUA_REGISTRYINDEX, kRegAudio);
}

void luaScripting_setApplication(lua_State* L, void* app)
{
    lua_pushlightuserdata(L, app);
    lua_setfield(L, LUA_REGISTRYINDEX, kRegApp);
}

void luaScripting_setHud(lua_State* L, void* hud)
{
    lua_pushlightuserdata(L, hud);
    lua_setfield(L, LUA_REGISTRYINDEX, kRegHud);
}

void luaScripting_setParticles(lua_State* L, void* particles)
{
    lua_pushlightuserdata(L, particles);
    lua_setfield(L, LUA_REGISTRYINDEX, kRegParticles);
}

void luaScripting_setPhysics(lua_State* L, void* physics)
{
    lua_pushlightuserdata(L, physics);
    lua_setfield(L, LUA_REGISTRYINDEX, kRegPhysics);
}

void luaScripting_setScenePtr(lua_State* L, void* scene)
{
    lua_pushlightuserdata(L, scene);
    lua_setfield(L, LUA_REGISTRYINDEX, kRegScenePtr);
}

void luaScripting_setFrameData(lua_State* L, float dt, float time, int frameCount)
{
    lua_pushnumber(L, dt);
    lua_setfield(L, LUA_REGISTRYINDEX, kRegDt);
    lua_pushnumber(L, time);
    lua_setfield(L, LUA_REGISTRYINDEX, kRegTime);
    lua_pushinteger(L, frameCount);
    lua_setfield(L, LUA_REGISTRYINDEX, kRegFrameCount);
}

void luaScripting_setSelectedEntity(lua_State* L, uint64_t id)
{
    lua_pushinteger(L, static_cast<lua_Integer>(id));
    lua_setfield(L, LUA_REGISTRYINDEX, kRegSelected);
}

bool luaScripting_loadFile(lua_State* L, const char* path, int* outRef)
{
    if (luaL_dofile(L, path) != LUA_OK) {
        spdlog::error("[lua] Failed to load '{}': {}", path, lua_tostring(L, -1));
        lua_pop(L, 1);
        return false;
    }
    // Script should return a table
    if (!lua_istable(L, -1)) {
        spdlog::warn("[lua] Script '{}' did not return a table", path);
        lua_pop(L, 1);
        return false;
    }
    *outRef = luaL_ref(L, LUA_REGISTRYINDEX);
    return true;
}

void luaScripting_unref(lua_State* L, int ref)
{
    if (ref != LUA_NOREF && ref != LUA_REFNIL) {
        luaL_unref(L, LUA_REGISTRYINDEX, ref);
    }
}

void luaScripting_callHook(lua_State* L, int ref, const char* hookName, uint64_t entityId)
{
    if (ref == LUA_NOREF || ref == LUA_REFNIL) {
        return;
    }
    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return;
    }
    lua_getfield(L, -1, hookName);
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 2);
        return;
    }
    lua_pushinteger(L, static_cast<lua_Integer>(entityId));
    if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
        spdlog::error("[lua] Error in {}: {}", hookName, lua_tostring(L, -1));
        lua_pop(L, 1);
    }
    lua_pop(L, 1);  // pop script table
}

void luaScripting_callUpdate(lua_State* L, int ref, uint64_t entityId, float dt)
{
    if (ref == LUA_NOREF || ref == LUA_REFNIL) {
        return;
    }
    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return;
    }
    lua_getfield(L, -1, "on_update");
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 2);
        return;
    }
    lua_pushinteger(L, static_cast<lua_Integer>(entityId));
    lua_pushnumber(L, dt);
    if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
        spdlog::error("[lua] Error in on_update: {}", lua_tostring(L, -1));
        lua_pop(L, 1);
    }
    lua_pop(L, 1);  // pop script table
}

bool luaScripting_executeFile(lua_State* L, const char* path)
{
    if (luaL_dofile(L, path) != LUA_OK) {
        spdlog::error("[lua] Error executing '{}': {}", path, lua_tostring(L, -1));
        lua_pop(L, 1);
        return false;
    }
    // Pop any return values
    lua_settop(L, 0);
    return true;
}

const char* luaScripting_getPendingAction(lua_State* L)
{
    lua_getfield(L, LUA_REGISTRYINDEX, "engine_pending_action");
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        return nullptr;
    }
    const char* action = lua_tostring(L, -1);
    lua_pop(L, 1);
    // Clear it
    lua_pushnil(L);
    lua_setfield(L, LUA_REGISTRYINDEX, "engine_pending_action");
    return action;
}
