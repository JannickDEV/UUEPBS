#include "camera.hpp"

#include "proportions.hpp"

#include <cmath>
#include <cstdio>

namespace uuepbs
{
    bool CameraWish::active() const
    {
        return std::fabs(lift) >= 0.05 || std::fabs(distance - 1.0) >= kScaleNeutralEps ||
               (follow && std::fabs(height - 1.0) >= kScaleNeutralEps);
    }

    CameraWish camera_wish(const EditBook& book)
    {
        CameraWish w;
        w.lift = scale_value(book, "camera_height");
        w.distance = scale_value(book, "camera_distance");
        w.follow = scale_value(book, "camera_follow") >= 0.5;
        w.height = scale_value(book, "height");
        return w;
    }

    std::string format_camera_fields(const CameraWish& wish)
    {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "%.2f\t%.4f\t%d\t%.4f", wish.lift, wish.distance, wish.follow ? 1 : 0, wish.height);
        return buf;
    }
} // namespace uuepbs
