#include "hand_settings.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace uuepbs::hand_settings
{
    namespace
    {
        double fit(double v, double lo, double hi, double fallback)
        {
            return std::isfinite(v) ? std::min(hi, std::max(lo, v)) : fallback;
        }
    } // namespace

    HandOptions clamp(HandOptions o)
    {
        const HandOptions d{};
        o.strength = fit(o.strength, 0.0, 1.0, d.strength);
        o.full_within = fit(o.full_within, 0.05, 1.0, d.full_within);
        o.fade_out = fit(o.fade_out, 0.1, 1.5, d.fade_out);
        if (o.fade_out < o.full_within + 0.05)
        {
            o.fade_out = o.full_within + 0.05;
        }
        o.shoulder_turn = fit(o.shoulder_turn, 0.0, 60.0, d.shoulder_turn);
        o.stretch = fit(o.stretch, 1.0, 1.5, d.stretch);
        return o;
    }

    std::string write(const HandOptions& o)
    {
        char buf[400];
        std::snprintf(buf, sizeof buf,
                      "# UUEPBS Hand Contact, set in the window (Character Scale > Animation). Delete this file to use config.lua again.\n"
                      "enabled=%d\nstrength=%.3f\nnear=%.3f\nfar=%.3f\nshoulder=%.1f\nstretch=%.3f\nfingers=%d\nfeet=%d\n",
                      o.enabled ? 1 : 0, o.strength, o.full_within, o.fade_out, o.shoulder_turn, o.stretch, o.fingers ? 1 : 0, o.feet ? 1 : 0);
        return buf;
    }

    bool read(std::string_view text, HandOptions& o)
    {
        bool any = false;
        size_t at = 0;
        while (at < text.size())
        {
            size_t end = text.find('\n', at);
            if (end == std::string_view::npos)
            {
                end = text.size();
            }
            std::string line(text.substr(at, end - at));
            at = end + 1;
            while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
            {
                line.pop_back();
            }
            const size_t eq = line.find('=');
            if (line.empty() || line[0] == '#' || eq == std::string::npos)
            {
                continue;
            }
            const std::string key = fold_case(line.substr(0, eq));
            const std::string val = line.substr(eq + 1);
            char* stop = nullptr;
            const double v = std::strtod(val.c_str(), &stop);
            if (stop == val.c_str())
            {
                continue;
            }
            if (key == "enabled")
            {
                o.enabled = v != 0.0;
            }
            else if (key == "strength")
            {
                o.strength = v;
            }
            else if (key == "near")
            {
                o.full_within = v;
            }
            else if (key == "far")
            {
                o.fade_out = v;
            }
            else if (key == "shoulder")
            {
                o.shoulder_turn = v;
            }
            else if (key == "stretch")
            {
                o.stretch = v;
            }
            else if (key == "fingers")
            {
                o.fingers = v != 0.0;
            }
            else if (key == "feet")
            {
                o.feet = v != 0.0;
            }
            else
            {
                continue;
            }
            any = true;
        }
        o = clamp(o);
        return any;
    }
} // namespace uuepbs::hand_settings
