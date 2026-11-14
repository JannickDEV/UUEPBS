#include "looks.hpp"
#include "sculpt.hpp" // fold_case

#include <algorithm>
#include <cmath>

namespace uuepbs::looks
{
    namespace
    {
        constexpr float kEpsilon = 1e-4f;

        struct Word
        {
            const char* id;
            const char* below; // value under neutral
            const char* above; // value over neutral
        };
        constexpr Word kWords[] = {
            {"skin.tone", "tan", "pale"},
            {"skin.sweat", "dry", "sweat"},
            {"hair.hue", "hue", "hue"},
            {"hair.saturation", "muted", "vivid"},
            {"hair.lightness", "darker", "lighter"},
        };
    } // namespace

    const std::vector<Category>& categories()
    {
        static const std::vector<Category> list = {
            {"Skin", "Every skin slot of this outfit (body, head, arms...), so the seams match. Mouths, eyes and clothes stay as they are."},
            {"Hair", "Hair, brows and lashes of this outfit. The hair's own colour is shifted, so strands and roots keep their detail."},
        };
        return list;
    }

    const std::vector<Control>& controls()
    {
        static const std::vector<Control> list = {
            {"skin.tone", "Skin", "Skin tone", "Below 0 tans the skin, above 0 makes it paler.\nScales the skin material's own tint, so its texture stays.", -2.0f,
             2.0f, 0.0f},
            {"skin.sweat", "Skin", "Sweat", "A wet sheen: lowers the skin's roughness and raises its wetness / specular\nwhere the material has those parameters.",
             0.0f, 2.0f, 0.0f},
            {"hair.hue", "Hair", "Hue", "Turns the hair colour around the colour wheel (1 = half way, 180 degrees).", -1.0f, 1.0f, 0.0f},
            {"hair.saturation", "Hair", "Saturation", "Below 0 greys the hair out, above 0 makes its colour stronger.", -2.0f, 2.0f, 0.0f},
            {"hair.lightness", "Hair", "Lightness", "Below 0 darkens the hair, above 0 lightens it\n(MetaHuman-style hair: less or more melanin).", -2.0f,
             2.0f, 0.0f},
        };
        return list;
    }

    const Control* find(std::string_view id)
    {
        const std::string folded = fold_case(id);
        for (const Control& c : controls())
        {
            if (folded == c.id)
            {
                return &c;
            }
        }
        return nullptr;
    }

    std::string key_of(std::string_view id)
    {
        return std::string(kPrefix) + fold_case(id);
    }

    bool is_look(std::string_view name)
    {
        if (name.size() <= kPrefix.size())
        {
            return false;
        }
        for (size_t i = 0; i < kPrefix.size(); ++i)
        {
            char ch = name[i];
            if (ch >= 'A' && ch <= 'Z')
            {
                ch = static_cast<char>(ch - 'A' + 'a');
            }
            if (ch != kPrefix[i])
            {
                return false;
            }
        }
        return true;
    }

    std::string id_of(std::string_view name)
    {
        return is_look(name) ? fold_case(name.substr(kPrefix.size())) : std::string();
    }

    float value_of(const MorphBook& book, const Control& c)
    {
        const auto it = book.find(key_of(c.id));
        if (it == book.end() || !std::isfinite(it->second.weight))
        {
            return c.neutral;
        }
        return static_cast<float>(it->second.weight);
    }

    bool is_set(const MorphBook& book, const Control& c)
    {
        return std::fabs(value_of(book, c) - c.neutral) > kEpsilon;
    }

    void put(MorphBook& book, const Control& c, float v)
    {
        const std::string key = key_of(c.id);
        v = std::clamp(v, c.lo, c.hi);
        if (!std::isfinite(v) || std::fabs(v - c.neutral) <= kEpsilon)
        {
            book.erase(key);
            return;
        }
        book[key] = MorphEntry{key, clamp_morph(v)};
    }

    size_t count(const MorphBook& book)
    {
        return static_cast<size_t>(std::count_if(book.begin(), book.end(), [](const auto& kv) { return is_look(kv.first); }));
    }

    MorphBook only_looks(const MorphBook& book)
    {
        MorphBook out;
        for (const auto& [key, m] : book)
        {
            if (is_look(key))
            {
                out.emplace(key, m);
            }
        }
        return out;
    }

    MorphBook without_looks(const MorphBook& book)
    {
        MorphBook out;
        for (const auto& [key, m] : book)
        {
            if (!is_look(key))
            {
                out.emplace(key, m);
            }
        }
        return out;
    }

    void keep_when_absent(MorphBook& loaded, const MorphBook& current)
    {
        if (count(loaded) > 0)
        {
            return;
        }
        for (const auto& [key, m] : current)
        {
            if (is_look(key))
            {
                loaded.emplace(key, m);
            }
        }
    }

    std::string summary(const MorphBook& book, std::string_view category)
    {
        std::string out;
        for (const Control& c : controls())
        {
            if (category != c.category || !is_set(book, c))
            {
                continue;
            }
            const bool above = value_of(book, c) > c.neutral;
            for (const Word& w : kWords)
            {
                if (std::string_view(w.id) == c.id)
                {
                    const std::string word = above ? w.above : w.below;
                    if (out.find(word) == std::string::npos)
                    {
                        out += (out.empty() ? "" : " + ") + word;
                    }
                }
            }
        }
        return out;
    }

    size_t changed(const MorphBook& book, std::string_view category)
    {
        size_t n = 0;
        for (const Control& c : controls())
        {
            if (category == c.category && is_set(book, c))
            {
                ++n;
            }
        }
        return n;
    }

    Report& Report::instance()
    {
        static Report r;
        return r;
    }

    void Report::set(std::map<std::string, std::string> lines)
    {
        std::lock_guard lock(m_lock);
        if (lines != m_lines)
        {
            m_lines = std::move(lines);
            ++m_revision;
        }
    }

    std::string Report::get(const std::string& actor) const
    {
        std::lock_guard lock(m_lock);
        const auto it = m_lines.find(actor);
        return it == m_lines.end() ? std::string() : it->second;
    }

    uint64_t Report::revision() const
    {
        std::lock_guard lock(m_lock);
        return m_revision;
    }
} // namespace uuepbs::looks
