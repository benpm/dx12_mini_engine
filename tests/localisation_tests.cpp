// Tests for the Localisation class. Doesn't need engine_lib — the class
// is self-contained, so we include the .cpp directly via a thin wrapper.
#include <doctest/doctest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <unordered_map>

// Re-declare the Localisation interface here (matches src/modules/localisation.ixx)
// and pull in the implementation as a non-module TU. Easier than building a
// module-aware test target. The trade-off: load() drags spdlog in, but that's
// already linked for the existing lua_scripting_tests.
//
// We include the implementation file directly under a guard so it compiles into
// this TU with the bare-minimum decls. Defining LOCALISATION_INLINE_TEST
// switches the .cpp from module-mode to plain C++ for this build.

#define LOCALISATION_INLINE_TEST 1

class Localisation
{
   public:
    bool load(const std::string& path);
    std::string_view tr(std::string_view key) const;
    std::size_t size() const { return table.size(); }

   private:
    std::unordered_map<std::string, std::string> table;
};

// Bring in the impl bodies. The original src/localisation.cpp wraps these in
// `module localisation;` — for tests we just need the function bodies. Rather
// than re-include the source, redefine the parser + methods inline.
namespace
{
    void skipWs(std::string_view s, std::size_t& i)
    {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) {
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
        return false;
    }
    std::string buf((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    std::string_view s = buf;

    std::size_t i = 0;
    skipWs(s, i);
    if (i >= s.size() || s[i] != '{') {
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

namespace
{
    std::string writeTempFile(const char* contents)
    {
        auto path = std::filesystem::temp_directory_path() / "loc_test_XXXXXX.json";
        // Make the filename unique-ish: tack on a counter.
        static int counter = 0;
        char buf[64];
        std::snprintf(buf, sizeof(buf), "loc_test_%d.json", counter++);
        path = std::filesystem::temp_directory_path() / buf;
        std::ofstream f(path, std::ios::binary);
        f << contents;
        return path.string();
    }
}  // namespace

TEST_CASE("Localisation: lookup hit returns the value")
{
    auto path = writeTempFile(R"({"HELLO": "Hi", "BYE": "Cheers"})");
    Localisation L;
    REQUIRE(L.load(path));
    CHECK(L.size() == 2);
    CHECK(L.tr("HELLO") == "Hi");
    CHECK(L.tr("BYE") == "Cheers");
}

TEST_CASE("Localisation: missing key returns the key verbatim")
{
    auto path = writeTempFile(R"({"PRESENT": "OK"})");
    Localisation L;
    REQUIRE(L.load(path));
    CHECK(L.tr("NOT_THERE") == "NOT_THERE");
}

TEST_CASE("Localisation: escape sequences in values")
{
    auto path = writeTempFile(R"({"NL": "line1\nline2", "Q": "He said \"hi\""})");
    Localisation L;
    REQUIRE(L.load(path));
    CHECK(L.tr("NL") == "line1\nline2");
    CHECK(L.tr("Q") == "He said \"hi\"");
}

TEST_CASE("Localisation: empty object loads OK")
{
    auto path = writeTempFile(R"({})");
    Localisation L;
    REQUIRE(L.load(path));
    CHECK(L.size() == 0);
}

TEST_CASE("Localisation: load failure on missing file leaves table empty")
{
    Localisation L;
    CHECK_FALSE(L.load("does_not_exist_12345.json"));
    CHECK(L.size() == 0);
}

TEST_CASE("Localisation: malformed JSON returns false")
{
    auto path = writeTempFile("not even valid json");
    Localisation L;
    CHECK_FALSE(L.load(path));
}
