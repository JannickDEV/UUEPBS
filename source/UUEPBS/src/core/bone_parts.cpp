#include "bone_parts.hpp"

#include "morph_sort.hpp" // morph_words
#include "sculpt.hpp"     // fold_case

#include <algorithm>
#include <cctype>

namespace uuepbs
{
    const std::vector<BonePart>& bone_parts()
    {
        // Order matters: a bone joins the first part that matches, so the tip of a shaft
        // (penis_tip, cockHead) goes to the glans before "penis*" can take it.
        static const std::vector<BonePart> parts = {
            {"glans", "glans",
             {"glans#", "*glans*", "penistip#", "cocktip#", "dicktip#", "penishead#", "cockhead#", "dickhead#", "*penistip*", "*cocktip*",
              "*penishead*", "*cockhead*"},
             {}},
            {"penis", "penis shaft",
             {"penis#", "penis*", "*penis#", "*penis*", "cock#", "cock*", "*cock#", "dick#", "dick*", "*dick#", "phallus#", "*phallus*",
              "shaft#", "penisshaft#", "genitalshaft#", "foreskin#", "*foreskin*"},
             {"glans", "tip", "head", "ball", "testic", "scrot", "pit", "tail"}},
            {"testicles", "testicles",
             {"testicle#", "testicles#", "*testic*", "testes#", "testis#", "scrotum#", "*scrot*", "balls#", "*balls*", "ballsack#",
              "*ballsack*"},
             {"eye", "foot", "toe"}},
            {"vulva", "vulva",
             {"vagina#", "*vagina*", "vulva#", "*vulva*", "labia#", "*labia*", "labium#", "*labium*", "pussy#", "*pussy*", "clit#",
              "*clitoris*"},
             {}},
        };
        return parts;
    }

    const BonePart* find_bone_part(std::string_view id)
    {
        const std::string folded = fold_case(id);
        for (const BonePart& p : bone_parts())
        {
            if (folded == p.id)
            {
                return &p;
            }
        }
        return nullptr;
    }

    std::vector<PartBones> resolve_bone_parts(const std::vector<std::string>& names, const StemRules* rules, const std::vector<uint8_t>* taken)
    {
        std::vector<PartBones> out;
        std::vector<std::string> stems(names.size());
        for (size_t i = 0; i < names.size(); ++i)
        {
            stems[i] = bone_stem(names[i], rules);
        }
        std::vector<uint8_t> used(names.size(), 0);
        if (taken && taken->size() == names.size())
        {
            used = *taken;
        }
        for (const BonePart& part : bone_parts())
        {
            PartBones pb;
            pb.id = part.id;
            for (size_t i = 0; i < names.size(); ++i)
            {
                const std::string& s = stems[i];
                if (used[i] || s.empty())
                {
                    continue;
                }
                const bool excluded = std::any_of(part.exclude.begin(), part.exclude.end(),
                                                  [&](const char* w) { return s.find(w) != std::string::npos; });
                if (excluded)
                {
                    continue;
                }
                const bool hit = std::any_of(part.patterns.begin(), part.patterns.end(), [&](const char* p) { return glob_match(p, s); });
                if (hit)
                {
                    used[i] = 1;
                    pb.bones.push_back(names[i]);
                }
            }
            if (!pb.bones.empty())
            {
                out.push_back(std::move(pb));
            }
        }
        return out;
    }

    namespace
    {
        bool in(const std::vector<const char*>& list, const std::string& w)
        {
            for (const char* x : list)
            {
                // "ball" also matches "balls", "testicle" matches "testicles".
                if (w == x || w == std::string(x) + "s")
                {
                    return true;
                }
            }
            return false;
        }

        bool noise(const std::string& w)
        {
            static const std::vector<const char*> words = {"m", "gen", "genital", "genitals", "morph", "shape", "l", "r", "left", "right"};
            return w.empty() || std::all_of(w.begin(), w.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; }) ||
                   in(words, w);
        }
    } // namespace

    bool part_covered_by_morph(std::string_view morph, const std::vector<const char*>& part_words,
                               const std::vector<const char*>& dimension_words)
    {
        static const std::vector<const char*> size_words = {"size", "enlarge", "enlarged", "large", "larger", "big", "bigger", "small",
                                                            "smaller", "reduce", "scale", "huge", "tiny", "grow", "shrink"};
        bool names_part = false;
        std::vector<std::string> rest;
        for (const std::string& w : morph_words(morph))
        {
            if (in(part_words, w))
            {
                names_part = true;
            }
            else if (!noise(w))
            {
                rest.push_back(w);
            }
        }
        if (!names_part)
        {
            return false;
        }
        if (dimension_words.empty())
        {
            return std::all_of(rest.begin(), rest.end(), [](const std::string& w) { return in(size_words, w); });
        }
        return std::any_of(rest.begin(), rest.end(), [&](const std::string& w) { return in(dimension_words, w); });
    }
} // namespace uuepbs
