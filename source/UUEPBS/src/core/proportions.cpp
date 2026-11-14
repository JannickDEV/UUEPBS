#include "proportions.hpp"

#include <algorithm>
#include <cmath>

namespace uuepbs
{
    const std::vector<ScaleSlider>& scale_sliders()
    {
        static const std::vector<ScaleSlider> sliders = {
            {"height", "Overall", "Height",
             "The whole character, scaled from the mesh origin (the feet on most characters), so it stays on the ground.\n"
             "Every mesh of the character scales with it. Collision and the camera stay as they are.",
             {}, false, Spread::Chain, 0.5, 2.0},
            {"head", "Proportions", "Head size", "The head and everything on it (face, hair bones).", {"Head"}, false, Spread::Chain, 0.5, 2.0},
            {"neck", "Proportions", "Neck length", "Longer or shorter neck; the head keeps its size.", {"Neck"}, true, Spread::Keep, 0.5, 2.0},
            {"shoulders", "Proportions", "Shoulder width", "Collarbone length: moves the arms in or out.", {"Shoulders"}, true, Spread::Keep, 0.5, 2.0},
            {"torso", "Proportions", "Torso length", "Waist and rib cage length; the chest and arms keep their size.", {"Waist", "Rib cage"}, true,
             Spread::Keep, 0.5, 2.0},
            {"arm_length", "Proportions", "Arm length", "Upper arm and forearm length; the hands keep their size.", {"Upper arms", "Forearms"}, true,
             Spread::Keep, 0.5, 2.0},
            {"hands", "Proportions", "Hand size", "Hands and fingers.", {"Hands"}, false, Spread::Chain, 0.5, 2.0},
            {"leg_length", "Proportions", "Leg length", "Thigh and calf length; the feet keep their size.", {"Thighs", "Calves"}, true, Spread::Keep,
             0.5, 2.0},
            {"feet", "Proportions", "Foot size", "Feet and toes.", {"Feet"}, false, Spread::Chain, 0.5, 2.0},
        };
        return sliders;
    }

    const ScaleSlider* find_scale_slider(std::string_view id)
    {
        const std::string folded = fold_case(id);
        for (const ScaleSlider& s : scale_sliders())
        {
            if (folded == s.id)
            {
                return &s;
            }
        }
        return nullptr;
    }

    std::string scale_key(std::string_view id)
    {
        return std::string(kScaleKeyPrefix) + fold_case(id);
    }

    bool is_scale_key(std::string_view key)
    {
        const std::string_view prefix = kScaleKeyPrefix;
        return key.size() > prefix.size() && fold_case(key.substr(0, prefix.size())) == prefix;
    }

    std::string scale_id_of(std::string_view key)
    {
        return is_scale_key(key) ? fold_case(key.substr(std::string_view(kScaleKeyPrefix).size())) : std::string();
    }

    double scale_value(const EditBook& book, std::string_view id)
    {
        const auto it = book.find(scale_key(id));
        if (it == book.end() || !std::isfinite(it->second.edit.axis[0]) || it->second.edit.axis[0] <= 0.0)
        {
            return 1.0;
        }
        return it->second.edit.axis[0];
    }

    EditEntry scale_entry(std::string_view id, double value)
    {
        const ScaleSlider* s = find_scale_slider(id);
        if (!std::isfinite(value))
        {
            value = 1.0;
        }
        if (s)
        {
            value = std::clamp(value, s->lo, s->hi);
        }
        EditEntry e;
        e.bone = scale_key(id);
        e.edit.axis[0] = e.edit.axis[1] = e.edit.axis[2] = value;
        e.edit.spread = Spread::Chain;
        e.edit.clamp();
        return e;
    }

    void set_scale_value(EditBook& book, std::string_view id, double value)
    {
        EditEntry e = scale_entry(id, value);
        if (std::fabs(e.edit.axis[0] - 1.0) < kScaleNeutralEps)
        {
            book.erase(scale_key(id));
            return;
        }
        book[scale_key(id)] = std::move(e);
    }

    bool has_proportions(const EditBook& book)
    {
        // Pseudo keys start with '@', which sorts before letters and digits but after a few
        // punctuation marks, so look them up rather than relying on the map order.
        const auto it = book.lower_bound(kScaleKeyPrefix);
        return it != book.end() && is_scale_key(it->first);
    }

    size_t bone_entry_count(const EditBook& book)
    {
        size_t n = 0;
        for (const auto& [key, entry] : book)
        {
            n += is_scale_key(key) ? 0 : 1;
        }
        return n;
    }

    EditBook apply_proportions(const EditBook& book, const BodyMap& map, double& world)
    {
        world = 1.0;
        EditBook out;
        for (const auto& [key, entry] : book)
        {
            if (!is_scale_key(key))
            {
                out.emplace(key, entry);
            }
        }
        for (const ScaleSlider& s : scale_sliders())
        {
            const double v = scale_value(book, s.id);
            if (std::fabs(v - 1.0) < kScaleNeutralEps)
            {
                continue;
            }
            if (s.groups.empty())
            {
                world = std::clamp(v, s.lo, s.hi);
                continue;
            }
            for (const char* title : s.groups)
            {
                const std::string want = fold_case(title);
                for (const GroupBones& gb : map.groups)
                {
                    if (fold_case(gb.group.title) != want)
                    {
                        continue;
                    }
                    for (const std::string& bone : gb.bones)
                    {
                        const std::string bk = fold_case(bone);
                        auto it = out.find(bk);
                        if (it == out.end())
                        {
                            EditEntry fresh;
                            fresh.bone = bone;
                            fresh.edit.spread = s.spread;
                            it = out.emplace(bk, fresh).first;
                        }
                        BoneEdit& e = it->second.edit;
                        e.axis[0] *= v;
                        if (!s.length_only)
                        {
                            e.axis[1] *= v;
                            e.axis[2] *= v;
                        }
                    }
                }
            }
        }
        return out;
    }
} // namespace uuepbs
