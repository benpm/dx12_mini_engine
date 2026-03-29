// Separate TU for glaze so its heavy templates don't mix with Windows.h/DirectX headers
#ifdef __clang__
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Weverything"
#endif
#include <glaze/glaze.hpp>
#ifdef __clang__
    #pragma clang diagnostic pop
#endif

#include "scene_data.h"

bool readSceneJson(const std::string& path, SceneFileData& out, std::string& err)
{
    std::string buf;
    auto ec = glz::read_file_json(out, path, buf);
    if (ec) {
        err = glz::format_error(ec, buf);
        return false;
    }
    return true;
}

bool writeSceneJson(const std::string& path, const SceneFileData& data, std::string& err)
{
    auto ec = glz::write_file_json<glz::opts{ .prettify = true }>(data, path, std::string{});
    if (ec) {
        err = glz::format_error(ec, std::string{});
        return false;
    }
    return true;
}
