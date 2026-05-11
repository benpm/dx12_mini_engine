module;

#if defined(__clang__)
    #define FMT_CONSTEVAL
#endif

#include <imgui.h>

#include <cstdint>
#include <string>

module hud;

#include "audio_capi.h"

extern "C" void engine_hud_clear(void* hudPtr)
{
    if (auto* h = static_cast<Hud*>(hudPtr)) {
        h->clear();
    }
}

extern "C" void engine_hud_text(
    void* hudPtr, float x, float y, const char* text, unsigned int color, float scale
)
{
    if (!hudPtr || !text) {
        return;
    }
    static_cast<Hud*>(hudPtr)->addText(x, y, text, color, scale == 0.0f ? 1.0f : scale);
}

extern "C" void engine_hud_filled_rect(
    void* hudPtr, float x, float y, float w, float h, unsigned int color
)
{
    if (!hudPtr) {
        return;
    }
    static_cast<Hud*>(hudPtr)->addFilledRect(x, y, w, h, color);
}

extern "C" void engine_hud_outline_rect(
    void* hudPtr, float x, float y, float w, float h, unsigned int color
)
{
    if (!hudPtr) {
        return;
    }
    static_cast<Hud*>(hudPtr)->addOutlineRect(x, y, w, h, color);
}

void Hud::addText(float x, float y, const std::string& text, uint32_t color, float scale)
{
    HudElement e;
    e.kind = HudElement::Kind::Text;
    e.x = x;
    e.y = y;
    e.color = color;
    e.text = text;
    e.scale = scale;
    list.push_back(std::move(e));
}

void Hud::addFilledRect(float x, float y, float w, float h, uint32_t color)
{
    HudElement e;
    e.kind = HudElement::Kind::FilledRect;
    e.x = x;
    e.y = y;
    e.w = w;
    e.h = h;
    e.color = color;
    list.push_back(std::move(e));
}

void Hud::addOutlineRect(float x, float y, float w, float h, uint32_t color)
{
    HudElement e;
    e.kind = HudElement::Kind::OutlineRect;
    e.x = x;
    e.y = y;
    e.w = w;
    e.h = h;
    e.color = color;
    list.push_back(std::move(e));
}

void Hud::clear()
{
    list.clear();
}

void Hud::render() const
{
    if (!ImGui::GetCurrentContext()) {
        return;
    }
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    if (!dl) {
        return;
    }
    ImFont* font = ImGui::GetFont();
    float baseSize = ImGui::GetFontSize();
    for (const auto& e : list) {
        switch (e.kind) {
            case HudElement::Kind::Text: {
                if (e.scale != 1.0f && font) {
                    dl->AddText(
                        font, baseSize * e.scale, ImVec2(e.x, e.y), e.color, e.text.c_str()
                    );
                } else {
                    dl->AddText(ImVec2(e.x, e.y), e.color, e.text.c_str());
                }
                break;
            }
            case HudElement::Kind::FilledRect:
                dl->AddRectFilled(ImVec2(e.x, e.y), ImVec2(e.x + e.w, e.y + e.h), e.color);
                break;
            case HudElement::Kind::OutlineRect:
                dl->AddRect(ImVec2(e.x, e.y), ImVec2(e.x + e.w, e.y + e.h), e.color);
                break;
        }
    }
}
