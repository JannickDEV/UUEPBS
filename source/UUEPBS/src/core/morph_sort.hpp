// UUEPBS - automatic Body tab sliders for morph targets
//
// A mesh's morph targets used to reach the Body tab only through "MorphGroups" in the game
// profile; everything else sat in Advanced > Morphs. This file sorts the rest by name:
//
//   Morph_BreastEnl   -> Chest        "Breast enlarge"
//   Exp_BrowUpL + R   -> Face         "Brow up"            (one slider drives both sides)
//   Morph_HipsWide    -> Hips & Legs  "Hips wide"
//   Form_Felian       -> Body shape   "Felian form"
//   Crctve_KneeCapL   -> Advanced only (correctives and clothing helpers are driven by the game)
//   Morph_SFinery     -> Other        (nothing recognised: still a slider, never hidden)
//
// Names are split into words (underscores, dashes, camelCase, digits), common abbreviations
// are spelled out (Enl, Hei, Wid, Narr...), a trailing L / R is read as a side, and the words
// are looked up in a rule table. Profile "MorphGroups" win: a morph one of them claims is
// not sorted again, and "AutoMorphGroups": false in the profile turns the sorting off.
#pragma once

#include "morphs.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace uuepbs
{
    enum class MorphSide
    {
        None,
        Left,
        Right
    };

    struct MorphSort
    {
        std::string category; // Body tab category; empty = Advanced > Morphs only
        std::string label;    // readable slider name without the side ("Eye close")
        MorphSide side{MorphSide::None};
        bool randomize{false}; // part of Randomize (body shape, not faces or genitals)
    };

    // Words of a morph name, lower case, abbreviations spelled out ("Morph_NippleHei" -> morph nipple height).
    std::vector<std::string> morph_words(std::string_view name);
    // Category, readable label and side of one morph target.
    MorphSort sort_morph(std::string_view name);
    // Readable slider name ("Morph_BreastEnl" -> "Breast enlarge", "Eye_CloseL" -> "Eye close (left)").
    std::string morph_label(std::string_view name);

    // Body tab order of a category (bone and morph categories alike); unknown names sit between
    // "Body shape" and "Groin", "Other" is always last.
    int section_rank(std::string_view section);

    // Profile groups first, then one automatic group per remaining morph (left/right pairs share
    // one), sorted by category. Morphs claimed by a profile group, and correctives / clothing
    // helpers, get no automatic group.
    std::vector<MorphGroupMorphs> body_morph_groups(const std::vector<std::string>& morphs, const MorphProfile& profile);
} // namespace uuepbs
