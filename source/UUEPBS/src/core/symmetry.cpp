#include "symmetry.hpp"

#include "morph_sort.hpp"
#include "morphs.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <random>

namespace uuepbs
{
    namespace
    {
        // Folded name -> display spelling of every name the character has.
        std::map<std::string, std::string> index_of(const std::vector<std::string>& names)
        {
            std::map<std::string, std::string> out;
            for (const std::string& n : names)
            {
                out.emplace(fold_case(n), n);
            }
            return out;
        }

        // Uniform in [-1, 1], identical on every platform (std distributions are not).
        double unit(std::mt19937_64& rng)
        {
            const double u = static_cast<double>(rng() >> 11) * (1.0 / 9007199254740992.0); // [0, 1)
            return u * 2.0 - 1.0;
        }
    } // namespace

    EditSnapshot mirror_sides(const EditSnapshot& now, const std::vector<std::string>& bones, const std::vector<std::string>& morphs,
                              Side from, const MirrorTable& table, MirrorCount* count)
    {
        EditSnapshot out = now;
        MirrorCount c;
        if (from != Side::Left && from != Side::Right)
        {
            if (count)
            {
                *count = c;
            }
            return out;
        }

        const auto bone_index = index_of(bones);
        for (const auto& [key, name] : bone_index)
        {
            if (find_side(name).side != from)
            {
                continue;
            }
            const auto partner = bone_index.find(fold_case(mirror_bone_name(name)));
            if (partner == bone_index.end() || partner->first == key)
            {
                continue;
            }
            const auto src = now.bones.find(key);
            if (src == now.bones.end() || src->second.edit.is_neutral())
            {
                const auto old = out.bones.find(partner->first);
                if (old != out.bones.end())
                {
                    if (!old->second.edit.is_neutral())
                    {
                        ++c.bones;
                    }
                    out.bones.erase(old);
                }
                continue;
            }
            BoneEdit m = mirror_with(table, name, src->second.edit);
            m.spread = src->second.edit.spread;
            m.clamp();
            out.bones[partner->first] = EditEntry{partner->second, m};
            ++c.bones;
        }

        const auto morph_index = index_of(morphs);
        for (const auto& [key, name] : morph_index)
        {
            if (morph_side(name) != from)
            {
                continue;
            }
            const auto partner = morph_index.find(fold_case(mirror_morph_name(name)));
            if (partner == morph_index.end() || partner->first == key)
            {
                continue;
            }
            const auto src = now.morphs.find(key);
            if (src == now.morphs.end())
            {
                c.morphs += out.morphs.erase(partner->first);
                continue;
            }
            out.morphs[partner->first] = MorphEntry{partner->second, src->second.weight};
            ++c.morphs;
        }
        if (count)
        {
            *count = c;
        }
        return out;
    }

    void RandomLimits::clamp()
    {
        const auto fix = [](double v, double lo, double hi) { return std::isfinite(v) ? std::clamp(v, lo, hi) : lo; };
        size = fix(size, 0.0, 0.9);
        shape = fix(shape, 0.0, 0.9);
        morph_lo = fix(morph_lo, kMorphMin, kMorphMax);
        morph_hi = fix(morph_hi, kMorphMin, kMorphMax);
        if (morph_lo > morph_hi)
        {
            std::swap(morph_lo, morph_hi);
        }
    }

    EditSnapshot randomize_parts(const EditSnapshot& now, const std::vector<RandomPart>& parts,
                                 const std::vector<std::vector<std::string>>& morph_groups, const RandomLimits& in, uint64_t seed)
    {
        RandomLimits limits = in;
        limits.clamp();
        EditSnapshot out = now;
        std::mt19937_64 rng(seed);

        if (limits.parts)
        {
            for (const RandomPart& part : parts)
            {
                const double size = 1.0 + limits.size * unit(rng);
                double axis[3];
                for (double& a : axis)
                {
                    a = std::clamp(size * (1.0 + limits.shape * unit(rng)), 0.1, 10.0);
                }
                for (const std::string& bone : part.bones)
                {
                    const std::string key = fold_case(bone);
                    EditEntry& e = out.bones[key];
                    if (e.bone.empty())
                    {
                        e.bone = bone;
                        e.edit.spread = part.spread;
                    }
                    for (int i = 0; i < 3; ++i)
                    {
                        e.edit.axis[i] = axis[i];
                    }
                    e.edit.clamp();
                    if (e.edit.is_neutral())
                    {
                        out.bones.erase(key);
                    }
                }
            }
        }
        if (limits.morphs)
        {
            for (const auto& group : morph_groups)
            {
                const double t = (unit(rng) + 1.0) * 0.5;
                const double w = clamp_morph(limits.morph_lo + (limits.morph_hi - limits.morph_lo) * t);
                for (const std::string& m : group)
                {
                    out.morphs[fold_case(m)] = MorphEntry{m, w};
                }
            }
        }
        return out;
    }
} // namespace uuepbs
