// UUEPBS - "universal morphs": body parts found by bone name, for meshes without a morph for them
//
// Many skeletons carry soft-body or anatomy bones (penis_01..03, glans, testicle_l/r, labia...)
// but no morph target that sizes them. The Body tab turns such bones into morph-like sliders
// (Penis length, Testicle size, ...; see the "body" entries of scale_sliders() in
// proportions.hpp). This file only finds the bones: each part is a list of name patterns
// matched against the cleaned bone name (rig_names.hpp: bone_stem + glob_match), so it works
// for any game whose rig names them in a recognisable way.
//
// A real morph target always wins: part_covered_by_morph() says when a mesh already has a
// morph for the same thing, and the Body tab then shows the morph and hides the bone slider.
#pragma once

#include "rig_names.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace uuepbs
{
    struct BonePart
    {
        const char* id;    // "penis", "glans", "testicles", "vulva"
        const char* title; // for tooltips: "penis shaft"
        std::vector<const char*> patterns; // glob patterns on bone_stem(); any match joins the part
        std::vector<const char*> exclude;  // a stem containing one of these never joins
    };

    const std::vector<BonePart>& bone_parts();
    const BonePart* find_bone_part(std::string_view id);

    struct PartBones
    {
        std::string id;
        std::vector<std::string> bones; // real bone names, skeleton order
    };

    // Matches every part against a skeleton. Bones flagged in `taken` (claimed by a Body tab
    // group) are skipped, and a bone joins at most one part (the first in bone_parts() order).
    // Parts with no bones are left out.
    std::vector<PartBones> resolve_bone_parts(const std::vector<std::string>& names, const StemRules* rules = nullptr,
                                              const std::vector<uint8_t>* taken = nullptr);

    // True when the morph target `morph` already does what a bone slider would: it names the
    // part (`part_words`, e.g. penis / cock) and either one of `dimension_words` (length, girth...)
    // or nothing else at all ("mGen_Balls" covers Testicle size, "Penis_Length" covers Penis
    // length but not Penis size). Empty `dimension_words` = any morph naming the part with a
    // size word or no other word.
    bool part_covered_by_morph(std::string_view morph, const std::vector<const char*>& part_words,
                               const std::vector<const char*>& dimension_words);
} // namespace uuepbs
