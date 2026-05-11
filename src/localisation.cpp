module;

#if defined(__clang__)
    #define FMT_CONSTEVAL
#endif

#include <spdlog/spdlog.h>

#include <fstream>
#include <sstream>
#include <string>

module localisation;

// Minimal JSON parser tailored to flat {"key": "value"} dictionaries. Glaze
// could parse this trivially but pulling glaze into a module body adds extra
// compile cost — for a feature this small a hand-rolled scanner is fine.
namespace
{
    void skipWs(std::string_view s, std::size_t& i)
    {
        while (i < s.size() &&
               (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) {
            ++i;
        }
    }

    bool parseString(std::string_view s, std::size_t& i, std::string& out)
    {
        skipWs(s, i);
        if (i >= s.size() || s[i] != '"') {
            return false;
        }
        ++i;
        out.clear();
        while (i < s.size() && s[i] != '"') {
            if (s[i] == '\\' && i + 1 < s.size()) {
                char e = s[i + 1];
                switch (e) {
                    case 'n':
                        out += '\n';
                        break;
                    case 't':
                        out += '\t';
                        break;
                    case '"':
                        out += '"';
                        break;
                    case '\\':
                        out += '\\';
                        break;
                    default:
                        out += e;
                        break;
                }
                i += 2;
            } else {
                out += s[i];
                ++i;
            }
        }
        if (i >= s.size()) {
            return false;
        }
        ++i;
        return true;
    }
}  // namespace

bool Localisation::load(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        spdlog::warn("Localisation: failed to open '{}'", path);
        return false;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    std::string buf = ss.str();
    std::string_view s = buf;

    std::size_t i = 0;
    skipWs(s, i);
    if (i >= s.size() || s[i] != '{') {
        spdlog::warn("Localisation: '{}' is not a JSON object", path);
        return false;
    }
    ++i;
    skipWs(s, i);
    while (i < s.size() && s[i] != '}') {
        std::string key, value;
        if (!parseString(s, i, key)) {
            return false;
        }
        skipWs(s, i);
        if (i >= s.size() || s[i] != ':') {
            return false;
        }
        ++i;
        if (!parseString(s, i, value)) {
            return false;
        }
        table[std::move(key)] = std::move(value);
        skipWs(s, i);
        if (i < s.size() && s[i] == ',') {
            ++i;
            skipWs(s, i);
        }
    }
    spdlog::info("Localisation: loaded {} entries from '{}'", table.size(), path);
    return true;
}

std::string_view Localisation::tr(std::string_view key) const
{
    auto it = table.find(std::string(key));
    if (it == table.end()) {
        return key;
    }
    return it->second;
}
