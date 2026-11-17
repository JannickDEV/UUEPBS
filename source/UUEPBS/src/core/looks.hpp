// UUEPBS - Looks: skin and hair recolouring through the outfit's own material parameters
//
// The window's Looks tab edits a handful of values per character (skin tone, sweat, hair hue /
// saturation / lightness). They travel with the morph weights: a look is a MorphBook entry named
// "look:<id>" (e.g. "look:skin.tone"). That way a character's looks are saved, remembered, synced,
// undone and sent to Lua exactly like its sliders, with no new plumbing:
//   * presets and _characters/<identity>.json write them in their own "looks" object
//     ({ "skin.tone": -2, "skin.sweat": 1.5 }); older builds ignore that object;
//   * the bridge sends them as ordinary "mw" lines; the Lua script (uuepbs_looks.lua) picks out the
//     "look:" names and recolours the meshes' material slots instead of calling SetMorphTarget;
//   * Lua reports back what it changed per character ("looks" lines), shown under the sliders.
// A look that is absent (or at its neutral value) leaves the game's material untouched.
#pragma once

#include "morphs.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace uuepbs::looks
{
    constexpr std::string_view kPrefix = "look:";

    struct Control
    {
        const char* id;       // "skin.tone" (stored as "look:skin.tone")
        const char* category; // "Skin" / "Hair"
        const char* title;    // slider label
        const char* hint;     // tooltip
        float lo, hi;         // slider range (inside kMorphMin..kMorphMax)
        float neutral;        // the game's own look
    };

    struct Category
    {
        const char* name;
        const char* note; // one line under the section bar
    };

    const std::vector<Category>& categories();
    const std::vector<Control>& controls();
    const Control* find(std::string_view id);

    // "look:skin.tone" for "skin.tone".
    std::string key_of(std::string_view id);
    // True for "look:..." (any case).
    bool is_look(std::string_view name);
    // "skin.tone" for "look:skin.tone" (folded); "" when it is not a look.
    std::string id_of(std::string_view name);

    // Value of a control in a book; nullopt-free: the neutral value when absent.
    float value_of(const MorphBook& book, const Control& c);
    bool is_set(const MorphBook& book, const Control& c);
    // Writes (or, at neutral, removes) one look in a book.
    void put(MorphBook& book, const Control& c, float v);

    // Entries of a book that are looks / are real morph targets.
    size_t count(const MorphBook& book);
    MorphBook only_looks(const MorphBook& book);
    MorphBook without_looks(const MorphBook& book);
    // A preset made before Looks (or saved without any) keeps the character's current looks:
    // copies them from `current` into `loaded` when `loaded` has none.
    void keep_when_absent(MorphBook& loaded, const MorphBook& current);

    // Short words for the category list: "tan + sweat", "hue + lighter"; "" when nothing is set.
    std::string summary(const MorphBook& book, std::string_view category);
    // Number of set controls in a category.
    size_t changed(const MorphBook& book, std::string_view category);

    // What the Lua script last reported per character (actor key -> one line).
    class Report
    {
      public:
        static Report& instance();
        void set(std::map<std::string, std::string> lines);
        std::string get(const std::string& actor) const;
        uint64_t revision() const;

      private:
        mutable std::mutex m_lock;
        std::map<std::string, std::string> m_lines;
        uint64_t m_revision{1};
    };
} // namespace uuepbs::looks
