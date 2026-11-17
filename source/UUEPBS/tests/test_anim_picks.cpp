// UUEPBS - Hand Contact animation picks and the animation history (core/anim_picks.*), and how the
// Registry uses them to decide whether Hand Contact runs.
//   g++ -std=c++23 -Isrc tests/test_anim_picks.cpp src/core/*.cpp -o test_anim_picks
#include "core/anim_picks.hpp"

#include <cstdio>
#include <string>

using namespace uuepbs::anim_picks;

static int g_fail = 0;
#define CHECK(c)                                                                                                                                     \
    do                                                                                                                                               \
    {                                                                                                                                                \
        if (!(c))                                                                                                                                    \
        {                                                                                                                                            \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                                                                 \
            ++g_fail;                                                                                                                                \
        }                                                                                                                                            \
    } while (0)

int main()
{
    // matching: whole names ignore case, * patterns, the "no montage" entry
    CHECK(covers("AM_Emote_Wave", "am_emote_wave"));
    CHECK(!covers("AM_Emote", "AM_Emote_Wave"));
    CHECK(covers("*emote*", "AM_Emote_Wave"));
    CHECK(covers("AM_*_Wave", "AM_Emote_Wave"));
    CHECK(!covers("AM_*_Wave", "AM_Emote_Waves"));
    CHECK(covers("*", "anything"));
    CHECK(!covers("*", ""));
    CHECK(covers(kNormal, ""));
    CHECK(!covers(kNormal, "AM_Emote_Wave"));
    CHECK(!covers("", ""));

    Choice every;
    CHECK(wanted(every, "") && wanted(every, "X"));

    Choice only{Mode::only, {"A_Sex_Loop", "*Hug*"}};
    CHECK(wanted(only, "a_sex_loop"));
    CHECK(wanted(only, "AM_Cutscene_Hug_01"));
    CHECK(!wanted(only, "A_Sex_Loop_End"));
    CHECK(!wanted(only, "")); // normal movement not ticked

    Choice except{Mode::except, {kNormal, "A_Idle_ArmsCrossed"}};
    CHECK(!wanted(except, ""));
    CHECK(!wanted(except, "a_idle_armscrossed"));
    CHECK(wanted(except, "AM_Emote_Wave"));

    // ticking: no duplicates, ignores case, unticks
    Choice c;
    tick(c, "AM_Wave", true);
    tick(c, "am_wave", true);
    CHECK(c.names.size() == 1);
    CHECK(ticked(c, "AM_WAVE"));
    tick(c, " AM_Wave ", false);
    CHECK(c.names.empty());
    tick(c, "  ", true);
    CHECK(c.names.empty());

    // file round trip (tabs and line breaks in names cannot break it)
    Choice saved{Mode::except, {kNormal, "AM_A", "*Hug*", "bad\tname"}};
    Choice back;
    CHECK(read(write(saved), back));
    CHECK(back.mode == Mode::except);
    CHECK(back.names.size() == 4);
    CHECK(back.names[3] == "bad name");
    Choice untouched{Mode::only, {"x"}};
    CHECK(!read("# nothing here\nanim=y\n", untouched));
    CHECK(untouched.mode == Mode::only && untouched.names.size() == 1);
    CHECK(mode_from("ONLY", Mode::every) == Mode::only);
    CHECK(mode_from("nonsense", Mode::except) == Mode::except);

    // history: newest first, counts starts, ignores empty/None, keeps the last character
    History h;
    CHECK(h.record("AM_A", "Roku"));
    CHECK(h.record("AM_B", "Anca"));
    CHECK(!h.record("am_a", "Vendor"));
    CHECK(!h.record("", "x"));
    CHECK(!h.record("None", "x"));
    auto e = h.entries();
    CHECK(e.size() == 2);
    CHECK(e[0].name == "AM_A" && e[0].count == 2 && e[0].who == "Vendor");
    CHECK(e[1].name == "AM_B" && e[1].count == 1);

    History h2;
    h2.read(h.write());
    auto e2 = h2.entries();
    CHECK(e2.size() == 2);
    CHECK(e2[0].name == "AM_A" && e2[0].count == 2 && e2[0].who == "Vendor");
    CHECK(e2[1].name == "AM_B" && e2[1].who == "Anca");
    // order continues after a read
    h2.record("AM_B", "Anca");
    CHECK(h2.entries()[0].name == "AM_B");

    // limit: the oldest go
    History big;
    for (size_t i = 0; i < kHistoryLimit + 25; ++i)
    {
        big.record("Anim_" + std::to_string(i), "x");
    }
    CHECK(big.size() == kHistoryLimit);
    CHECK(big.entries().back().name == "Anim_25");
    CHECK(big.entries().front().name == "Anim_" + std::to_string(kHistoryLimit + 24));

    // a broken file does not throw and keeps what it can
    History junk;
    junk.read("\n\n# c\nonly_name\nname2\t\tnotanumber\n\t\t\n");
    CHECK(junk.size() == 2);

    if (g_fail == 0)
    {
        std::printf("test_anim_picks: all passed\n");
    }
    return g_fail == 0 ? 0 : 1;
}
