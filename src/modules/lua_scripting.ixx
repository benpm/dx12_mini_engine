module;

#include <flecs.h>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include "lua_script_types.h"

export module lua_scripting;

export import scene;

export using ::Scripted;
export using ::ScriptActionBinding;

// Opaque Lua state handle (avoid including lua.h in module interface)
struct lua_State;

export class LuaScripting
{
   public:
    LuaScripting() = default;
    ~LuaScripting();

    bool init(Scene& scene, const std::string& scriptsDir);
    void shutdown();

    // Called each frame from Application::update()
    void updateScriptedEntities(float dt, float time, int frameCount);

    // Execute a one-shot script file (for editor actions)
    bool executeScript(const std::string& scriptPath);

    // Attach a script to an entity (creates Scripted component, loads script, calls on_create)
    bool attachScript(flecs::entity e, const std::string& scriptPath);

    // Detach script from entity (calls on_destroy, removes component)
    void detachScript(flecs::entity e);

    // Reload all scripts whose files have changed
    void pollHotReload();

    // Load editor action bindings from JSON
    bool loadActionBindings(const std::string& jsonPath);

    // Execute an editor action by name (looks up binding, runs script)
    bool executeAction(const std::string& actionName);

    // Get loaded action bindings (for UI display)
    const std::vector<ScriptActionBinding>& getActionBindings() const;

    bool isInitialized() const;

    // Selected entity ID for scripts to query (set by Application each frame)
    uint64_t selectedEntityId = 0;

    // Pending entity destroys from scripts (processed by Application)
    std::vector<uint64_t> pendingDestroys;

   private:
    lua_State* L = nullptr;
    Scene* scenePtr = nullptr;
    std::string scriptsBasePath;
    std::vector<ScriptActionBinding> actionBindings_;
    bool initialized = false;

    // Hot reload: track file modification times
    struct ScriptFileInfo
    {
        std::string fullPath;
        uint64_t lastModTime = 0;
    };
    std::unordered_map<std::string, ScriptFileInfo> loadedScripts;
    float hotReloadTimer = 0.0f;

    std::string resolveScriptPath(const std::string& scriptPath) const;
    bool loadScriptFile(const std::string& fullPath, int& outRef);
    void callLifecycleHook(int luaRef, const char* hookName, uint64_t entityId);
    void callUpdateHook(int luaRef, uint64_t entityId, float dt);
};
