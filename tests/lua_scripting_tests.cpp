#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#include <flecs.h>
#include <cmath>
#include <string>
#include <vector>
#include "ecs_types.h"
#include "material_types.h"
#include "math_types.h"

// Extern declarations matching lua_scripting_impl.cpp
extern lua_State* luaScripting_createState();
extern void luaScripting_destroyState(lua_State* L);
extern void luaScripting_setScenePointers(
    lua_State* L,
    flecs::world* ecsWorld,
    std::vector<Material>* materials,
    std::vector<MeshRef>* meshRefs,
    std::vector<std::string>* meshNames,
    std::vector<uint64_t>* pendingDestroys
);
extern void luaScripting_setFrameData(lua_State* L, float dt, float time, int frameCount);
extern void luaScripting_setSelectedEntity(lua_State* L, uint64_t id);
extern bool luaScripting_loadFile(lua_State* L, const char* path, int* outRef);
extern void luaScripting_unref(lua_State* L, int ref);
extern void luaScripting_callHook(lua_State* L, int ref, const char* hookName, uint64_t entityId);
extern void luaScripting_callUpdate(lua_State* L, int ref, uint64_t entityId, float dt);
extern bool luaScripting_executeFile(lua_State* L, const char* path);

// Helper: set up a test Lua state + flecs world with engine pointers registered
struct LuaTestFixture
{
    flecs::world world;
    std::vector<Material> materials;
    std::vector<MeshRef> meshRefs;
    std::vector<std::string> meshNames;
    std::vector<uint64_t> pendingDestroys;
    lua_State* L = nullptr;

    LuaTestFixture()
    {
        materials.push_back(Material{});
        MeshRef mr{};
        mr.indexCount = 36;
        meshRefs.push_back(mr);
        meshNames.push_back("test_mesh");

        L = luaScripting_createState();
        REQUIRE(L != nullptr);
        luaScripting_setScenePointers(
            L, &world, &materials, &meshRefs, &meshNames, &pendingDestroys
        );
        luaScripting_setFrameData(L, 0.016f, 1.0f, 60);
    }

    ~LuaTestFixture() { luaScripting_destroyState(L); }

    // Run a Lua snippet and return true on success
    bool exec(const char* code) { return luaL_dostring(L, code) == LUA_OK; }

    // Run Lua code, expect integer result on stack
    lua_Integer execInt(const char* code)
    {
        REQUIRE(luaL_dostring(L, code) == LUA_OK);
        lua_Integer val = lua_tointeger(L, -1);
        lua_pop(L, 1);
        return val;
    }

    // Run Lua code, expect number result on stack
    double execNum(const char* code)
    {
        REQUIRE(luaL_dostring(L, code) == LUA_OK);
        double val = lua_tonumber(L, -1);
        lua_pop(L, 1);
        return val;
    }

    // Run Lua code, expect boolean result on stack
    bool execBool(const char* code)
    {
        REQUIRE(luaL_dostring(L, code) == LUA_OK);
        bool val = lua_toboolean(L, -1);
        lua_pop(L, 1);
        return val;
    }
};

// ---------------------------------------------------------------------------
// engine.create_entity / entity_is_alive / destroy_entity
// ---------------------------------------------------------------------------
TEST_CASE_FIXTURE(LuaTestFixture, "create and check entity")
{
    auto id = execInt("return engine.create_entity()");
    CHECK(id > 0);

    auto e = world.entity(static_cast<uint64_t>(id));
    CHECK(e.is_alive());
}

TEST_CASE_FIXTURE(LuaTestFixture, "entity_is_alive returns false for dead entity")
{
    auto e = world.entity();
    uint64_t id = e.id();
    e.destruct();

    std::string code = "return engine.entity_is_alive(" + std::to_string(id) + ")";
    CHECK_FALSE(execBool(code.c_str()));
}

TEST_CASE_FIXTURE(LuaTestFixture, "destroy_entity adds to pending destroys")
{
    auto e = world.entity();
    uint64_t id = e.id();

    std::string code = "engine.destroy_entity(" + std::to_string(id) + ")";
    CHECK(exec(code.c_str()));
    CHECK(pendingDestroys.size() == 1);
    CHECK(pendingDestroys[0] == id);
}

// ---------------------------------------------------------------------------
// engine.get/set_position
// ---------------------------------------------------------------------------
TEST_CASE_FIXTURE(LuaTestFixture, "set_position and get_position round-trip")
{
    auto e = world.entity();
    Transform tf;
    tf.world = translate(0, 0, 0);
    e.set(tf);
    uint64_t id = e.id();

    std::string setCode = "engine.set_position(" + std::to_string(id) + ", 3.5, -1.0, 7.25)";
    CHECK(exec(setCode.c_str()));

    std::string getCode = "local x,y,z = engine.get_position(" + std::to_string(id) + "); return x";
    CHECK(execNum(getCode.c_str()) == doctest::Approx(3.5));

    getCode = "local x,y,z = engine.get_position(" + std::to_string(id) + "); return y";
    CHECK(execNum(getCode.c_str()) == doctest::Approx(-1.0));

    getCode = "local x,y,z = engine.get_position(" + std::to_string(id) + "); return z";
    CHECK(execNum(getCode.c_str()) == doctest::Approx(7.25));
}

TEST_CASE_FIXTURE(LuaTestFixture, "get_position returns 0,0,0 for entity without Transform")
{
    auto e = world.entity();
    uint64_t id = e.id();

    std::string code =
        "local x,y,z = engine.get_position(" + std::to_string(id) + "); return x+y+z";
    CHECK(execNum(code.c_str()) == doctest::Approx(0.0));
}

// ---------------------------------------------------------------------------
// engine.set_scale / get_scale
// ---------------------------------------------------------------------------
TEST_CASE_FIXTURE(LuaTestFixture, "set_scale applies uniform scale")
{
    auto e = world.entity();
    Transform tf;
    tf.world = translate(5, 10, 15);
    e.set(tf);
    uint64_t id = e.id();

    std::string code = "engine.set_scale(" + std::to_string(id) + ", 2.0)";
    CHECK(exec(code.c_str()));

    code = "local sx,sy,sz = engine.get_scale(" + std::to_string(id) + "); return sx";
    CHECK(execNum(code.c_str()) == doctest::Approx(2.0).epsilon(0.01));
}

// ---------------------------------------------------------------------------
// engine.has_component
// ---------------------------------------------------------------------------
TEST_CASE_FIXTURE(LuaTestFixture, "has_component detects Transform")
{
    auto e = world.entity();
    e.set(Transform{ translate(0, 0, 0) });
    uint64_t id = e.id();

    std::string code = "return engine.has_component(" + std::to_string(id) + ", 'Transform')";
    CHECK(execBool(code.c_str()));

    code = "return engine.has_component(" + std::to_string(id) + ", 'MeshRef')";
    CHECK_FALSE(execBool(code.c_str()));
}

TEST_CASE_FIXTURE(LuaTestFixture, "has_component detects Pickable and Animated")
{
    auto e = world.entity();
    e.add<Pickable>();
    uint64_t id = e.id();

    std::string code = "return engine.has_component(" + std::to_string(id) + ", 'Pickable')";
    CHECK(execBool(code.c_str()));

    code = "return engine.has_component(" + std::to_string(id) + ", 'Animated')";
    CHECK_FALSE(execBool(code.c_str()));
}

// ---------------------------------------------------------------------------
// engine.add_pickable / remove_pickable
// ---------------------------------------------------------------------------
TEST_CASE_FIXTURE(LuaTestFixture, "add and remove pickable via Lua")
{
    auto e = world.entity();
    uint64_t id = e.id();
    CHECK_FALSE(e.has<Pickable>());

    std::string code = "engine.add_pickable(" + std::to_string(id) + ")";
    CHECK(exec(code.c_str()));
    CHECK(e.has<Pickable>());

    code = "engine.remove_pickable(" + std::to_string(id) + ")";
    CHECK(exec(code.c_str()));
    CHECK_FALSE(e.has<Pickable>());
}

// ---------------------------------------------------------------------------
// engine.add_animated / remove_animated
// ---------------------------------------------------------------------------
TEST_CASE_FIXTURE(LuaTestFixture, "add and remove animated via Lua")
{
    auto e = world.entity();
    uint64_t id = e.id();

    std::string code = "engine.add_animated(" + std::to_string(id) + ", 2.0, 5.0, 1.5)";
    CHECK(exec(code.c_str()));
    CHECK(e.has<Animated>());
    CHECK(e.get<Animated>().speed == doctest::Approx(2.0f));
    CHECK(e.get<Animated>().orbitRadius == doctest::Approx(5.0f));
    CHECK(e.get<Animated>().orbitY == doctest::Approx(1.5f));

    code = "engine.remove_animated(" + std::to_string(id) + ")";
    CHECK(exec(code.c_str()));
    CHECK_FALSE(e.has<Animated>());
}

// ---------------------------------------------------------------------------
// engine.set_albedo_override
// ---------------------------------------------------------------------------
TEST_CASE_FIXTURE(LuaTestFixture, "set_albedo_override modifies MeshRef")
{
    auto e = world.entity();
    MeshRef mr{};
    mr.indexCount = 36;
    e.set(mr);
    uint64_t id = e.id();

    std::string code =
        "engine.set_albedo_override(" + std::to_string(id) + ", 1.0, 0.5, 0.25, 1.0)";
    CHECK(exec(code.c_str()));

    const auto& ref = e.get<MeshRef>();
    CHECK(ref.albedoOverride.x == doctest::Approx(1.0f));
    CHECK(ref.albedoOverride.y == doctest::Approx(0.5f));
    CHECK(ref.albedoOverride.z == doctest::Approx(0.25f));
    CHECK(ref.albedoOverride.w == doctest::Approx(1.0f));
}

// ---------------------------------------------------------------------------
// engine.get_material_count / set_material_albedo
// ---------------------------------------------------------------------------
TEST_CASE_FIXTURE(LuaTestFixture, "material count and set albedo")
{
    CHECK(execInt("return engine.get_material_count()") == 1);

    CHECK(exec("engine.set_material_albedo(0, 1.0, 0.0, 0.0, 1.0)"));
    CHECK(materials[0].albedo.x == doctest::Approx(1.0f));
    CHECK(materials[0].albedo.y == doctest::Approx(0.0f));
}

// ---------------------------------------------------------------------------
// engine.set_material_roughness / metallic / emissive
// ---------------------------------------------------------------------------
TEST_CASE_FIXTURE(LuaTestFixture, "material property setters")
{
    CHECK(exec("engine.set_material_roughness(0, 0.75)"));
    CHECK(materials[0].roughness == doctest::Approx(0.75f));

    CHECK(exec("engine.set_material_metallic(0, 1.0)"));
    CHECK(materials[0].metallic == doctest::Approx(1.0f));

    CHECK(exec("engine.set_material_emissive(0, 0.5, 1.0, 0.0, 3.0)"));
    CHECK(materials[0].emissive.x == doctest::Approx(0.5f));
    CHECK(materials[0].emissive.y == doctest::Approx(1.0f));
    CHECK(materials[0].emissiveStrength == doctest::Approx(3.0f));
}

// ---------------------------------------------------------------------------
// engine.get_entity_count / get_all_entities
// ---------------------------------------------------------------------------
TEST_CASE_FIXTURE(LuaTestFixture, "entity count and list")
{
    CHECK(execInt("return engine.get_entity_count()") == 0);

    auto e1 = world.entity();
    e1.set(Transform{ translate(0, 0, 0) });
    MeshRef mr{};
    mr.indexCount = 36;
    e1.set(mr);

    auto e2 = world.entity();
    e2.set(Transform{ translate(1, 0, 0) });
    e2.set(mr);

    CHECK(execInt("return engine.get_entity_count()") == 2);
    CHECK(execInt("return #engine.get_all_entities()") == 2);
}

// ---------------------------------------------------------------------------
// engine.get_entities_with
// ---------------------------------------------------------------------------
TEST_CASE_FIXTURE(LuaTestFixture, "get_entities_with filters by component")
{
    auto e1 = world.entity();
    e1.add<Pickable>();
    world.entity();  // no Pickable

    CHECK(execInt("return #engine.get_entities_with('Pickable')") == 1);
    CHECK(execInt("return #engine.get_entities_with('Animated')") == 0);
}

// ---------------------------------------------------------------------------
// engine.spawn_entity
// ---------------------------------------------------------------------------
TEST_CASE_FIXTURE(LuaTestFixture, "spawn_entity creates entity with transform and mesh")
{
    auto id = execInt("return engine.spawn_entity(0, 0, 2.0, 3.0, 4.0, 1.5)");
    CHECK(id > 0);

    auto e = world.entity(static_cast<uint64_t>(id));
    CHECK(e.is_alive());
    CHECK(e.has<Transform>());
    CHECK(e.has<MeshRef>());
    CHECK(e.has<Pickable>());
}

// ---------------------------------------------------------------------------
// engine.get_mesh_count / get_mesh_names
// ---------------------------------------------------------------------------
TEST_CASE_FIXTURE(LuaTestFixture, "mesh info queries")
{
    CHECK(execInt("return engine.get_mesh_count()") == 1);
    CHECK(exec("local n = engine.get_mesh_names(); assert(n[1] == 'test_mesh')"));
}

// ---------------------------------------------------------------------------
// engine.get_dt / get_time / get_frame_count
// ---------------------------------------------------------------------------
TEST_CASE_FIXTURE(LuaTestFixture, "frame data accessors")
{
    CHECK(execNum("return engine.get_dt()") == doctest::Approx(0.016));
    CHECK(execNum("return engine.get_time()") == doctest::Approx(1.0));
    CHECK(execInt("return engine.get_frame_count()") == 60);

    luaScripting_setFrameData(L, 0.033f, 5.5f, 200);
    CHECK(execNum("return engine.get_dt()") == doctest::Approx(0.033));
    CHECK(execNum("return engine.get_time()") == doctest::Approx(5.5));
    CHECK(execInt("return engine.get_frame_count()") == 200);
}

// ---------------------------------------------------------------------------
// engine.get_selected_entity / set_selected_entity
// ---------------------------------------------------------------------------
TEST_CASE_FIXTURE(LuaTestFixture, "selected entity get/set")
{
    // Initially 0 → nil
    CHECK(exec("assert(engine.get_selected_entity() == nil)"));

    auto e = world.entity();
    luaScripting_setSelectedEntity(L, e.id());
    std::string code = "return engine.get_selected_entity() == " + std::to_string(e.id());
    CHECK(execBool(code.c_str()));
}

// ---------------------------------------------------------------------------
// engine.add_point_light / get_light_color / set_light_color
// ---------------------------------------------------------------------------
TEST_CASE_FIXTURE(LuaTestFixture, "point light component via Lua")
{
    auto e = world.entity();
    uint64_t id = e.id();

    std::string code = "engine.add_point_light(" + std::to_string(id) + ", 1,2,3, 1.0,0.5,0.0,1.0)";
    CHECK(exec(code.c_str()));
    CHECK(e.has<PointLight>());

    const auto& pl = e.get<PointLight>();
    CHECK(pl.center.x == doctest::Approx(1.0f));
    CHECK(pl.center.y == doctest::Approx(2.0f));
    CHECK(pl.center.z == doctest::Approx(3.0f));
    CHECK(pl.color.x == doctest::Approx(1.0f));
    CHECK(pl.color.y == doctest::Approx(0.5f));

    code = "engine.set_light_color(" + std::to_string(id) + ", 0.0, 1.0, 0.0, 1.0)";
    CHECK(exec(code.c_str()));
    CHECK(e.get<PointLight>().color.y == doctest::Approx(1.0f));

    code = "local r,g,b,a = engine.get_light_color(" + std::to_string(id) + "); return g";
    CHECK(execNum(code.c_str()) == doctest::Approx(1.0));
}

// ---------------------------------------------------------------------------
// engine.set_material_index / get_material_index
// ---------------------------------------------------------------------------
TEST_CASE_FIXTURE(LuaTestFixture, "material index get/set on MeshRef entity")
{
    auto e = world.entity();
    MeshRef mr{};
    mr.materialIndex = 0;
    e.set(mr);
    uint64_t id = e.id();

    std::string code = "return engine.get_material_index(" + std::to_string(id) + ")";
    CHECK(execInt(code.c_str()) == 0);

    materials.push_back(Material{});  // index 1
    code = "engine.set_material_index(" + std::to_string(id) + ", 1)";
    CHECK(exec(code.c_str()));
    CHECK(e.get<MeshRef>().materialIndex == 1);
}

// ---------------------------------------------------------------------------
// engine.log (just verify it doesn't crash)
// ---------------------------------------------------------------------------
TEST_CASE_FIXTURE(LuaTestFixture, "log functions do not crash")
{
    CHECK(exec("engine.log('test info')"));
    CHECK(exec("engine.log_warn('test warn')"));
    CHECK(exec("engine.log_error('test error')"));
}

// ---------------------------------------------------------------------------
// Script lifecycle: loadFile, callHook, callUpdate, unref
// ---------------------------------------------------------------------------
TEST_CASE_FIXTURE(LuaTestFixture, "script lifecycle hooks called correctly")
{
    // Create a script inline via dostring that returns a table with hooks
    const char* scriptCode = R"(
        _test_hooks = {}
        local s = {}
        function s.on_create(id)
            _test_hooks.created = id
        end
        function s.on_update(id, dt)
            _test_hooks.updated = id
            _test_hooks.dt = dt
        end
        function s.on_destroy(id)
            _test_hooks.destroyed = id
        end
        return s
    )";

    REQUIRE(luaL_dostring(L, scriptCode) == LUA_OK);
    REQUIRE(lua_istable(L, -1));
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);

    auto e = world.entity();
    uint64_t id = e.id();

    luaScripting_callHook(L, ref, "on_create", id);
    std::string check = "return _test_hooks.created == " + std::to_string(id);
    CHECK(execBool(check.c_str()));

    luaScripting_callUpdate(L, ref, id, 0.016f);
    check = "return _test_hooks.updated == " + std::to_string(id);
    CHECK(execBool(check.c_str()));
    CHECK(execNum("return _test_hooks.dt") == doctest::Approx(0.016));

    luaScripting_callHook(L, ref, "on_destroy", id);
    check = "return _test_hooks.destroyed == " + std::to_string(id);
    CHECK(execBool(check.c_str()));

    luaScripting_unref(L, ref);
}

// ---------------------------------------------------------------------------
// set_rotation preserves position
// ---------------------------------------------------------------------------
TEST_CASE_FIXTURE(LuaTestFixture, "set_rotation preserves position")
{
    auto e = world.entity();
    Transform tf;
    tf.world = translate(5, 10, 15);
    e.set(tf);
    uint64_t id = e.id();

    std::string code = "engine.set_rotation(" + std::to_string(id) + ", 0,1,0, 1.57)";
    CHECK(exec(code.c_str()));

    code = "local x,y,z = engine.get_position(" + std::to_string(id) + "); return y";
    CHECK(execNum(code.c_str()) == doctest::Approx(10.0).epsilon(0.1));
}
