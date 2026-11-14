// UUEPBS - Looks (core/looks.hpp): keys, summaries, presets ("looks" object), bridge report lines.
//   g++ -std=c++23 -I../src test_looks.cpp ../src/core/*.cpp -o test_looks && ./test_looks
#include "core/bridge.hpp"
#include "core/looks.hpp"
#include "core/presets.hpp"
#include "core/registry.hpp"

#include <cmath>
#include <cstdio>
#include <string>

using namespace uuepbs;

static int g_fail = 0;
#define CHECK(c)                                                                                                                                                \
    do                                                                                                                                                          \
    {                                                                                                                                                           \
        if (!(c))                                                                                                                                               \
        {                                                                                                                                                       \
            std::printf("FAIL line %d: %s\n", __LINE__, #c);                                                                                                  \
            ++g_fail;                                                                                                                                           \
        }                                                                                                                                                       \
    } while (0)

int main()
{
    // keys
    CHECK(looks::key_of("Skin.Tone") == "look:skin.tone");
    CHECK(looks::is_look("LOOK:skin.tone"));
    CHECK(!looks::is_look("look:"));
    CHECK(!looks::is_look("LookAtTarget"));
    CHECK(looks::id_of("Look:Hair.Hue") == "hair.hue");
    CHECK(looks::find("skin.sweat") && looks::find("SKIN.SWEAT") == looks::find("skin.sweat"));
    CHECK(looks::find("nope") == nullptr);
    for (const looks::Control& c : looks::controls())
    {
        CHECK(c.lo >= kMorphMin && c.hi <= kMorphMax && c.lo <= c.neutral && c.neutral <= c.hi); // rides in the morph book
    }

    // put / value / summary
    MorphBook book;
    const looks::Control& tone = *looks::find("skin.tone");
    const looks::Control& sweat = *looks::find("skin.sweat");
    const looks::Control& light = *looks::find("hair.lightness");
    looks::put(book, tone, -2.0f);
    looks::put(book, sweat, 9.0f); // clamped to the control's range
    CHECK(std::fabs(looks::value_of(book, sweat) - 2.0f) < 1e-6f);
    CHECK(looks::summary(book, "Skin") == "tan + sweat");
    CHECK(looks::changed(book, "Skin") == 2 && looks::changed(book, "Hair") == 0);
    looks::put(book, light, 1.0f);
    CHECK(looks::summary(book, "Hair") == "lighter");
    looks::put(book, light, 0.0f); // neutral removes it
    CHECK(book.count("look:hair.lightness") == 0);
    CHECK(!looks::is_set(book, light));
    book["bellysize"] = MorphEntry{"BellySize", 0.5};
    CHECK(looks::count(book) == 2);
    CHECK(looks::only_looks(book).size() == 2 && looks::without_looks(book).size() == 1);

    // presets: looks get their own object, morphs stay as they were; round trip
    const std::string text = PresetShelf::serialize(EditBook{}, &book);
    CHECK(text.find("\"looks\": {") != std::string::npos);
    CHECK(text.find("\"skin.tone\": -2") != std::string::npos);
    CHECK(text.find("look:") == std::string::npos);
    CHECK(text.find("\"BellySize\": 0.5") != std::string::npos);
    EditBook bones;
    MorphBook back;
    std::string msg;
    CHECK(PresetShelf::parse(text, bones, msg, &back));
    CHECK(back.size() == 3 && looks::count(back) == 2);
    CHECK(std::fabs(looks::value_of(back, tone) + 2.0f) < 1e-6f);
    CHECK(msg.find("2 look(s)") != std::string::npos);

    // a looks-only book still writes a version 2 preset (older builds read it, ignoring the looks)
    const MorphBook look_book = looks::only_looks(book);
    const std::string only = PresetShelf::serialize(EditBook{}, &look_book);
    CHECK(only.find("\"version\": 2") != std::string::npos && only.find("\"morphs\"") == std::string::npos);
    CHECK(PresetShelf::parse(only, bones, msg, &back) && looks::count(back) == 2);
    // a preset with only "looks" is valid; an older preset without looks has none
    CHECK(PresetShelf::parse(R"({ "looks": { "hair.hue": 0.5 } })", bones, msg, &back) && back.count("look:hair.hue") == 1);
    CHECK(PresetShelf::parse(R"({ "bones": {} })", bones, msg, &back) && back.empty());

    // loading a preset without looks keeps the current looks; one with looks replaces them
    MorphBook loaded;
    looks::keep_when_absent(loaded, book);
    CHECK(looks::count(loaded) == 2 && loaded.count("bellysize") == 0);
    MorphBook other;
    looks::put(other, light, -1.0f);
    looks::keep_when_absent(other, book);
    CHECK(looks::count(other) == 1);

    // registry: looks are per character and travel like morphs
    Registry& reg = Registry::instance();
    reg.set_morph("look:skin.tone", -1.0);
    CHECK(looks::count(reg.morphs()) == 1);
    CHECK(reg.all_morphs().at(kPlayerActor).count("look:skin.tone") == 1);

    // bridge: "looks" report lines from Lua
    const std::string lua = "UBS1 s 3\nsetup\tversion=2.7\nlooks\tplayer\t2 meshes: 4 slots changed (tint)\nlooks\tC0FF\tno skin slots found\n#end 3\n";
    std::string why;
    auto state = bridge::parse_lua_state(lua, &why);
    CHECK(state.has_value());
    if (state)
    {
        CHECK(state->looks.size() == 2 && state->looks["player"] == "2 meshes: 4 slots changed (tint)");
        looks::Report::instance().set(state->looks);
        CHECK(looks::Report::instance().get("C0FF") == "no skin slots found");
        const uint64_t rev = looks::Report::instance().revision();
        looks::Report::instance().set(state->looks);
        CHECK(looks::Report::instance().revision() == rev); // unchanged: no new revision
    }

    if (g_fail == 0)
    {
        std::printf("looks tests OK\n");
    }
    return g_fail == 0 ? 0 : 1;
}
