// UUEPBS - "next / previous preset" hotkeys: which preset comes next for a character.
//
// Each character keeps its own place in the (alphabetical) preset list, so cycling the player
// and then an NPC does not make either skip ahead. A character that has not cycled yet starts at
// the first preset (next) or the last one (previous). When the preset it was on has been renamed
// or deleted, it continues from where that name would sort.
#pragma once

#include "sculpt.hpp"

#include <map>
#include <string>
#include <vector>

namespace uuepbs
{
    class PresetCycle
    {
      public:
        // `names` as PresetShelf::list() returns them (sorted case-insensitively). step: +1 or -1.
        // Returns the preset to load, or "" when there are none. Does not remember it: the caller
        // calls remember() afterwards, also when the file failed to load, so one broken preset is
        // skipped on the next press instead of blocking the cycle.
        static std::string pick(const std::vector<std::string>& names, const std::string& current, int step)
        {
            if (names.empty())
            {
                return {};
            }
            const int n = static_cast<int>(names.size());
            if (current.empty())
            {
                return step >= 0 ? names.front() : names.back();
            }
            const std::string folded = fold_case(current);
            int at = -1;
            for (int i = 0; i < n; ++i)
            {
                if (fold_case(names[i]) == folded)
                {
                    at = i;
                    break;
                }
            }
            if (at >= 0)
            {
                return names[static_cast<size_t>(((at + (step >= 0 ? 1 : -1)) % n + n) % n)];
            }
            // Gone: first name sorting after it (next) or before it (previous), wrapping around.
            if (step >= 0)
            {
                for (const std::string& s : names)
                {
                    if (fold_case(s) > folded)
                    {
                        return s;
                    }
                }
                return names.front();
            }
            for (int i = n - 1; i >= 0; --i)
            {
                if (fold_case(names[static_cast<size_t>(i)]) < folded)
                {
                    return names[static_cast<size_t>(i)];
                }
            }
            return names.back();
        }

        // Position text for the message, e.g. "3/7".
        static std::string position(const std::vector<std::string>& names, const std::string& name)
        {
            for (size_t i = 0; i < names.size(); ++i)
            {
                if (names[i] == name)
                {
                    return std::to_string(i + 1) + "/" + std::to_string(names.size());
                }
            }
            return {};
        }

        std::string current(const std::string& character) const
        {
            const auto it = m_at.find(character);
            return it == m_at.end() ? std::string() : it->second;
        }
        void remember(const std::string& character, const std::string& preset)
        {
            if (m_at.size() > 256 && !m_at.count(character))
            {
                m_at.clear(); // characters come and go; this only has to hold a session's worth
            }
            m_at[character] = preset;
        }

      private:
        std::map<std::string, std::string> m_at;
    };
} // namespace uuepbs
