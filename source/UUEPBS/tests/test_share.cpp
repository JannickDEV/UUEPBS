// Preset share codes (core/share_code.*) and the next/previous preset hotkey order (core/preset_cycle.hpp).
//   g++ -std=c++23 -I../src test_share.cpp ../src/core/*.cpp -o test_share && ./test_share
#include "core/preset_cycle.hpp"
#include "core/presets.hpp"
#include "core/share_code.hpp"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

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

static EditBook sample_book()
{
    EditBook book;
    auto add = [&](const std::string& bone, BoneEdit e) { book[fold_case(bone)] = EditEntry{bone, e}; };
    BoneEdit boob;
    boob.axis[0] = boob.axis[1] = boob.axis[2] = 1.2;
    add("Boob_L", boob);
    BoneEdit thigh;
    thigh.axis[1] = 1.15;
    thigh.axis[2] = 1.1;
    thigh.spread = Spread::Keep;
    add("thigh_l", thigh);
    BoneEdit head;
    head.turn[2] = 10;
    head.shift[2] = 1.5;
    head.spread = Spread::Solo;
    add("head", head);
    add("neutral_bone", BoneEdit{}); // left out of the code
    BoneEdit odd;                     // name with quotes and a backslash
    odd.axis[0] = 0.75;
    add("we\"ird\\bone ü", odd);
    return book;
}

static MorphBook sample_morphs()
{
    MorphBook m;
    m[fold_case("BreastSize")] = MorphEntry{"BreastSize", 0.8};
    m[fold_case("Belly")] = MorphEntry{"Belly", 0.0}; // 0 still overrides the game's value
    return m;
}

static void test_base64_and_crc()
{
    CHECK(share::crc32("123456789") == 0xCBF43926u); // standard check value
    for (std::string s : {std::string(), std::string("f"), std::string("fo"), std::string("foo"), std::string("foob"), std::string("\xff\x00\xfe", 3)})
    {
        std::string back;
        CHECK(share::base64url_decode(share::base64url_encode(s), back));
        CHECK(back == s);
    }
    CHECK(share::base64url_encode("\xfb\xff") == "-_8");
    std::string out;
    CHECK(!share::base64url_decode("ab+c", out));  // standard base64 alphabet is not accepted
    CHECK(!share::base64url_decode("abcde", out)); // impossible length
    CHECK(share::base64url_decode("Zg", out) && out == "f");
    CHECK(!share::base64url_decode("Zh", out)); // non-zero leftover bits: one code per byte string
    CHECK(share::base64url_decode("Zg==", out) && out == "f");
}

static void test_round_trip()
{
    const EditBook book = sample_book();
    const MorphBook morphs = sample_morphs();
    const std::string code = share::encode(book, &morphs, "My: preset?");
    CHECK(code.rfind("UUEPBS1.", 0) == 0);
    CHECK(code.find_first_of(" \n\t\"") == std::string::npos);
    std::printf("code for 4 bones + 2 morphs: %zu characters\n", code.size());

    share::Decoded d;
    std::string msg;
    CHECK(share::decode(code, d, msg));
    CHECK(d.name == "My preset"); // made file-name safe
    CHECK(d.bones.size() == 4);
    CHECK(d.morphs.size() == 2);
    const auto& b = d.bones.at(fold_case("boob_l"));
    CHECK(b.bone == "Boob_L");
    CHECK(near(b.edit.axis[0], 1.2) && near(b.edit.axis[1], 1.2) && near(b.edit.axis[2], 1.2));
    CHECK(b.edit.spread == Spread::Chain);
    const auto& t = d.bones.at("thigh_l");
    CHECK(near(t.edit.axis[0], 1.0) && near(t.edit.axis[1], 1.15) && near(t.edit.axis[2], 1.1));
    CHECK(t.edit.spread == Spread::Keep);
    const auto& h = d.bones.at("head");
    CHECK(near(h.edit.turn[2], 10) && near(h.edit.shift[2], 1.5) && !h.edit.has_scale());
    CHECK(h.edit.spread == Spread::Solo);
    CHECK(d.bones.count(fold_case("we\"ird\\bone ü")) == 1);
    CHECK(near(d.morphs.at(fold_case("belly")).weight, 0.0));
    CHECK(near(d.morphs.at(fold_case("breastsize")).weight, 0.8));

    // Wrapped by a chat app: line breaks and spaces anywhere.
    std::string wrapped = "  \n";
    for (size_t i = 0; i < code.size(); ++i)
    {
        wrapped += code[i];
        if (i % 17 == 16)
        {
            wrapped += i % 2 ? "\r\n" : " ";
        }
    }
    CHECK(share::decode(wrapped, d, msg));
    CHECK(share::looks_like_code(wrapped));

    // The code body is a normal preset: a preset file reads it the same way.
    EditBook from_file;
    MorphBook file_morphs;
    CHECK(PresetShelf::parse(share::compact_json(book, &morphs, ""), from_file, msg, &file_morphs));
    CHECK(from_file.size() == 4 && file_morphs.size() == 2);

    // Morph-only and unnamed.
    EditBook none;
    const std::string morph_code = share::encode(none, &morphs);
    CHECK(share::decode(morph_code, d, msg) && d.bones.empty() && d.morphs.size() == 2 && d.name.empty());
}

static std::string make_code(const std::string& json)
{
    char sum[16];
    std::snprintf(sum, sizeof(sum), "%08x", static_cast<unsigned>(share::crc32(json)));
    return std::string(share::kPrefix) + share::base64url_encode(json) + "." + sum;
}

static void test_rejects()
{
    share::Decoded d;
    std::string msg;
    const EditBook book = sample_book();
    const std::string code = share::encode(book, nullptr, "x");

    CHECK(!share::decode("", d, msg));
    CHECK(!share::decode("hello there", d, msg));
    CHECK(!share::decode("UUEPBS9.abcd.00000000", d, msg) && msg.find("newer") != std::string::npos);
    CHECK(!share::decode(code.substr(0, code.size() - 3), d, msg)); // cut off at the end
    CHECK(!share::decode(code.substr(0, code.size() / 2) + code.substr(code.size() - 9), d, msg)); // middle lost
    std::string flipped = code;
    flipped[20] = flipped[20] == 'A' ? 'B' : 'A';
    CHECK(!share::decode(flipped, d, msg) && msg.find("checksum") != std::string::npos);
    std::string bad_char = code;
    bad_char[20] = '+';
    CHECK(!share::decode(bad_char, d, msg));
    std::string bad_sum = code;
    bad_sum.back() = 'z';
    CHECK(!share::decode(bad_sum, d, msg));
    CHECK(d.bones.empty() && d.morphs.empty()); // nothing half-filled after a failure

    // Valid checksum, hostile or wrong contents.
    CHECK(!share::decode(make_code("not json"), d, msg));
    CHECK(!share::decode(make_code("boob_l 1.2 1.2 1.2"), d, msg)); // the legacy text format is not taken from codes
    CHECK(!share::decode(make_code("[1,2,3]"), d, msg));
    CHECK(!share::decode(make_code("{\"bones\":{\"a\":{\"scale\":2}}}"), d, msg)); // no "format"
    CHECK(!share::decode(make_code("{\"format\":\"something else\",\"bones\":{\"a\":{\"scale\":2}}}"), d, msg));
    CHECK(!share::decode(make_code("{\"format\":\"UUEPBS preset\",\"bones\":{}}"), d, msg)); // empty
    std::string deep = "{\"format\":\"UUEPBS preset\",\"bones\":{\"a\":";
    for (int i = 0; i < 5000; ++i)
    {
        deep += "[";
    }
    CHECK(!share::decode(make_code(deep), d, msg)); // nesting limit, no stack overflow
    std::string many = "{\"format\":\"UUEPBS preset\",\"bones\":{";
    for (size_t i = 0; i <= share::kMaxBones; ++i)
    {
        many += (i ? ",\"b" : "\"b") + std::to_string(i) + "\":{\"scale\":1.1}";
    }
    many += "}}";
    CHECK(!share::decode(make_code(many), d, msg) && msg.find("too many") != std::string::npos);
    CHECK(!share::decode(make_code("{\"format\":\"UUEPBS preset\",\"bones\":{\"a\\u0001b\":{\"scale\":1.1}}}"), d, msg));
    CHECK(!share::decode(make_code("{\"format\":\"UUEPBS preset\",\"bones\":{\"" + std::string(300, 'x') + "\":{\"scale\":1.1}}}"), d, msg));
    CHECK(!share::decode(std::string(share::kMaxCodeChars + 1, 'A'), d, msg));

    // Out-of-range and non-finite numbers are clamped like preset files, not refused.
    CHECK(share::decode(make_code("{\"format\":\"UUEPBS preset\",\"name\":\"..\\\\..\\\\evil\",\"bones\":{\"a\":{\"scale\":1e999,\"rotate\":[9999,-9999,0],"
                                  "\"move\":[1e9,0,0]}},\"morphs\":{\"m\":1e300}}"),
                        d, msg));
    const BoneEdit& e = d.bones.at("a").edit;
    CHECK(std::isfinite(e.axis[0]) && e.axis[0] <= 20.0);
    CHECK(near(e.turn[0], BoneEdit::kMaxTurn) && near(e.turn[1], -BoneEdit::kMaxTurn));
    CHECK(near(e.shift[0], BoneEdit::kMaxShift));
    CHECK(d.morphs.at("m").weight <= kMorphMax);
    CHECK(d.name.find('\\') == std::string::npos && d.name.find('/') == std::string::npos); // cannot name a path
}

static void test_cycle()
{
    const std::vector<std::string> names = {"Athletic", "curvy", "Petite", "Tall"};
    CHECK(PresetCycle::pick({}, "", 1).empty());
    CHECK(PresetCycle::pick(names, "", 1) == "Athletic");
    CHECK(PresetCycle::pick(names, "", -1) == "Tall");
    CHECK(PresetCycle::pick(names, "Athletic", 1) == "curvy");
    CHECK(PresetCycle::pick(names, "Tall", 1) == "Athletic"); // wraps
    CHECK(PresetCycle::pick(names, "Athletic", -1) == "Tall");
    CHECK(PresetCycle::pick(names, "CURVY", 1) == "Petite"); // case-insensitive
    CHECK(PresetCycle::pick(names, "Medium", 1) == "Petite"); // deleted: continues where it would sort
    CHECK(PresetCycle::pick(names, "Medium", -1) == "curvy");
    CHECK(PresetCycle::pick(names, "Zzz", 1) == "Athletic");
    CHECK(PresetCycle::pick(names, "Aaa", -1) == "Tall");
    CHECK(PresetCycle::pick({"Only"}, "Only", 1) == "Only");
    CHECK(PresetCycle::position(names, "Petite") == "3/4");

    PresetCycle c;
    c.remember("player", "curvy");
    c.remember("npc:Lune", "Tall");
    CHECK(c.current("player") == "curvy");
    CHECK(c.current("npc:Lune") == "Tall");
    CHECK(c.current("someone").empty());
}

int main()
{
    test_base64_and_crc();
    test_round_trip();
    test_rejects();
    test_cycle();
    if (g_fail)
    {
        std::printf("%d check(s) failed\n", g_fail);
        return 1;
    }
    std::printf("share codes + preset cycle: all checks passed\n");
    return 0;
}
