// UUEPBS - meshes that already follow the edited main mesh
//
// Some games build a character from several skeletal meshes that each compute a pose of their
// own: a separate head / face / eyes / hair mesh whose animation copies the body's finished pose
// ("Copy Pose From Mesh", or the game's own copy). Such a mesh receives the body's pose *after*
// UUEPBS edited it, so editing it again applies every slider twice: Height 1.2 puts its bones at
// 1.44, Head size 1.89 at 3.57, and the eyes and hair float off the head.
//
// The fix is to tell the two kinds of extra mesh apart from their live pose:
//   - "Own":    its incoming pose matches the body's pose *before* our edits (it animates on
//               its own, like the body): it must be edited too, as before.
//   - "Copies": its incoming pose matches the body's pose *after* our edits: it already shows
//               them and must be left alone.
// Only bones the body's edits actually changed say anything; a mesh is never switched on a
// single frame (FollowLatch), and nothing changes while the answer is unclear.
#pragma once

#include "xform.hpp"

#include <cstdint>
#include <vector>

namespace uuepbs
{
    // One bone shared by the extra mesh and the main mesh, all in component space.
    struct FollowSample
    {
        Xform incoming; // the extra mesh's bone as the engine wrote it this frame (none of our edits)
        Xform source;   // the main mesh's bone before our edits
        Xform result;   // the main mesh's bone after our edits
    };

    enum class FollowVerdict : uint8_t
    {
        Unknown, // the edits change too little, or the pose matches neither
        Own,     // animates on its own: edit it
        Copies,  // already carries the main mesh's edits: leave it alone
    };

    const char* follow_label(FollowVerdict v);

    // Distance between two transforms: centimetres, plus scale and rotation differences weighted
    // so that a 10% size change on a bone counts like a couple of centimetres.
    double follow_distance(const Xform& a, const Xform& b);

    FollowVerdict judge_follow(const std::vector<FollowSample>& samples);

    // Turns per-frame verdicts into a stable state: it changes after `kFrames` agreeing frames.
    class FollowLatch
    {
      public:
        static constexpr int kFrames = 3;

        // Feeds one frame's verdict; returns the (possibly new) state.
        FollowVerdict feed(FollowVerdict v);
        FollowVerdict state() const { return m_state; }
        bool copies() const { return m_state == FollowVerdict::Copies; }
        void reset()
        {
            m_state = FollowVerdict::Unknown;
            m_pending = FollowVerdict::Unknown;
            m_streak = 0;
        }

      private:
        FollowVerdict m_state{FollowVerdict::Unknown};
        FollowVerdict m_pending{FollowVerdict::Unknown};
        int m_streak{0};
    };
} // namespace uuepbs
