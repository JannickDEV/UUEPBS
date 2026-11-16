#include "follow.hpp"

#include <algorithm>
#include <cmath>

namespace uuepbs
{
    namespace
    {
        constexpr double kScaleWeight = 20.0;    // 0.1 scale difference = 2 cm
        constexpr double kRotationWeight = 50.0; // (1 - |dot|) is tiny for small angles
        // A bone whose edit moves it less than this says nothing (animation noise between the
        // frame the copy was taken and the frame we compare against is of this order).
        constexpr double kMinGap = 0.05;
        // Total change our edits make, summed over the bones, below which nothing is decided.
        constexpr double kMinTotalGap = 0.5;
        // How close the incoming pose must be to one side, and how far from the other, as a
        // fraction of the total change our edits make.
        constexpr double kNear = 0.3;
        constexpr double kFar = 0.6;
    } // namespace

    const char* follow_label(FollowVerdict v)
    {
        switch (v)
        {
        case FollowVerdict::Own:
            return "own pose";
        case FollowVerdict::Copies:
            return "follows the main mesh";
        case FollowVerdict::Unknown:
            break;
        }
        return "not known yet";
    }

    double follow_distance(const Xform& a, const Xform& b)
    {
        double d = 0.0;
        double p = 0.0;
        for (int i = 0; i < 3; ++i)
        {
            const double dp = a.pos[i] - b.pos[i];
            p += dp * dp;
            d += kScaleWeight * std::fabs(a.scl[i] - b.scl[i]);
        }
        d += std::sqrt(p);
        double dot = 0.0;
        for (int i = 0; i < 4; ++i)
        {
            dot += a.rot[i] * b.rot[i];
        }
        d += kRotationWeight * std::max(0.0, 1.0 - std::fabs(dot));
        return std::isfinite(d) ? d : 1.0e30;
    }

    FollowVerdict judge_follow(const std::vector<FollowSample>& samples)
    {
        double gap = 0.0;
        double to_source = 0.0;
        double to_result = 0.0;
        for (const FollowSample& s : samples)
        {
            if (!xf::finite(s.incoming) || !xf::finite(s.source) || !xf::finite(s.result))
            {
                continue;
            }
            const double g = follow_distance(s.source, s.result);
            if (g < kMinGap)
            {
                continue;
            }
            gap += g;
            to_source += follow_distance(s.incoming, s.source);
            to_result += follow_distance(s.incoming, s.result);
        }
        if (gap < kMinTotalGap)
        {
            return FollowVerdict::Unknown;
        }
        if (to_result <= kNear * gap && to_source >= kFar * gap)
        {
            return FollowVerdict::Copies;
        }
        if (to_source <= kNear * gap && to_result >= kFar * gap)
        {
            return FollowVerdict::Own;
        }
        return FollowVerdict::Unknown;
    }

    FollowVerdict FollowLatch::feed(FollowVerdict v)
    {
        if (v == FollowVerdict::Unknown || v == m_state)
        {
            m_pending = FollowVerdict::Unknown;
            m_streak = 0;
            return m_state;
        }
        if (v != m_pending)
        {
            m_pending = v;
            m_streak = 0;
        }
        if (++m_streak >= kFrames)
        {
            m_state = v;
            m_pending = FollowVerdict::Unknown;
            m_streak = 0;
        }
        return m_state;
    }
} // namespace uuepbs
