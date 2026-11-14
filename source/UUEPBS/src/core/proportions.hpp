// UUEPBS - Character Scale tab: overall height and broad proportions
//
// A layer on top of the bone edits. Each slider multiplies the bones of one or
// more Body tab groups (Head, Upper arms + Forearms, Thighs + Calves, ...), so a
// Body tab "Thighs 1.2" and a Character Scale "Leg length 1.1" both apply.
// Height is not a bone edit: it scales the whole pose about the mesh origin
// (the feet on most characters), so the character stays on the ground.
//
// The values live in the character's EditBook as pseudo entries keyed
// "@scale:<id>" (uniform axis = the value), so they travel with everything that
// already carries a book: per-character memory, the session, undo and presets.
// Presets write them as a separate "proportions" object, never under "bones";
// older builds ignore that key.
#pragma once

#include "body_groups.hpp"
#include "sculpt.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace uuepbs
{
    struct ScaleSlider
    {
        const char* id;      // preset key, e.g. "leg_length"
        const char* section; // "Overall" or "Proportions"
        const char* title;
        const char* hint;
        std::vector<const char*> groups; // Body tab group titles; empty = the whole character (height)
        bool length_only;                // X (along the bone) only, else uniform
        Spread spread;                   // for bones the Body tab or Advanced tab has not touched
        double lo;
        double hi;
    };

    constexpr const char* kScaleKeyPrefix = "@scale:";
    constexpr double kScaleNeutralEps = 1.0e-4;

    const std::vector<ScaleSlider>& scale_sliders();
    const ScaleSlider* find_scale_slider(std::string_view id);

    // "@scale:<id>" (already case-folded).
    std::string scale_key(std::string_view id);
    // True for a pseudo entry key ("@scale:..."), compared case-insensitively.
    bool is_scale_key(std::string_view key);
    // The id of a pseudo entry key, empty when it is not one.
    std::string scale_id_of(std::string_view key);

    // 1.0 when the slider is not set.
    double scale_value(const EditBook& book, std::string_view id);
    // Writes (or, at 1.0, removes) the pseudo entry. Values are clamped to the slider's range.
    void set_scale_value(EditBook& book, std::string_view id, double value);
    // The pseudo entry for `id` set to `value` (what Registry::set_bone stores).
    EditEntry scale_entry(std::string_view id, double value);
    bool has_proportions(const EditBook& book);
    // Number of real bone entries (pseudo entries left out).
    size_t bone_entry_count(const EditBook& book);

    // The book the sculptor should bind: real bone edits with the proportion multipliers
    // folded in, pseudo entries removed. `world` receives the height factor (1 = unchanged).
    // `map` must be resolved against the same skeleton the result is bound to.
    EditBook apply_proportions(const EditBook& book, const BodyMap& map, double& world);
} // namespace uuepbs
