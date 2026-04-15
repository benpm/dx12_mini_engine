#pragma once

#include <string>
#include <unordered_map>

// Material Design Icons codepoints (from MaterialIcons-Regular.ttf)
// Font range: 0xe000 - 0xf8ff (Private Use Area)
namespace IconCP
{
    inline constexpr unsigned short MIN = 0xe000;
    inline constexpr unsigned short MAX = 0xf8ff;

    inline constexpr unsigned short DesktopWindows = 0xe30c;
    inline constexpr unsigned short Videocam = 0xe047;
    inline constexpr unsigned short Flare = 0xe3e4;
    inline constexpr unsigned short Tune = 0xe429;
    inline constexpr unsigned short Visibility = 0xe59f;
    inline constexpr unsigned short Landscape = 0xe3f7;
    inline constexpr unsigned short LightMode = 0xe518;
    inline constexpr unsigned short Contrast = 0xeb37;
    inline constexpr unsigned short Animation = 0xe71c;
    inline constexpr unsigned short AddCircle = 0xe147;
    inline constexpr unsigned short Lightbulb = 0xe0f0;
    inline constexpr unsigned short Palette = 0xe40a;
    inline constexpr unsigned short Flip = 0xe3e8;
    inline constexpr unsigned short BlurOn = 0xe3a5;
    inline constexpr unsigned short FolderOpen = 0xe2c8;
    inline constexpr unsigned short Folder = 0xe2c7;
    inline constexpr unsigned short AddBox = 0xe146;
    inline constexpr unsigned short Code = 0xe86f;
    inline constexpr unsigned short Save = 0xe161;
    inline constexpr unsigned short Delete = 0xe872;
    inline constexpr unsigned short PlayArrow = 0xe037;
    inline constexpr unsigned short Fullscreen = 0xe5d0;
    inline constexpr unsigned short Info = 0xe88e;
    inline constexpr unsigned short BarChart = 0xe26b;
    inline constexpr unsigned short Close = 0xe5cd;
    inline constexpr unsigned short Deselect = 0xebb6;
    inline constexpr unsigned short AttachFile = 0xe226;
    inline constexpr unsigned short TouchApp = 0xe925;
    inline constexpr unsigned short Settings = 0xe8b8;
    inline constexpr unsigned short WbSunny = 0xe430;
    inline constexpr unsigned short Add = 0xe145;
    inline constexpr unsigned short Restore = 0xe8b3;
    inline constexpr unsigned short UploadFile = 0xe2c6;
    inline constexpr unsigned short Search = 0xe8b6;
}  // namespace IconCP

// UTF-8 encode a BMP codepoint (U+0000..U+FFFF) into a std::string
inline std::string iconUtf8(unsigned short cp)
{
    std::string s;
    if (cp < 0x80) {
        s += static_cast<char>(cp);
    } else if (cp < 0x800) {
        s += static_cast<char>(0xC0 | (cp >> 6));
        s += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        s += static_cast<char>(0xE0 | (cp >> 12));
        s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        s += static_cast<char>(0x80 | (cp & 0x3F));
    }
    return s;
}

// Lookup icon codepoint by name string (for config-driven icon assignment)
inline unsigned short iconCodepointFromName(const std::string& name)
{
    static const std::unordered_map<std::string, unsigned short> map = {
        { "desktop_windows", IconCP::DesktopWindows },
        { "videocam", IconCP::Videocam },
        { "flare", IconCP::Flare },
        { "tune", IconCP::Tune },
        { "visibility", IconCP::Visibility },
        { "landscape", IconCP::Landscape },
        { "light_mode", IconCP::LightMode },
        { "contrast", IconCP::Contrast },
        { "animation", IconCP::Animation },
        { "add_circle", IconCP::AddCircle },
        { "lightbulb", IconCP::Lightbulb },
        { "palette", IconCP::Palette },
        { "flip", IconCP::Flip },
        { "blur_on", IconCP::BlurOn },
        { "folder_open", IconCP::FolderOpen },
        { "folder", IconCP::Folder },
        { "add_box", IconCP::AddBox },
        { "code", IconCP::Code },
        { "save", IconCP::Save },
        { "delete", IconCP::Delete },
        { "play_arrow", IconCP::PlayArrow },
        { "fullscreen", IconCP::Fullscreen },
        { "info", IconCP::Info },
        { "bar_chart", IconCP::BarChart },
        { "close", IconCP::Close },
        { "deselect", IconCP::Deselect },
        { "attach_file", IconCP::AttachFile },
        { "touch_app", IconCP::TouchApp },
        { "settings", IconCP::Settings },
        { "wb_sunny", IconCP::WbSunny },
        { "add", IconCP::Add },
        { "restore", IconCP::Restore },
        { "upload_file", IconCP::UploadFile },
        { "search", IconCP::Search },
    };
    auto it = map.find(name);
    return it != map.end() ? it->second : 0;
}

// Get icon UTF-8 string by name, with trailing space for label use
inline std::string iconStr(const std::string& name)
{
    auto cp = iconCodepointFromName(name);
    return cp ? iconUtf8(cp) + " " : "";
}
