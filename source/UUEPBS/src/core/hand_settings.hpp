// UUEPBS - Hand Contact settings chosen in the window (Character Scale > Animation)
//
// Remembered in "<presets folder>\_hand_contact.txt" as key=value lines and preferred over config.lua's
// HandContact* values while the file exists ("Use config.lua values" in the window deletes it).
#pragma once

#include "sculpt.hpp"

#include <string>
#include <string_view>

namespace uuepbs::hand_settings
{
    constexpr const wchar_t* kFileName = L"_hand_contact.txt";

    std::string write(const HandOptions& o);
    // Starts from `base` (the config values) and takes every known key found in `text`.
    // False when the text holds none of them.
    bool read(std::string_view text, HandOptions& o);
    // Every value within the ranges the window offers.
    HandOptions clamp(HandOptions o);
} // namespace uuepbs::hand_settings
