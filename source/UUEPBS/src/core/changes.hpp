// UUEPBS - what a set of edits changes, by body part
//
// Used by the Simplified Panel's "Changed only" filter and by the preset details in the
// Presets tab (which body parts and morphs a preset changes, before loading it).
#pragma once

#include "body_groups.hpp"
#include "morphs.hpp"
#include "sculpt.hpp"

#include <string>
#include <vector>

namespace uuepbs
{
    // Any bone of the group has a non-neutral edit (scale, rotation or move).
    bool group_changed(const GroupBones& group, const EditBook& book);
    // Any morph of the group is set (overrides the game's value).
    bool morph_group_changed(const MorphGroupMorphs& group, const MorphBook& morphs);

    struct PartChange
    {
        std::string section;
        std::string title;
        std::vector<std::string> bones; // edited bones of this part, preset spelling
        double scale{1.0};              // average size of the first edited bone
        bool turn{false};
        bool shift{false};
    };

    struct MorphChange
    {
        std::string name;
        double weight{0.0};
        std::string group; // MorphGroups title, empty when not in one
        bool present{true}; // the current character has this morph
    };

    struct EditSummary
    {
        std::vector<PartChange> parts;         // in Simplified Panel order
        std::vector<PartChange> other_bones;   // one entry per edited bone not in any body part
        std::vector<std::string> missing;      // edited bones the current skeleton does not have
        std::vector<MorphChange> morphs;
        size_t bones{0}; // edited bones in total
    };

    // `skeleton`: bone names of the current character (empty = unknown, nothing is reported missing).
    EditSummary summarize_edits(const EditBook& book, const MorphBook& morphs, const BodyMap& body,
                                const std::vector<MorphGroupMorphs>& morph_groups, const std::vector<std::string>& skeleton,
                                const std::vector<std::string>& morph_names);
} // namespace uuepbs
