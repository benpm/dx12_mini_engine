#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

struct ConfigData
{
    // Window
    uint32_t windowWidth = 1280;
    uint32_t windowHeight = 720;
    bool startFullscreen = false;

    // Graphics defaults
    bool vsync = true;
    int tonemapMode = 2;
    float bloomThreshold = 1.7f;
    float bloomIntensity = 0.1f;
    bool ssaoEnabled = true;
    float ssaoRadius = 0.5f;
    float ssaoBias = 0.025f;
    int ssaoKernelSize = 32;
    bool shadowsEnabled = true;
    bool cubemapEnabled = true;
    uint32_t cubemapResolution = 128;

    // Display
    bool showGrid = true;
    float gridMajorSize = 10.0f;
    int gridSubdivisions = 10;
    bool showMetrics = true;
    bool showLightBillboards = true;
    bool animateEntities = true;
    float lightAnimationSpeed = 1.0f;

    // Spawning
    bool autoStopSpawning = true;
    float spawnStopFrameMs = 10.0f;
    int spawnBatchSize = 50;

    // Paths
    std::string defaultScenePath = "resources/scenes/default.json";
    std::string scriptsDir = "resources/scripts";
    std::string actionBindingsFile = "resources/scripts/actions.json";

    // Hotkeys: action name → list of key names (e.g. "toggleFullscreen": ["F11"])
    std::map<std::string, std::vector<std::string>> hotkeys = {
        { "toggleFullscreen", { "F11" } },
        { "deleteEntity", { "Delete" } },
        { "deselect", { "Escape" } },
    };

    // Icons: UI element name → Material Icons icon name (see resources/icons/)
    std::map<std::string, std::string> icons = {
        // Main menu bar menus
        { "menu.Display", "desktop_windows" },
        { "menu.Camera", "videocam" },
        { "menu.Bloom", "flare" },
        { "menu.Tonemap", "tune" },
        { "menu.View", "visibility" },
        { "menu.Scene", "wb_sunny" },
        { "menu.Shadows", "contrast" },
        { "menu.Animation", "animation" },
        { "menu.Spawning", "add_circle" },
        { "menu.Lights", "lightbulb" },
        { "menu.Material", "palette" },
        { "menu.Reflections", "flip" },
        { "menu.SSAO", "blur_on" },
        { "menu.File", "folder" },
        { "menu.Edit", "tune" },
        { "menu.Render", "palette" },
        { "menu.World", "landscape" },
        { "menu.Create", "add_box" },
        { "menu.Scripts", "code" },
        // Menu items / buttons
        { "action.LoadScene", "folder_open" },
        { "action.SaveScene", "save" },
        { "action.LoadGLB", "upload_file" },
        { "action.ResetToTeapot", "restore" },
        { "action.SpawnEntity", "add" },
        { "action.Delete", "delete" },
        { "action.Deselect", "deselect" },
        { "action.Run", "play_arrow" },
        { "action.AddAnimated", "animation" },
        { "action.AddPickable", "touch_app" },
        { "action.AttachScript", "attach_file" },
        { "action.Fullscreen", "fullscreen" },
        // Window titles
        { "window.Metrics", "bar_chart" },
        { "window.EntityInspector", "info" },
    };
};
