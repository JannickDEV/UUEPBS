// UUEPBS - which animations Hand Contact works in, picked in the window from what the characters played
//
// The script reports what each character plays (its active montage, else the animation of a single-node mesh;
// uuepbs_hands.lua). Every name seen goes into a history kept per game in the presets folder
// ("_animations.txt"), so the window can list the animations a character is playing now or has played,
// in any game, and the user ticks the ones Hand Contact should (or should not) run in. No names to type.
//
// A picked entry is an animation name (matched whole, ignoring case), a pattern with * (any run of
// characters, e.g. "*Sex*"), or kNormal: no montage at all, i.e. the game's own movement/idle animation.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace uuepbs::anim_picks
{
    constexpr const wchar_t* kChoiceFile = L"_hand_contact_anims.txt";
    constexpr const wchar_t* kHistoryFile = L"_animations.txt";
    // The entry for "no montage playing": walking, standing, idling (the game's animation graph).
    constexpr const char* kNormal = "(normal movement)";
    constexpr size_t kHistoryLimit = 400; // oldest names are dropped beyond this

    enum class Mode
    {
        every,  // Hand Contact in every animation
        only,   // only in the ticked ones
        except, // in all but the ticked ones
    };

    const char* mode_key(Mode m);  // "every", "only", "except"
    Mode mode_from(std::string_view key, Mode fallback);

    struct Choice
    {
        Mode mode{Mode::every};
        std::vector<std::string> names; // as shown (original case), no duplicates
        bool operator==(const Choice&) const = default;
    };

    // True when `entry` (a name, a * pattern or kNormal) covers `playing` ("" = nothing playing). Ignores case.
    bool covers(std::string_view entry, std::string_view playing);
    // Whether Hand Contact runs while `playing` plays ("" = nothing).
    bool wanted(const Choice& c, std::string_view playing);
    bool ticked(const Choice& c, std::string_view name); // the exact entry is in the list (ignoring case)
    void tick(Choice& c, const std::string& name, bool on);

    std::string write(const Choice& c);
    // False when the text holds no "mode=" line.
    bool read(std::string_view text, Choice& c);

    struct Seen
    {
        std::string name;
        std::string who;   // the character it was last seen on (readable name)
        uint32_t count{0}; // times it started
        uint64_t order{0}; // larger = seen more recently
    };

    class History
    {
      public:
        // A start of `name` on `who`. True when the name is new to the history.
        bool record(const std::string& name, const std::string& who);
        // Most recent first.
        std::vector<Seen> entries() const;
        size_t size() const { return m_seen.size(); }
        void clear() { m_seen.clear(); }
        std::string write() const;
        void read(std::string_view text);

      private:
        std::vector<Seen> m_seen;
        uint64_t m_next{1};
    };
} // namespace uuepbs::anim_picks
