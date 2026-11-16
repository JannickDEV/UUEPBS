// UUEPBS - Character Scale > Camera: where the game's camera should sit for a character
//
// Height scales the character from the feet, but the game's camera keeps its own pivot,
// so a taller character is framed too low (and a smaller one too high). The Camera
// sliders live in the character's EditBook like the other Character Scale values
// ("@scale:camera_follow", "@scale:camera_height", "@scale:camera_distance"), and the DLL
// sends the result to the Lua script as "cam" lines (bridge.hpp). Scripts/uuepbs_camera.lua
// then moves the controlled character's camera: the spring arm (camera boom) when there
// is one, else the camera component, else the pawn's eye height.
#pragma once

#include "sculpt.hpp"

#include <string>
#include <utility>
#include <vector>

namespace uuepbs
{
    struct CameraWish
    {
        double lift{0.0};     // extra pivot height in centimetres
        double distance{1.0}; // arm length factor
        bool follow{false};   // also scale the pivot height (and the arm) with `height`
        double height{1.0};   // the character's Height slider

        // Something for the script to do. Height alone does nothing unless follow is on.
        bool active() const;
        bool operator==(const CameraWish&) const = default;
    };

    CameraWish camera_wish(const EditBook& book);

    // "cam" line fields after the actor key: lift, distance, follow (0/1), height.
    std::string format_camera_fields(const CameraWish& wish);
} // namespace uuepbs
