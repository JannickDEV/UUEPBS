// Host-side tests for the before/after compare (core/compare.hpp + Registry):
// the window's latched switch, the hold-to-compare hotkey, what ends a compare, and that
// the pose hook shows the game's own pose while comparing without losing any edit.
//
//   g++ -std=c++23 -I../src test_compare.cpp ../src/core/*.cpp -o test_compare && ./test_compare
#include "core/compare.hpp"
#include "core/registry.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace uuepbs;

static int g_fail = 0;
#define CHECK(c)                                                                                                                                     \
    do                                                                                                                                               \
    {                                                                                                                                                \
        if (!(c))                                                                                                                                    \
        {                                                                                                                                            \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c);                                                                                 \
            ++g_fail;                                                                                                                                \
        }                                                                                                                                            \
    } while (0)

static double worst(const std::vector<Xform>& a, const std::vector<Xform>& b)
{
    double w = 0.0;
    for (size_t i = 0; i < a.size(); ++i)
    {
        for (int k = 0; k < 4; ++k)
        {
            w = std::max({w, std::fabs(a[i].rot[k] - b[i].rot[k]), std::fabs(a[i].pos[k] - b[i].pos[k]), std::fabs(a[i].scl[k] - b[i].scl[k])});
        }
    }
    return w;
}

static EditBook scaled(const char* bone, double f)
{
    EditBook b;
    EditEntry e;
    e.bone = bone;
    e.edit.axis[0] = e.edit.axis[1] = e.edit.axis[2] = f;
    b[fold_case(bone)] = e;
    return b;
}

static bool same_book(const EditBook& a, const EditBook& b)
{
    if (a.size() != b.size())
    {
        return false;
    }
    for (const auto& [k, e] : a)
    {
        const auto it = b.find(k);
        if (it == b.end() || std::memcmp(e.edit.axis, it->second.edit.axis, sizeof(e.edit.axis)) != 0)
        {
            return false;
        }
    }
    return true;
}

int main()
{
    // ---- the switch on its own -------------------------------------------------------------
    {
        CompareSwitch c;
        CHECK(!c.showing_original() && !c.latched() && !c.held());
        const uint64_t r0 = c.revision();
        CHECK(c.set_latched(true) && c.showing_original() && c.revision() > r0);
        CHECK(!c.set_latched(true)); // no change, no churn
        const uint64_t r1 = c.revision();
        CHECK(c.revision() == r1);
        CHECK(!c.set_held(true)); // already showing the original: the input changes, what is shown does not
        CHECK(c.revision() > r1 && c.held());
        CHECK(!c.set_latched(false) && c.showing_original()); // the key still holds it
        CHECK(c.set_held(false) && !c.showing_original());
        CHECK(c.toggle() && c.latched());
        CHECK(c.toggle() && !c.latched());
        CHECK(c.set_held(true) && c.showing_original() && !c.latched());
        CHECK(c.set_held(false) && !c.showing_original());
    }

    // ---- through the registry and the pose hook ----------------------------------------------
    const std::vector<std::string> names = {"root", "pelvis", "spine_01", "head"};
    const std::vector<int32_t> parents = {-1, 0, 1, 2};
    std::vector<Xform> original(names.size(), xf::identity());
    for (size_t i = 0; i < original.size(); ++i)
    {
        original[i].pos[2] = 20.0 * static_cast<double>(i); // a straight column, 20 cm apart
        original[i].pos[3] = 0.0;
        original[i].scl[3] = 0.0;
    }
    alignas(16) static unsigned char comp[0x1000] = {};
    std::vector<Xform> bufs[2] = {original, original};
    auto put = [&](size_t off, auto v) { std::memcpy(comp + off, &v, sizeof(v)); };
    put(0x0, (uintptr_t)0x1234);
    put(0xC, (int32_t)77);
    put(0x10, (uintptr_t)0x5555);
    put(0x20, (uintptr_t)0x6666);
    RawArray a0{bufs[0].data(), (int32_t)bufs[0].size(), (int32_t)bufs[0].size()};
    RawArray a1{bufs[1].data(), (int32_t)bufs[1].size(), (int32_t)bufs[1].size()};
    put(0x600, a0);
    put(0x610, a1);
    put(0x644, (int32_t)0);
    put(0x648, (int32_t)1);

    Registry& reg = Registry::instance();
    reg.set_layout(PoseLayout{0x600, 0x648, 16, 96});
    std::string msg;
    CHECK(reg.track((uintptr_t)comp, "CharacterMesh0", "BP_Test_C_0", names, parents, true, msg));

    // The engine writes a fresh pose every frame; the hook then adjusts it.
    auto frame = [&]() {
        bufs[1] = original;
        reg.on_pose_finalized(comp);
    };

    const EditBook book = scaled("spine_01", 1.5);
    reg.replace_edits(book);
    reg.set_morph("BreastSize", 0.7);
    frame();
    const std::vector<Xform> edited = bufs[1];
    CHECK(worst(edited, original) > 1e-3); // the edit shows
    CHECK(reg.applying() && !reg.comparing());

    // Window switch: Before.
    uint64_t m0 = reg.morph_revision();
    reg.set_compare(true);
    CHECK(reg.comparing() && !reg.applying() && reg.enabled());
    CHECK(reg.morph_revision() > m0); // the morphs are re-sent (none while comparing)
    frame();
    CHECK(worst(bufs[1], original) < 1e-9);
    CHECK(same_book(reg.edits(), book) && reg.morphs().size() == 1); // nothing lost
    m0 = reg.morph_revision();
    reg.set_compare(true);
    CHECK(reg.morph_revision() == m0); // no change, no churn

    // After again: the same edit comes back.
    reg.set_compare(false);
    CHECK(!reg.comparing() && reg.morph_revision() > m0);
    frame();
    CHECK(worst(bufs[1], edited) < 1e-9);

    // Hold-to-compare hotkey.
    reg.set_compare_held(true);
    frame();
    CHECK(worst(bufs[1], original) < 1e-9 && !reg.compare().latched());
    reg.set_compare_held(false);
    frame();
    CHECK(worst(bufs[1], edited) < 1e-9);

    // Moving a slider ends a latched compare (you see what you changed) ...
    reg.set_compare(true);
    reg.set_bone("head", scaled("head", 1.2).begin()->second.edit);
    CHECK(!reg.comparing());
    frame();
    const std::vector<Xform> edited2 = bufs[1];
    CHECK(worst(edited2, edited) > 1e-3);
    // ... and so do the other edits made from the window.
    reg.set_compare(true);
    reg.set_morph("Belly", 0.2);
    CHECK(!reg.comparing());
    reg.set_compare(true);
    reg.clear_morph("Belly");
    CHECK(!reg.comparing());
    reg.set_compare(true);
    reg.clear_bone("head");
    CHECK(!reg.comparing());
    reg.set_compare(true);
    reg.replace_edits(book);
    CHECK(!reg.comparing());
    // ... but not the hotkey: the original stays until the key comes up.
    reg.set_compare_held(true);
    reg.set_bone("head", scaled("head", 1.2).begin()->second.edit);
    CHECK(reg.comparing());
    frame();
    CHECK(worst(bufs[1], original) < 1e-9);
    reg.set_compare_held(false);
    frame();
    CHECK(worst(bufs[1], edited2) < 1e-9);

    // Restoring edits for another character (Lua bridge, presets loaded at start) does not end it.
    reg.set_compare(true);
    reg.replace_edits_of("0xDEAD", scaled("pelvis", 1.1));
    CHECK(reg.comparing());
    reg.forget_actor("0xDEAD", true);

    // Compare and the Enabled switch are independent: turning sliders back on while still on
    // Before keeps showing the original.
    reg.set_enabled(false);
    reg.set_compare(false);
    CHECK(!reg.applying());
    reg.set_compare(true);
    reg.set_enabled(true);
    CHECK(!reg.applying());
    frame();
    CHECK(worst(bufs[1], original) < 1e-9);
    reg.set_compare(false);
    CHECK(reg.applying());
    frame();
    CHECK(worst(bufs[1], edited2) < 1e-9);

    reg.clear_edits();
    reg.clear_morphs();
    reg.untrack_all();

    if (g_fail)
    {
        std::printf("%d check(s) failed\n", g_fail);
        return 1;
    }
    std::printf("compare: all checks passed\n");
    return 0;
}
