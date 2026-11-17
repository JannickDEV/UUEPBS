// Names given to characters in the window (core/character_names.cpp): cleaning, file format,
// which key a name is stored under, and that session-only names never reach the file.
#include "core/character_names.hpp"
#include "core/portrait_requests.hpp"
#include "core/registry.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace uuepbs;

static int failures = 0;
#define CHECK(cond)                                                                                                                        \
    do                                                                                                                                     \
    {                                                                                                                                      \
        if (!(cond))                                                                                                                       \
        {                                                                                                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                                    \
            ++failures;                                                                                                                    \
        }                                                                                                                                  \
    } while (0)

static std::string slurp(const std::filesystem::path& p)
{
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

int main()
{
    // clean(): trims, drops control characters, keeps UTF-8 whole when cutting.
    CHECK(CharacterNames::clean("  Eve  ") == "Eve");
    CHECK(CharacterNames::clean("A\tB\nC\r") == "ABC");
    CHECK(CharacterNames::clean("   ").empty());
    {
        std::string longname;
        for (int i = 0; i < 40; ++i)
            longname += "\xC3\xA9"; // e acute, 2 bytes each = 80 bytes
        const std::string c = CharacterNames::clean(longname);
        CHECK(c.size() <= CharacterNames::kMaxBytes);
        CHECK(c.size() % 2 == 0); // never half a character
    }

    // parse()/serialize(): comments, BOM, CRLF, bad lines.
    {
        const auto m = CharacterNames::parse("\xEF\xBB\xBFplayer\tMira\r\n# comment\nAnca\t  Anca the Brave \nbroken line\n\tno key\nEmpty\t\n");
        CHECK(m.size() == 2);
        CHECK(m.at("player") == "Mira");
        CHECK(m.at("Anca") == "Anca the Brave");
        CHECK(CharacterNames::parse(CharacterNames::serialize(m)) == m);
    }

    // key_for(): player and identities are stable, bare actor keys are not.
    {
        bool stable = false;
        CHECK(CharacterNames::key_for(kPlayerActor, "", &stable) == "player" && stable);
        CHECK(CharacterNames::key_for("7FF61A2B3C", "Anca", &stable) == "Anca" && stable);
        CHECK(CharacterNames::key_for("7FF61A2B3C", "", &stable) == "7FF61A2B3C" && !stable);
    }

    // set()/get()/shown() and the file.
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "uuepbs_test_names";
    std::filesystem::remove_all(dir);
    const std::filesystem::path file = dir / "_characters" / CharacterNames::kFileName;
    CharacterNames& names = CharacterNames::instance();
    names.set_file(file);
    CHECK(names.get("player").empty());
    CHECK(names.shown("Roku (player)", kPlayerActor, "") == "Roku (player)");

    const uint64_t rev = names.revision();
    CHECK(names.set("player", " Mira ", true));
    CHECK(names.revision() != rev);
    CHECK(names.get("player") == "Mira");
    CHECK(names.shown("Roku (player)", kPlayerActor, "") == "Mira (player)");
    CHECK(names.set("Anca", "Anca the Brave", true));
    CHECK(names.shown("Anca", "123ABC", "Anca") == "Anca the Brave");
    CHECK(names.set("123ABC", "Stranger", false)); // no identity: this session only
    CHECK(names.shown("BP_NPC_C_3", "123ABC", "") == "Stranger");

    const std::string text = slurp(file);
    CHECK(text.find("player\tMira\n") != std::string::npos);
    CHECK(text.find("Anca\tAnca the Brave\n") != std::string::npos);
    CHECK(text.find("Stranger") == std::string::npos);

    // Reload from disk: saved names come back, session ones are gone.
    names.set_file(file);
    CHECK(names.get("player") == "Mira");
    CHECK(names.get("Anca") == "Anca the Brave");

    // Clearing a name removes it from the file.
    CHECK(names.set("Anca", "", true));
    CHECK(names.get("Anca").empty());
    CHECK(slurp(file).find("Anca") == std::string::npos);
    CHECK(names.shown("Anca", "123ABC", "Anca") == "Anca");

    // The names file must not show up as a remembered character (only .json/.rbs are listed).
    CHECK(file.extension() != ".json" && file.extension() != ".rbs");

    std::filesystem::remove_all(dir);

    // Portrait requests (window -> script): asked once, again on retake, bad text refused.
    {
        PortraitRequests req;
        CHECK(req.add("auto", "BP_Rokuv3_C_0", false));
        CHECK(!req.add("auto", "BP_Rokuv3_C_0", false)); // already asked
        CHECK(req.add("auto", "BP_Rokuv3_C_0", true));   // retake: new nonce
        CHECK(req.add("4D5E6F", "Anca", false));
        CHECK(!req.add("bad\tid", "x", false));
        CHECK(!req.add("id", "", false));
        const std::string text = req.serialize();
        CHECK(text.rfind(std::string(PortraitRequests::kHeader) + "\n", 0) == 0);
        CHECK(text.find("auto\tBP_Rokuv3_C_0\t2\n") != std::string::npos);
        CHECK(text.find("4D5E6F\tAnca\t3\n") != std::string::npos);
        for (int i = 0; i < 300; ++i)
            req.add("a" + std::to_string(i), "s", false);
        CHECK(req.size() == PortraitRequests::kMaxEntries);
    }

    std::printf(failures ? "%d failure(s)\n" : "names: all checks passed\n", failures);
    return failures ? 1 : 0;
}
