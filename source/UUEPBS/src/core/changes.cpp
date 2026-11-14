#include "changes.hpp"

#include "proportions.hpp"
#include "rig_names.hpp"

#include <map>
#include <set>

namespace uuepbs
{
    bool group_changed(const GroupBones& group, const EditBook& book)
    {
        for (const std::string& bone : group.bones)
        {
            const auto it = book.find(fold_case(bone));
            if (it != book.end() && !it->second.edit.is_neutral())
            {
                return true;
            }
        }
        return false;
    }

    bool morph_group_changed(const MorphGroupMorphs& group, const MorphBook& morphs)
    {
        for (const std::string& m : group.morphs)
        {
            if (morphs.count(fold_case(m)) > 0)
            {
                return true;
            }
        }
        return false;
    }

    namespace
    {
        void note_bone(PartChange& part, const EditEntry& entry)
        {
            const BoneEdit& e = entry.edit;
            if (part.bones.empty())
            {
                part.scale = (e.axis[0] + e.axis[1] + e.axis[2]) / 3.0;
            }
            part.bones.push_back(entry.bone);
            part.turn = part.turn || e.has_turn();
            part.shift = part.shift || e.has_shift();
        }
    } // namespace

    EditSummary summarize_edits(const EditBook& book, const MorphBook& morphs, const BodyMap& body,
                                const std::vector<MorphGroupMorphs>& morph_groups, const std::vector<std::string>& skeleton,
                                const std::vector<std::string>& morph_names)
    {
        EditSummary out;

        // Each bone belongs to the first body part that lists it.
        std::map<std::string, size_t> part_of;
        for (size_t g = 0; g < body.groups.size(); ++g)
        {
            for (const std::string& b : body.groups[g].bones)
            {
                part_of.emplace(fold_case(b), g);
            }
        }
        std::set<std::string> on_skeleton;
        for (const std::string& n : skeleton)
        {
            on_skeleton.insert(fold_case(n));
        }

        std::vector<PartChange> parts(body.groups.size());
        for (const auto& [key, entry] : book)
        {
            if (entry.edit.is_neutral())
            {
                continue;
            }
            if (is_scale_key(key))
            {
                // Character Scale value (a pseudo entry, not a bone): listed by its slider name.
                const ScaleSlider* sl = find_scale_slider(scale_id_of(key));
                PartChange scale;
                scale.title = sl ? std::string(sl->title) : scale_id_of(key);
                note_bone(scale, entry);
                out.other_bones.push_back(std::move(scale));
                continue;
            }
            ++out.bones;
            if (!skeleton.empty() && on_skeleton.count(key) == 0)
            {
                out.missing.push_back(entry.bone);
                continue;
            }
            const auto it = part_of.find(key);
            if (it == part_of.end())
            {
                PartChange other;
                other.title = entry.bone;
                note_bone(other, entry);
                out.other_bones.push_back(std::move(other));
                continue;
            }
            note_bone(parts[it->second], entry);
        }
        for (size_t g = 0; g < parts.size(); ++g)
        {
            if (!parts[g].bones.empty())
            {
                parts[g].section = body.groups[g].group.section;
                parts[g].title = body.groups[g].group.title;
                out.parts.push_back(std::move(parts[g]));
            }
        }

        std::map<std::string, std::string> group_of;
        for (const MorphGroupMorphs& mg : morph_groups)
        {
            for (const std::string& m : mg.morphs)
            {
                group_of.emplace(fold_case(m), mg.group.title);
            }
        }
        std::set<std::string> known;
        for (const std::string& m : morph_names)
        {
            known.insert(fold_case(m));
        }
        for (const auto& [key, m] : morphs)
        {
            MorphChange c;
            c.name = m.name;
            c.weight = m.weight;
            const auto g = group_of.find(key);
            if (g != group_of.end())
            {
                c.group = g->second;
            }
            c.present = morph_names.empty() || known.count(key) > 0;
            out.morphs.push_back(std::move(c));
        }
        return out;
    }
} // namespace uuepbs
