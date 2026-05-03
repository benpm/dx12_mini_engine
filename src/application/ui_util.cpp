module;

#if defined(__clang__)
    #define FMT_CONSTEVAL
#endif

#include <nfd.h>
#include <spdlog/spdlog.h>
#include <cstdlib>
#include <string>
#include <vector>

module application;

// Translate the engine's filter-list format
//   { ("Scene Files", "*.json"), ("All Files", "*.*") }
// into NFD's compact form
//   "json"
// NFD takes a single filter string: extensions separated by commas (e.g.
// "png,jpg") and filter groups separated by semicolons (e.g. "png,jpg;psd").
// It does not surface per-filter display names. We drop the "All Files" /
// "*.*" entry because NFD treats a NULL filter list as "accept anything".
static std::string nfdFilterFromPairs(
    const std::vector<std::pair<std::string, std::string>>& filters
)
{
    std::string out;
    for (const auto& f : filters) {
        const auto& spec = f.second;
        if (spec.empty() || spec == "*.*") {
            continue;
        }
        // spec is like "*.json" or "*.glb;*.gltf" — strip the "*." prefix
        // from each token, replace ';' with ',', skip empty tokens.
        std::string group;
        size_t pos = 0;
        while (pos < spec.size()) {
            size_t end = spec.find(';', pos);
            if (end == std::string::npos) {
                end = spec.size();
            }
            std::string tok = spec.substr(pos, end - pos);
            if (tok.size() > 2 && tok[0] == '*' && tok[1] == '.') {
                tok.erase(0, 2);
            }
            if (!tok.empty()) {
                if (!group.empty()) {
                    group += ',';
                }
                group += tok;
            }
            pos = (end == spec.size()) ? end : end + 1;
        }
        if (!group.empty()) {
            if (!out.empty()) {
                out += ';';
            }
            out += group;
        }
    }
    return out;
}

std::string Application::openNativeFileDialog(
    FileDialogType type,
    const char* title,
    const std::vector<std::pair<std::string, std::string>>& filters,
    const char* defaultExtension
)
{
    (void)title;              // NFD does not expose a title parameter on Win32
    (void)defaultExtension;   // NFD's save dialog appends extensions automatically

    const std::string filterStr = nfdFilterFromPairs(filters);
    const nfdchar_t* filterArg = filterStr.empty() ? nullptr : filterStr.c_str();

    nfdchar_t* outPath = nullptr;
    nfdresult_t r = (type == FileDialogType::Open)
                        ? NFD_OpenDialog(filterArg, nullptr, &outPath)
                        : NFD_SaveDialog(filterArg, nullptr, &outPath);

    if (r == NFD_OKAY && outPath) {
        std::string result(outPath);
        std::free(outPath);
        return result;
    }
    if (r == NFD_ERROR) {
        spdlog::warn("NFD error: {}", NFD_GetError());
    }
    return {};
}
