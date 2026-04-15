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
