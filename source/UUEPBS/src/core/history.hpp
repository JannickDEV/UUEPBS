// UUEPBS - undo / redo for slider edits
//
// The window calls observe() once per frame with the active character's bones and morphs.
// A change becomes one undo step once it has settled: no slider is being dragged or typed
// into (`busy`) and nothing changed for kSettle. So one drag, or a burst of Ctrl+wheel
// nudges, is one step, and so is a preset load or Reset all. Each character (actor key)
// has its own history, so switching characters never undoes someone else's sliders.
#pragma once

#include "morphs.hpp"
#include "sculpt.hpp"

#include <chrono>
#include <cstddef>
#include <deque>
#include <map>
#include <optional>
#include <set>
#include <string>

namespace uuepbs
{
    struct EditSnapshot
    {
        EditBook bones;
        MorphBook morphs;
    };

    // Same edits (neutral bone entries are ignored, as presets do).
    bool same_edits(const EditSnapshot& a, const EditSnapshot& b);

    class EditHistory
    {
      public:
        using Clock = std::chrono::steady_clock;
        static constexpr size_t kLimit = 100; // steps kept per character
        static constexpr std::chrono::milliseconds kSettle{350};

        void observe(const std::string& actor, const EditSnapshot& now, bool busy, Clock::time_point t = Clock::now());

        bool can_undo(const std::string& actor) const;
        bool can_redo(const std::string& actor) const;
        size_t undo_steps(const std::string& actor) const;
        size_t redo_steps(const std::string& actor) const;

        // The state to restore (the caller applies it), or nullopt when there is nothing to undo/redo.
        // `now` is the current state: an edit that has not settled yet counts as a step first.
        std::optional<EditSnapshot> undo(const std::string& actor, const EditSnapshot& now);
        std::optional<EditSnapshot> redo(const std::string& actor, const EditSnapshot& now);

        void forget(const std::string& actor);
        // Drops the history of every character not in `keep` (released or despawned ones:
        // their keys are addresses that a new character could reuse).
        void retain(const std::set<std::string>& keep);

      private:
        struct Track
        {
            EditSnapshot baseline; // last settled state
            EditSnapshot pending;  // latest state seen
            Clock::time_point pending_since{};
            bool started{false};
            std::deque<EditSnapshot> undo;
            std::deque<EditSnapshot> redo;
        };
        static void commit(Track& t);
        std::map<std::string, Track> m_tracks;
    };
} // namespace uuepbs
