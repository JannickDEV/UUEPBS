#include "proportions.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace uuepbs
{
    const std::vector<ScaleSlider>& scale_sliders()
    {
        static const std::vector<ScaleSlider> sliders = {
            {"height", "Overall", "Height",
             "The whole character, scaled from the mesh origin (the feet on most characters), so it stays on the ground.\n"
             "Every mesh of the character scales with it. Collision and the camera stay as they are.",
             {}, ScaleAxes::Uniform, Spread::Chain, 0.5, 2.0},
            {"head", "Proportions", "Head size", "The head and everything on it (face, hair bones).", {"Head"}, ScaleAxes::Uniform, Spread::Chain,
             0.5, 2.0},
            {"neck", "Proportions", "Neck length", "Longer or shorter neck; the head keeps its size.", {"Neck"}, ScaleAxes::Length, Spread::Keep,
             0.5, 2.0},
            {"shoulders", "Proportions", "Shoulder width", "Collarbone length: moves the arms in or out.", {"Shoulders"}, ScaleAxes::Length,
             Spread::Keep, 0.5, 2.0},
            {"torso", "Proportions", "Torso length", "Waist and rib cage length; the chest and arms keep their size.", {"Waist", "Rib cage"},
             ScaleAxes::Length, Spread::Keep, 0.5, 2.0},
            {"arm_length", "Proportions", "Arm length", "Upper arm and forearm length; the hands keep their size.", {"Upper arms", "Forearms"},
             ScaleAxes::Length, Spread::Keep, 0.5, 2.0},
            {"hands", "Proportions", "Hand size", "Hands and fingers.", {"Hands"}, ScaleAxes::Uniform, Spread::Chain, 0.5, 2.0},
            {"leg_length", "Proportions", "Leg length", "Thigh and calf length; the feet keep their size.", {"Thighs", "Calves"}, ScaleAxes::Length,
             Spread::Keep, 0.5, 2.0},
            {"feet", "Proportions", "Foot size", "Feet and toes.", {"Feet"}, ScaleAxes::Uniform, Spread::Chain, 0.5, 2.0},

            // Muscle: thickness only (the two axes across the bone), so limbs get bulkier without
            // getting longer. Children keep their size: hands, feet and the head never balloon.
            {"muscle", "Muscle", "Overall muscle",
             "Every muscle slider below at once: thicker arms, legs, neck and chest, same length. "
             "Multiplies with the single sliders.",
             {"Upper arms", "Forearms", "Thighs", "Calves", "Neck", "Rib cage", "Shoulders"}, ScaleAxes::Girth, Spread::Keep, 0.5, 2.0,
             {"Deltoids", "Biceps", "Triceps", "Pecs", "Lats", "Traps", "Serratus", "Abs", "Obliques", "Quads", "Hamstrings", "Calf muscles"}},
            {"upper_arm_girth", "Muscle", "Upper arm thickness", "Biceps and triceps: thicker upper arms, same length.", {"Upper arms"},
             ScaleAxes::Girth, Spread::Keep, 0.5, 2.0, {"Biceps", "Triceps"}},
            {"forearm_girth", "Muscle", "Forearm thickness", "Thicker forearms, same length; the hands keep their size.", {"Forearms"},
             ScaleAxes::Girth, Spread::Keep, 0.5, 2.0},
            {"shoulder_bulk", "Muscle", "Shoulder bulk", "Deltoids and traps: rounder, heavier shoulders. The arms keep their length.",
             {"Shoulders"}, ScaleAxes::Girth, Spread::Keep, 0.5, 2.0, {"Deltoids", "Traps"}},
            {"chest_breadth", "Muscle", "Chest breadth", "A broader, deeper rib cage (pecs and lats along); arms, neck and breasts keep their size.",
             {"Rib cage"}, ScaleAxes::Girth, Spread::Keep, 0.5, 2.0, {"Pecs", "Lats", "Serratus"}},
            {"waist_girth", "Muscle", "Waist thickness", "Thicker or slimmer waist (abs and obliques along), same torso length.", {"Waist"},
             ScaleAxes::Girth, Spread::Keep, 0.5, 2.0, {"Abs", "Obliques"}},
            {"neck_girth", "Muscle", "Neck thickness", "Thicker neck, same length; the head keeps its size.", {"Neck"}, ScaleAxes::Girth,
             Spread::Keep, 0.5, 2.0},
            {"thigh_girth", "Muscle", "Thigh thickness", "Quads and hamstrings: thicker thighs, same leg length.", {"Thighs"}, ScaleAxes::Girth,
             Spread::Keep, 0.5, 2.0, {"Quads", "Hamstrings"}},
            {"calf_girth", "Muscle", "Calf thickness", "Thicker calves, same leg length; the feet keep their size.", {"Calves"}, ScaleAxes::Girth,
             Spread::Keep, 0.5, 2.0, {"Calf muscles"}},

            // Body tab "universal morphs": built from anatomy bones when the mesh has no morph for it.
            {"penis_size", "Groin", "Penis size", "The whole shaft and glans, built from the bones (no morph target on this mesh).", {},
             ScaleAxes::Uniform, Spread::Keep, 0.3, 3.0, {}, {"penis", "glans"}, true, {"penis", "cock", "dick", "phallus"}},
            {"penis_length", "Groin", "Penis length", "Longer or shorter shaft, same thickness; the glans keeps its size. Built from the bones.", {},
             ScaleAxes::Length, Spread::Keep, 0.3, 3.0, {}, {"penis"}, true, {"penis", "cock", "dick", "phallus", "shaft"},
             {"length", "long", "longer", "short", "shorter"}},
            {"penis_girth", "Groin", "Penis girth", "Thicker or thinner shaft, same length. Built from the bones.", {}, ScaleAxes::Girth,
             Spread::Keep, 0.3, 3.0, {}, {"penis"}, true, {"penis", "cock", "dick", "phallus", "shaft"},
             {"girth", "thick", "thicker", "thickness", "thin", "wide", "width", "fat"}},
            {"glans_size", "Groin", "Glans size", "The tip on its own. Built from the bones.", {}, ScaleAxes::Uniform, Spread::Keep, 0.3, 3.0, {},
             {"glans"}, true, {"glans"}},
            {"testicle_size", "Groin", "Testicle size", "Both testicles / the scrotum. Built from the bones.", {}, ScaleAxes::Uniform,
             Spread::Keep, 0.3, 3.0, {}, {"testicles"}, true, {"ball", "testicle", "testes", "testis", "scrotum", "nut"}},
            {"vulva_size", "Groin", "Vulva size", "Labia / vulva bones. Built from the bones.", {}, ScaleAxes::Uniform, Spread::Keep, 0.3, 3.0, {},
             {"vulva"}, true, {"vulva", "labia", "labium", "pussy", "vagina"}},
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
            if (s.groups.empty() && s.parts.empty())
            {
                world = std::clamp(v, s.lo, s.hi);
                continue;
            }
            auto scale_bone = [&](const std::string& bone, ScaleAxes axes, Spread fresh_spread) {
                const std::string bk = fold_case(bone);
                auto it = out.find(bk);
                if (it == out.end())
                {
                    EditEntry fresh;
                    fresh.bone = bone;
                    fresh.edit.spread = fresh_spread;
                    it = out.emplace(bk, fresh).first;
                }
                BoneEdit& e = it->second.edit;
                const int along = long_axis_of(map, bk);
                for (int a = 0; a < 3; ++a)
                {
                    const bool is_length = a == along;
                    if (axes == ScaleAxes::Uniform || (axes == ScaleAxes::Length) == is_length)
                    {
                        if (axes == ScaleAxes::Girth && e.joint[a] <= 0.0)
                        {
                            e.joint[a] = e.axis[a]; // the joints keep following the user's own scale only
                        }
                        else if (axes != ScaleAxes::Girth && e.joint[a] > 0.0)
                        {
                            e.joint[a] *= v;
                        }
                        e.axis[a] *= v;
                    }
                }
                if (axes == ScaleAxes::Girth && e.spread == Spread::Chain)
                {
                    e.spread = Spread::Keep; // thicker forearm, same hand
                }
            };
            auto each_group = [&](const std::vector<const char*>& titles, auto&& fn) {
                for (const char* title : titles)
                {
                    const std::string want = fold_case(title);
                    for (const GroupBones& gb : map.groups)
                    {
                        if (fold_case(gb.group.title) == want)
                        {
                            for (const std::string& bone : gb.bones)
                            {
                                fn(bone, gb);
                            }
                        }
                    }
                }
            };
            each_group(s.groups, [&](const std::string& bone, const GroupBones&) { scale_bone(bone, s.axes, s.spread); });
            each_group(s.helpers, [&](const std::string& bone, const GroupBones& gb) { scale_bone(bone, ScaleAxes::Uniform, gb.group.spread); });
            for (const char* part : s.parts)
            {
                for (const PartBones& pb : map.parts)
                {
                    if (pb.id == part)
                    {
                        for (const std::string& bone : pb.bones)
                        {
                            scale_bone(bone, s.axes, s.spread);
                        }
                    }
                }
            }
        }
        return out;
    }
} // namespace uuepbs

namespace uuepbs
{
    void measure_long_axes(BodyMap& map, const std::vector<std::string>& names, const std::vector<int32_t>& parents,
                           const std::vector<Xform>& pose)
    {
        map.long_axis.clear();
        const size_t n = names.size();
        if (parents.size() != n || pose.size() != n)
        {
            return;
        }
        // Farthest child of every bone, as an offset in the bone's own frame.
        std::vector<double> best(n, 0.0);
        std::vector<std::array<double, 3>> dir(n, {0.0, 0.0, 0.0});
        auto length2 = [](const double v[3]) { return v[0] * v[0] + v[1] * v[1] + v[2] * v[2]; };
        for (size_t i = 0; i < n; ++i)
        {
            const int32_t p = parents[i];
            if (p < 0 || static_cast<size_t>(p) >= n || !xf::finite(pose[i]) || !xf::finite(pose[p]))
            {
                continue;
            }
            const Xform local = xf::relative(pose[i], pose[p]);
            const double d = length2(local.pos);
            if (d > best[p])
            {
                best[p] = d;
                dir[p] = {local.pos[0], local.pos[1], local.pos[2]};
            }
        }
        for (size_t i = 0; i < n; ++i)
        {
            double v[3] = {dir[i][0], dir[i][1], dir[i][2]};
            if (best[i] <= 1e-8)
            {
                // A leaf (twist bone, glans...): keep going the way it came, seen from its own frame.
                const int32_t p = parents[i];
                if (p < 0 || static_cast<size_t>(p) >= n || !xf::finite(pose[i]) || !xf::finite(pose[p]))
                {
                    continue;
                }
                const Xform local = xf::relative(pose[i], pose[p]);
                double inv[4];
                xf::qconj(local.rot, inv);
                xf::qrotate(inv, local.pos, v);
                if (length2(v) <= 1e-8)
                {
                    continue;
                }
            }
            int axis = 0;
            for (int a = 1; a < 3; ++a)
            {
                if (std::fabs(v[a]) > std::fabs(v[axis]))
                {
                    axis = a;
                }
            }
            if (axis != 0)
            {
                map.long_axis[fold_case(names[i])] = static_cast<uint8_t>(axis);
            }
        }
    }

    int long_axis_of(const BodyMap& map, std::string_view bone)
    {
        const auto it = map.long_axis.find(fold_case(bone));
        return it == map.long_axis.end() ? 0 : it->second;
    }

    std::vector<std::string> scale_slider_bones(const ScaleSlider& s, const BodyMap& map)
    {
        std::vector<std::string> out;
        auto add = [&out](const std::string& b) {
            if (std::find(out.begin(), out.end(), b) == out.end())
            {
                out.push_back(b);
            }
        };
        for (const auto* list : {&s.groups, &s.helpers})
        {
            for (const char* title : *list)
            {
                for (const GroupBones& gb : map.groups)
                {
                    if (fold_case(gb.group.title) == fold_case(title))
                    {
                        for (const std::string& b : gb.bones)
                        {
                            add(b);
                        }
                    }
                }
            }
        }
        for (const char* part : s.parts)
        {
            for (const PartBones& pb : map.parts)
            {
                if (pb.id == part)
                {
                    for (const std::string& b : pb.bones)
                    {
                        add(b);
                    }
                }
            }
        }
        return out;
    }

    bool scale_slider_covered(const ScaleSlider& s, const std::vector<std::string>& morphs)
    {
        if (s.morph_words.empty())
        {
            return false;
        }
        return std::any_of(morphs.begin(), morphs.end(),
                           [&](const std::string& m) { return part_covered_by_morph(m, s.morph_words, s.morph_dims); });
    }
} // namespace uuepbs
