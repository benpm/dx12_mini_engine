module;

#include <gainput/gainput.h>
#include <Windows.h>
#include <string>
#include <unordered_map>

module input;

gainput::InputManager inputManager = gainput::InputManager{};

// clang-format off
static const struct { Key key; const char* name; } kKeyTable[] = {
    { Key::A, "A" }, { Key::B, "B" }, { Key::C, "C" }, { Key::D, "D" },
    { Key::E, "E" }, { Key::F, "F" }, { Key::G, "G" }, { Key::H, "H" },
    { Key::I, "I" }, { Key::J, "J" }, { Key::K, "K" }, { Key::L, "L" },
    { Key::M, "M" }, { Key::N, "N" }, { Key::O, "O" }, { Key::P, "P" },
    { Key::Q, "Q" }, { Key::R, "R" }, { Key::S, "S" }, { Key::T, "T" },
    { Key::U, "U" }, { Key::V, "V" }, { Key::W, "W" }, { Key::X, "X" },
    { Key::Y, "Y" }, { Key::Z, "Z" },
    { Key::Num0, "0" }, { Key::Num1, "1" }, { Key::Num2, "2" }, { Key::Num3, "3" },
    { Key::Num4, "4" }, { Key::Num5, "5" }, { Key::Num6, "6" }, { Key::Num7, "7" },
    { Key::Num8, "8" }, { Key::Num9, "9" },
    { Key::F1, "F1" }, { Key::F2, "F2" }, { Key::F3, "F3" }, { Key::F4, "F4" },
    { Key::F5, "F5" }, { Key::F6, "F6" }, { Key::F7, "F7" }, { Key::F8, "F8" },
    { Key::F9, "F9" }, { Key::F10, "F10" }, { Key::F11, "F11" }, { Key::F12, "F12" },
    { Key::Escape, "Escape" }, { Key::Tab, "Tab" }, { Key::Space, "Space" },
    { Key::Backspace, "Backspace" }, { Key::Enter, "Enter" }, { Key::CapsLock, "CapsLock" },
    { Key::LShift, "LShift" }, { Key::RShift, "RShift" }, { Key::Shift, "Shift" },
    { Key::LControl, "LControl" }, { Key::RControl, "RControl" }, { Key::Control, "Control" },
    { Key::LAlt, "LAlt" }, { Key::RAlt, "RAlt" }, { Key::Alt, "Alt" },
    { Key::Insert, "Insert" }, { Key::Delete, "Delete" }, { Key::Home, "Home" },
    { Key::End, "End" }, { Key::PageUp, "PageUp" }, { Key::PageDown, "PageDown" },
    { Key::Up, "Up" }, { Key::Down, "Down" }, { Key::Left, "Left" }, { Key::Right, "Right" },
    { Key::Pause, "Pause" }, { Key::ScrollLock, "ScrollLock" }, { Key::NumLock, "NumLock" },
    { Key::LBracket, "[" }, { Key::RBracket, "]" }, { Key::SemiColon, ";" },
    { Key::Comma, "," }, { Key::Period, "." }, { Key::Quote, "'" },
    { Key::Slash, "/" }, { Key::BackSlash, "\\" }, { Key::Tilde, "~" },
    { Key::Equal, "=" }, { Key::Dash, "-" },
};
// clang-format on

static std::unordered_map<std::string, Key> buildNameToKey()
{
    std::unordered_map<std::string, Key> m;
    for (const auto& e : kKeyTable) {
        m[e.name] = e.key;
    }
    return m;
}

static std::unordered_map<UINT, const char*> buildKeyToName()
{
    std::unordered_map<UINT, const char*> m;
    for (const auto& e : kKeyTable) {
        m[static_cast<UINT>(e.key)] = e.name;
    }
    return m;
}

const char* keyName(Key k)
{
    static auto table = buildKeyToName();
    auto it = table.find(static_cast<UINT>(k));
    return it != table.end() ? it->second : "?";
}

Key keyFromName(const std::string& name)
{
    static auto table = buildNameToKey();
    auto it = table.find(name);
    return it != table.end() ? it->second : Key::Escape;
}