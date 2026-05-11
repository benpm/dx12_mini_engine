module;

#include <string>
#include <string_view>
#include <unordered_map>

export module localisation;

// Localisation — flat key→string lookup, loaded from resources/i18n/<locale>.json.
// Trivial bag-of-strings, no plural forms or ICU. Future revisions will plug into
// glaze (consistent with rest of the engine) and add %s-style interpolation.
//
// engine.tr("HELLO_WORLD") from Lua returns the looked-up string or the key
// itself if no translation exists (typical "missing translation" pattern).
export class Localisation
{
   public:
    Localisation() = default;

    // Load translations from a JSON file: {"key": "value", ...}. Returns true
    // on success (also true on empty files); existing entries are overwritten.
    bool load(const std::string& path);

    // Look up a key. Returns the key itself if no translation exists.
    std::string_view tr(std::string_view key) const;

    // Number of entries loaded.
    std::size_t size() const { return table.size(); }

   private:
    std::unordered_map<std::string, std::string> table;
};
