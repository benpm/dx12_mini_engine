#pragma once

#include <string>
#include <vector>

// Component: attaches a Lua script to an entity
struct Scripted
{
    std::string scriptPath;  // relative to scripts directory
    int luaRef = -1;         // Lua registry reference to script's environment table
};

// Maps an editor action name to a Lua script path
struct ScriptActionBinding
{
    std::string actionName;
    std::string scriptPath;
};
