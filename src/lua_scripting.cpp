module;

#if defined(__clang__)
    #define FMT_CONSTEVAL
#endif

#include <spdlog/spdlog.h>

#include <flecs.h>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include "lua_script_types.h"
#include "material_types.h"
#include "math_types.h"

module lua_scripting;

namespace fs = std::filesystem;

// Extern declarations for functions in lua_scripting_impl.cpp
struct lua_State;
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
extern const char* luaScripting_getPendingAction(lua_State* L);

// ---------------------------------------------------------------------------
// LuaScripting implementation
// ---------------------------------------------------------------------------

LuaScripting::~LuaScripting()
{
    shutdown();
}

bool LuaScripting::init(Scene& scene, const std::string& scriptsDir)
{
    if (initialized) {
        return true;
    }

    L = luaScripting_createState();
    if (!L) {
        spdlog::error("Failed to create Lua state");
        return false;
    }

    scenePtr = &scene;
    scriptsBasePath = scriptsDir;

    // Normalize path separator
    for (char& c : scriptsBasePath) {
        if (c == '\\') {
            c = '/';
        }
    }
    if (!scriptsBasePath.empty() && scriptsBasePath.back() != '/') {
        scriptsBasePath += '/';
    }

    luaScripting_setScenePointers(
        L, &scene.ecsWorld, &scene.materials, &scene.spawnableMeshRefs, &scene.spawnableMeshNames,
        &pendingDestroys
    );

    initialized = true;
    spdlog::info("Lua scripting initialized (scripts dir: {})", scriptsBasePath);
    return true;
}

void LuaScripting::shutdown()
{
    if (!initialized) {
        return;
    }

    // Call on_destroy for all scripted entities
    if (scenePtr) {
        scenePtr->scriptQuery.each([&](flecs::entity e, Scripted& s) {
            if (s.luaRef >= 0) {
                callLifecycleHook(s.luaRef, "on_destroy", e.id());
                luaScripting_unref(L, s.luaRef);
                s.luaRef = -1;
            }
        });
    }

    luaScripting_destroyState(L);
    L = nullptr;
    scenePtr = nullptr;
    initialized = false;
    loadedScripts.clear();
    actionBindings_.clear();
}

bool LuaScripting::isInitialized() const
{
    return initialized;
}

std::string LuaScripting::resolveScriptPath(const std::string& scriptPath) const
{
    // If already absolute or starts with scripts base, use as-is
    if (fs::path(scriptPath).is_absolute()) {
        return scriptPath;
    }
    return scriptsBasePath + scriptPath;
}

bool LuaScripting::loadScriptFile(const std::string& fullPath, int& outRef)
{
    if (!luaScripting_loadFile(L, fullPath.c_str(), &outRef)) {
        return false;
    }

    // Track for hot reload
    auto& info = loadedScripts[fullPath];
    info.fullPath = fullPath;
    try {
        auto ftime = fs::last_write_time(fullPath);
        info.lastModTime = static_cast<uint64_t>(ftime.time_since_epoch().count());
    } catch (...) {
        info.lastModTime = 0;
    }

    return true;
}

void LuaScripting::callLifecycleHook(int luaRef, const char* hookName, uint64_t entityId)
{
    luaScripting_callHook(L, luaRef, hookName, entityId);
}

void LuaScripting::callUpdateHook(int luaRef, uint64_t entityId, float dt)
{
    luaScripting_callUpdate(L, luaRef, entityId, dt);
}

void LuaScripting::updateScriptedEntities(float dt, float time, int frameCount)
{
    if (!initialized || !scenePtr) {
        return;
    }

    luaScripting_setFrameData(L, dt, time, frameCount);
    luaScripting_setSelectedEntity(L, selectedEntityId);

    // Iterate scripted entities
    scenePtr->scriptQuery.each([&](flecs::entity e, Scripted& s) {
        // Load script if not yet loaded
        if (s.luaRef < 0 && !s.scriptPath.empty()) {
            std::string fullPath = resolveScriptPath(s.scriptPath);
            if (loadScriptFile(fullPath, s.luaRef)) {
                callLifecycleHook(s.luaRef, "on_create", e.id());
            }
        }
        // Call on_update
        if (s.luaRef >= 0) {
            callUpdateHook(s.luaRef, e.id(), dt);
        }
    });

    // Process pending destroys from scripts
    for (uint64_t id : pendingDestroys) {
        auto e = scenePtr->ecsWorld.entity(id);
        if (e.is_alive()) {
            if (e.has<Scripted>()) {
                auto& s = e.get_mut<Scripted>();
                if (s.luaRef >= 0) {
                    callLifecycleHook(s.luaRef, "on_destroy", id);
                    luaScripting_unref(L, s.luaRef);
                }
            }
            e.destruct();
        }
    }
    pendingDestroys.clear();

    // Check for pending action from Lua
    // (handled by Application after this call)
}

bool LuaScripting::executeScript(const std::string& scriptPath)
{
    if (!initialized) {
        return false;
    }
    std::string fullPath = resolveScriptPath(scriptPath);
    return luaScripting_executeFile(L, fullPath.c_str());
}

bool LuaScripting::attachScript(flecs::entity e, const std::string& scriptPath)
{
    if (!initialized || !e.is_alive()) {
        return false;
    }

    // If already scripted, detach first
    if (e.has<Scripted>()) {
        detachScript(e);
    }

    Scripted s;
    s.scriptPath = scriptPath;
    s.luaRef = -1;

    std::string fullPath = resolveScriptPath(scriptPath);
    if (loadScriptFile(fullPath, s.luaRef)) {
        e.set(s);
        callLifecycleHook(s.luaRef, "on_create", e.id());
        return true;
    }

    // Still attach even if load failed (hot reload can fix it)
    e.set(s);
    return false;
}

void LuaScripting::detachScript(flecs::entity e)
{
    if (!initialized || !e.is_alive() || !e.has<Scripted>()) {
        return;
    }

    auto& s = e.get_mut<Scripted>();
    if (s.luaRef >= 0) {
        callLifecycleHook(s.luaRef, "on_destroy", e.id());
        luaScripting_unref(L, s.luaRef);
    }
    e.remove<Scripted>();
}

void LuaScripting::pollHotReload()
{
    if (!initialized) {
        return;
    }

    for (auto& [path, info] : loadedScripts) {
        try {
            auto ftime = fs::last_write_time(info.fullPath);
            uint64_t newTime = static_cast<uint64_t>(ftime.time_since_epoch().count());
            if (newTime != info.lastModTime) {
                info.lastModTime = newTime;
                spdlog::info("[lua] Hot reloading: {}", info.fullPath);

                // Find all entities using this script and reload
                scenePtr->scriptQuery.each([&](flecs::entity e, Scripted& s) {
                    std::string fullPath = resolveScriptPath(s.scriptPath);
                    if (fullPath == info.fullPath && s.luaRef >= 0) {
                        callLifecycleHook(s.luaRef, "on_destroy", e.id());
                        luaScripting_unref(L, s.luaRef);
                        s.luaRef = -1;
                        int newRef = -1;
                        if (luaScripting_loadFile(L, fullPath.c_str(), &newRef)) {
                            s.luaRef = newRef;
                            callLifecycleHook(s.luaRef, "on_create", e.id());
                        }
                    }
                });
            }
        } catch (...) {
            // File might be mid-write, skip
        }
    }
}

bool LuaScripting::loadActionBindings(const std::string& jsonPath)
{
    // Simple JSON parsing for actions.json — avoid pulling in glaze here.
    // Format: { "actions": [ { "name": "...", "script": "..." }, ... ] }
    std::ifstream file(jsonPath);
    if (!file.is_open()) {
        spdlog::warn("[lua] Could not open action bindings: {}", jsonPath);
        return false;
    }

    actionBindings_.clear();
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    // Minimal JSON parsing: find "name" and "script" pairs
    size_t pos = 0;
    while (true) {
        pos = content.find("\"name\"", pos);
        if (pos == std::string::npos) {
            break;
        }

        // Find the value after "name":
        size_t colonPos = content.find(':', pos + 6);
        if (colonPos == std::string::npos) {
            break;
        }
        size_t nameStart = content.find('"', colonPos + 1);
        if (nameStart == std::string::npos) {
            break;
        }
        size_t nameEnd = content.find('"', nameStart + 1);
        if (nameEnd == std::string::npos) {
            break;
        }
        std::string name = content.substr(nameStart + 1, nameEnd - nameStart - 1);

        // Find "script" after this
        size_t scriptPos = content.find("\"script\"", nameEnd);
        if (scriptPos == std::string::npos) {
            break;
        }
        size_t sColonPos = content.find(':', scriptPos + 8);
        if (sColonPos == std::string::npos) {
            break;
        }
        size_t scriptStart = content.find('"', sColonPos + 1);
        if (scriptStart == std::string::npos) {
            break;
        }
        size_t scriptEnd = content.find('"', scriptStart + 1);
        if (scriptEnd == std::string::npos) {
            break;
        }
        std::string script = content.substr(scriptStart + 1, scriptEnd - scriptStart - 1);

        actionBindings_.push_back({ name, script });
        pos = scriptEnd + 1;
    }

    spdlog::info("[lua] Loaded {} action bindings from {}", actionBindings_.size(), jsonPath);
    return true;
}

bool LuaScripting::executeAction(const std::string& actionName)
{
    if (!initialized) {
        return false;
    }
    for (const auto& binding : actionBindings_) {
        if (binding.actionName == actionName) {
            return executeScript(binding.scriptPath);
        }
    }
    spdlog::warn("[lua] No script bound to action '{}'", actionName);
    return false;
}

const std::vector<ScriptActionBinding>& LuaScripting::getActionBindings() const
{
    return actionBindings_;
}
