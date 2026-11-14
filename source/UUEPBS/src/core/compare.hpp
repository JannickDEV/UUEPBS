// UUEPBS - before/after compare
//
// Shows the game's own body for a moment without touching a single slider. Two inputs
// can ask for it, and the original is shown while either one does:
//   * the window's Before / After switch (latched: stays on until it is switched back,
//     a slider is moved, or the window closes)
//   * the hold-to-compare hotkey (CompareKey in config.lua: only while the key is down)
// Nothing here is saved, so a new game session always starts on "After".
//
// Lock-free: the pose hook (game thread) reads it every frame, the window and the
// service thread write it.
#pragma once

#include <atomic>
#include <cstdint>

namespace uuepbs
{
    class CompareSwitch
    {
      public:
        // True while the original (unedited) body should be shown.
        bool showing_original() const { return latched() || held(); }
        bool latched() const { return m_latched.load(std::memory_order_acquire); }
        bool held() const { return m_held.load(std::memory_order_acquire); }

        // Each returns true when showing_original() changed.
        bool set_latched(bool on) { return set(m_latched, on); }
        bool toggle() { return set_latched(!latched()); }
        bool set_held(bool down) { return set(m_held, down); }

        // Moves on every change of either input (the window redraws on it).
        uint64_t revision() const { return m_revision.load(std::memory_order_acquire); }

      private:
        bool set(std::atomic<bool>& flag, bool on)
        {
            const bool before = showing_original();
            if (flag.exchange(on, std::memory_order_acq_rel) == on)
            {
                return false;
            }
            m_revision.fetch_add(1, std::memory_order_acq_rel);
            return showing_original() != before;
        }

        std::atomic<bool> m_latched{false};
        std::atomic<bool> m_held{false};
        std::atomic<uint64_t> m_revision{1};
    };
} // namespace uuepbs
