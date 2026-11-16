// Character Scale > Camera (core/camera.*): the sliders, what the DLL sends to Lua, presets and share codes.
//   g++ -std=c++23 -I../src test_camera.cpp ../src/core/*.cpp -o test_camera && ./test_camera
#include "core/bridge.hpp"
#include "core/camera.hpp"
#include "core/presets.hpp"
#include "core/proportions.hpp"
#include "core/share_code.hpp"

#include <cmath>
#include <cstdio>
#include <string>

using namespace uuepbs;

static int g_fail = 0;
#define CHECK(cond)                                                                                                                        \
    do                                                                                                                                     \
    {                                                                                                                                      \
        if (!(cond))                                                                                                                       \
        {                                                                                                                                  \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                                                                    \
            ++g_fail;                                                                                                                      \
        }                                                                                                                                  \
    } while (0)

static bool near(double a, double b) { return std::fabs(a - b) < 1e-4; }

static void test_sliders()
{
    const ScaleSlider* follow = find_scale_slider("camera_follow");
    const ScaleSlider* lift = find_scale_slider("camera_height");
    const ScaleSlider* dist = find_scale_slider("camera_distance");
    CHECK(follow && lift && dist);
    if (!follow || !lift || !dist)
    {
        return;
    }
    CHECK(std::string(follow->section) == "Camera" && !follow->body && follow->offset() && follow->neutral() == 0.0);
    CHECK(lift->offset() && lift->neutral() == 0.0 && lift->lo <= -100.0 && lift->hi >= 100.0);
    CHECK(!dist->offset() && dist->neutral() == 1.0 && dist->lo < 1.0 && dist->hi > 1.0);
    CHECK(is_camera_axes(follow->axes) && is_camera_axes(lift->axes) && is_camera_axes(dist->axes) && !is_camera_axes(ScaleAxes::Uniform));

    EditBook book;
    CHECK(!camera_wish(book).active());
    set_scale_value(book, "height", 1.2);
    CHECK(!camera_wish(book).active()); // Height alone leaves the camera alone
    set_scale_value(book, "camera_follow", 1.0);
    CameraWish w = camera_wish(book);
    CHECK(w.active() && w.follow && near(w.height, 1.2) && near(w.lift, 0.0) && near(w.distance, 1.0));
    set_scale_value(book, "camera_height", -35.0);
    set_scale_value(book, "camera_distance", 1.5);
    w = camera_wish(book);
    CHECK(near(w.lift, -35.0) && near(w.distance, 1.5));
    set_scale_value(book, "camera_height", 900.0); // clamped to the slider
    CHECK(near(camera_wish(book).lift, lift->hi));
    // back to neutral clears the entries
    set_scale_value(book, "camera_height", 0.0);
    set_scale_value(book, "camera_distance", 1.0);
    set_scale_value(book, "camera_follow", 0.0);
    CHECK(book.count(scale_key("camera_height")) == 0 && book.count(scale_key("camera_follow")) == 0 && book.count(scale_key("camera_distance")) == 0);
    // Follow with Height at 100% is nothing to do
    EditBook still;
    set_scale_value(still, "camera_follow", 1.0);
    CHECK(!camera_wish(still).active());
}

// The camera sliders never reach the pose: no bone edits and no height factor.
static void test_no_bones()
{
    EditBook book;
    set_scale_value(book, "camera_height", 40.0);
    set_scale_value(book, "camera_distance", 2.0);
    set_scale_value(book, "camera_follow", 1.0);
    CHECK(has_proportions(book) && bone_entry_count(book) == 0);
    BodyMap map;
    double world = 0.0;
    const EditBook bound = apply_proportions(book, map, world);
    CHECK(bound.empty() && near(world, 1.0));
    for (const char* id : {"camera_follow", "camera_height", "camera_distance"})
    {
        CHECK(scale_slider_bones(*find_scale_slider(id), map).empty());
    }
    set_scale_value(book, "height", 1.3);
    apply_proportions(book, map, world);
    CHECK(near(world, 1.3));
}

static void test_bridge()
{
    bridge::DllState d;
    CameraWish w;
    w.lift = -12.5;
    w.distance = 1.25;
    w.follow = true;
    w.height = 1.2;
    d.cameras = {{"player", w}, {"7FF6AB", CameraWish{30.0, 1.0, false, 1.0}}};
    const std::string out = bridge::format_dll_state(d);
    CHECK(out.find("cam\tplayer\t-12.50\t1.2500\t1\t1.2000\n") != std::string::npos);
    CHECK(out.find("cam\t7FF6AB\t30.00\t1.0000\t0\t1.0000\n") != std::string::npos);
    CHECK(out.ends_with("#end\n"));
    bridge::DllState none;
    CHECK(bridge::format_dll_state(none).find("cam\t") == std::string::npos);
}

// Saved like the other Character Scale values: presets and share codes carry them.
static void test_presets()
{
    EditBook book;
    set_scale_value(book, "height", 1.15);
    set_scale_value(book, "camera_follow", 1.0);
    set_scale_value(book, "camera_height", 25.0);
    set_scale_value(book, "camera_distance", 0.8);
    const std::string text = PresetShelf::serialize(book);
    CHECK(text.find("\"camera_height\": 25") != std::string::npos);
    EditBook back;
    std::string msg;
    CHECK(PresetShelf::parse(text, back, msg));
    CHECK(camera_wish(back) == camera_wish(book));
    share::Decoded d;
    CHECK(share::decode(share::encode(book, nullptr), d, msg));
    CHECK(camera_wish(d.bones) == camera_wish(book));
}

int main()
{
    test_sliders();
    test_no_bones();
    test_bridge();
    test_presets();
    if (g_fail == 0)
    {
        std::printf("camera tests passed\n");
    }
    return g_fail == 0 ? 0 : 1;
}
