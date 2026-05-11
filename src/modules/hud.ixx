module;

#include <cstdint>
#include <string>
#include <vector>

export module hud;

// Hud — minimal retained-mode in-game UI built on top of ImGui's foreground
// draw list. Designed for in-game HUDs (health bars, score, prompts) — for
// editor panels keep using ImGui directly.
//
// Lua scripts populate the HUD each frame (or persistently) via
// engine.hud_text / engine.hud_rect / engine.hud_clear; render() walks the
// element list and emits draw commands. Coordinates are in pixels from the
// top-left of the window. Colors are 0xAABBGGRR (ImGui's packed format).
export struct HudElement
{
    enum class Kind : uint32_t
    {
        Text,
        FilledRect,
        OutlineRect,
    };
    Kind kind = Kind::Text;
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;  // for rects; for text, ignored
    float h = 0.0f;
    uint32_t color = 0xFFFFFFFFu;
    std::string text;  // for Text
    float scale = 1.0f;  // text size multiplier
};

export class Hud
{
   public:
    void addText(float x, float y, const std::string& text, uint32_t color = 0xFFFFFFFFu,
                 float scale = 1.0f);
    void addFilledRect(float x, float y, float w, float h, uint32_t color);
    void addOutlineRect(float x, float y, float w, float h, uint32_t color);
    void clear();

    // Submit all queued elements to ImGui's foreground draw list. Safe to call
    // when ImGui isn't running — render() short-circuits if the IO context is
    // missing.
    void render() const;

    const std::vector<HudElement>& elements() const { return list; }

   private:
    std::vector<HudElement> list;
};
