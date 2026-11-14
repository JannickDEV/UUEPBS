// Host-side tests for the preset browser's data (core/preset_library): tags in _library/presets.json,
// search and tag filter, thumbnail paths, cleanup on delete, thumbnail requests, and that existing
// preset files (and the preset list) are untouched by any of it.
#include "core/preset_library.hpp"
#include "core/presets.hpp"

#include <cstdio>
#include <fstream>
#include <sstream>

using namespace uuepbs;
namespace fs = std::filesystem;

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

static std::string read_file(const fs::path& p)
{
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static void touch(const fs::path& p, const std::string& body = "x")
{
    std::ofstream(p, std::ios::binary) << body;
}

static void test_tags()
{
    CHECK(PresetLibrary::clean_tag("  tall   legs ") == "tall legs");
    CHECK(PresetLibrary::clean_tag("#curvy") == "curvy");
    CHECK(PresetLibrary::clean_tag("a\"b\\c") == "abc");
    CHECK(PresetLibrary::clean_tag("   ").empty());
    CHECK(PresetLibrary::clean_tag(std::string(40, 'x')).size() == PresetLibrary::kMaxTagLength);
    // never cut a UTF-8 character in half: 23 ASCII + "é" (2 bytes) -> 23 bytes
    CHECK(PresetLibrary::clean_tag(std::string(23, 'a') + "\xC3\xA9" + "zz") == std::string(23, 'a'));

    const auto t = PresetLibrary::split_tags("curvy, Roku ,, roku; tall legs");
    CHECK(t.size() == 3 && t[0] == "curvy" && t[1] == "Roku" && t[2] == "tall legs");
    std::string many;
    for (int i = 0; i < 20; ++i)
        many += "t" + std::to_string(i) + ",";
    CHECK(PresetLibrary::split_tags(many).size() == PresetLibrary::kMaxTags);
}

static void test_search()
{
    const std::vector<std::string> tags = {"curvy", "Roku", "tall legs"};
    CHECK(PresetLibrary::matches("Big Hips", tags, "", {}));
    CHECK(PresetLibrary::matches("Big Hips", tags, "hip", {}));
    CHECK(PresetLibrary::matches("Big Hips", tags, "HIPS roku", {}));   // name + tag, case-insensitive
    CHECK(PresetLibrary::matches("Big Hips", tags, "legs", {}));        // part of a tag
    CHECK(!PresetLibrary::matches("Big Hips", tags, "hips petite", {})); // every word must match
    CHECK(PresetLibrary::matches("Big Hips", tags, "#ro", {}));         // tag prefix
    CHECK(!PresetLibrary::matches("Big Hips", tags, "#hips", {}));      // #word: tags only
    CHECK(!PresetLibrary::matches("Big Hips", tags, "#legs", {}));      // prefix, not middle
    CHECK(PresetLibrary::matches("Big Hips", tags, "#", {}));           // lone # while typing
    CHECK(PresetLibrary::matches("Big Hips", tags, "", {"roku", "curvy"}));
    CHECK(!PresetLibrary::matches("Big Hips", tags, "", {"roku", "fit"}));
    CHECK(!PresetLibrary::matches("Big Hips", {}, "", {"roku"}));
}

static void test_library(const fs::path& root)
{
    fs::remove_all(root);
    PresetShelf shelf;
    shelf.set_folder(root);
    std::string msg;
    EditBook book;
    EditEntry e;
    e.bone = "thigh_l";
    e.edit.axis[0] = e.edit.axis[1] = e.edit.axis[2] = 1.2;
    book["thigh_l"] = e;
    CHECK(shelf.save("Curvy", book, msg));
    CHECK(shelf.save("Petite", book, msg));
    const std::string preset_before = read_file(root / "Curvy.json");

    PresetLibrary lib;
    lib.set_presets_folder(root);
    CHECK(lib.ready() && lib.folder() == root / "_library");
    CHECK(lib.tags("Curvy").empty()); // no library yet: nothing breaks

    CHECK(lib.set_tags("Curvy", {"curvy", "roku"}));
    CHECK(lib.add_tag("curvy", "Roku, favourite")); // other case of the name; duplicate tag dropped
    CHECK(lib.tags("Curvy") == (std::vector<std::string>{"curvy", "roku", "favourite"}));
    CHECK(lib.add_tag("Petite", "roku"));
    CHECK(lib.remove_tag("Curvy", "FAVOURITE"));
    CHECK(lib.tags("Curvy").size() == 2);

    // The preset file itself is untouched, still loads, and the library folder is not listed as a preset.
    CHECK(read_file(root / "Curvy.json") == preset_before);
    EditBook back;
    CHECK(shelf.load("Curvy", back, msg) && back.size() == 1);
    const auto names = shelf.list();
    CHECK(names == (std::vector<std::string>{"Curvy", "Petite"}));

    // Another instance reads what was written.
    PresetLibrary again;
    again.set_presets_folder(root);
    CHECK(again.tags("CURVY") == (std::vector<std::string>{"curvy", "roku"}));
    const auto counts = again.tag_counts(names);
    CHECK(counts.size() == 2 && counts[0].first == "roku" && counts[0].second == 2 && counts[1].first == "curvy");
    CHECK(again.filter(names, "", {"curvy"}) == std::vector<std::string>{"Curvy"});
    CHECK(again.filter(names, "#roku", {}) == names);
    CHECK(again.filter(names, "pet", {}) == std::vector<std::string>{"Petite"});
    CHECK(again.tag_counts({"Petite"}).size() == 1); // only presets that exist count

    // Hand-edited file: picked up on reload; a string of tags works too; a broken file keeps the old tags.
    fs::file_time_type t = fs::last_write_time(root / "_library" / "presets.json");
    touch(root / "_library" / "presets.json",
          R"({ "format": "UUEPBS preset library", "version": 1, "presets": { "Petite": { "tags": "small, cute" }, "Curvy": { "tags": [] } } })");
    fs::last_write_time(root / "_library" / "presets.json", t + std::chrono::seconds(5));
    again.reload_if_changed();
    CHECK(again.tags("Petite") == (std::vector<std::string>{"small", "cute"}));
    CHECK(again.tags("Curvy").empty());
    touch(root / "_library" / "presets.json", "{ broken");
    fs::last_write_time(root / "_library" / "presets.json", t + std::chrono::seconds(10));
    again.reload_if_changed();
    CHECK(again.tags("Petite").size() == 2);

    // Round trip of the format, with characters that need escaping.
    std::map<std::string, std::pair<std::string, std::vector<std::string>>> entries, parsed;
    entries["a \"q\""] = {"A \"Q\"", {"x\\y", "z"}};
    entries["empty"] = {"Empty", {}};
    CHECK(PresetLibrary::parse(PresetLibrary::serialize(entries), parsed));
    CHECK(parsed.size() == 1 && parsed.begin()->second.first == "A \"Q\"");
    CHECK(!PresetLibrary::parse("{\"bones\": {}}", parsed)); // a preset file is not a library

    // Thumbnail paths and cleanup on delete.
    CHECK(again.auto_thumbnail("Curvy") == root / "_library" / "Curvy.auto.png");
    touch(root / "_library" / "Curvy.auto.png");
    touch(root / "_library" / "Curvy.jpg");
    touch(root / "_library" / "Petite.auto.png");
    CHECK(shelf.remove("Curvy", msg));
    again.forget("Curvy");
    CHECK(!fs::exists(root / "_library" / "Curvy.auto.png") && !fs::exists(root / "_library" / "Curvy.jpg"));
    CHECK(fs::exists(root / "_library" / "Petite.auto.png"));
    PresetLibrary third;
    third.set_presets_folder(root);
    CHECK(third.tags("Petite").size() == 2);

    // No folder: everything is a quiet no-op.
    PresetLibrary none;
    CHECK(!none.ready() && !none.set_tags("x", {"y"}) && none.auto_thumbnail("x").empty());
    none.forget("x");
}

static void test_requests(const fs::path& root)
{
    ThumbnailRequests r;
    CHECK(r.add("auto", "Curvy"));
    CHECK(r.add("7FF1", "Petite"));
    CHECK(r.add("auto", "curvy")); // saved again: replaces the old line, new nonce
    CHECK(!r.add("auto", ""));
    CHECK(!r.add("bad\tid", "x"));
    CHECK(r.size() == 2);
    const std::string text = r.serialize();
    CHECK(text.rfind("UUEPBS thumbnails 1\n", 0) == 0);
    CHECK(text.find("7FF1\tPetite\t") != std::string::npos);
    CHECK(text.find("auto\tcurvy\t") != std::string::npos && text.find("auto\tCurvy\t") == std::string::npos);
    // newest last, nonces increase and are not 1, 2, 3 (the script remembers lines across a DLL reload)
    unsigned a = 0, b = 0;
    CHECK(std::sscanf(text.c_str() + text.find("Petite\t") + 7, "%u", &a) == 1);
    CHECK(std::sscanf(text.c_str() + text.find("curvy\t") + 6, "%u", &b) == 1);
    CHECK(b > a && a > 3);
    for (int i = 0; i < 100; ++i)
        r.add("auto", "p" + std::to_string(i));
    CHECK(r.size() == ThumbnailRequests::kMaxEntries);
    fs::create_directories(root);
    CHECK(r.write(root / "thumbnail_requests.txt"));
    CHECK(read_file(root / "thumbnail_requests.txt") == r.serialize());
}

int main(int argc, char** argv)
{
    const fs::path root = argc > 1 ? fs::path(argv[1]) : fs::temp_directory_path() / "uuepbs_library_test";
    test_tags();
    test_search();
    test_library(root / "UUEPBS Presets");
    test_requests(root / "native");
    std::printf(g_fail ? "%d failure(s)\n" : "preset library: all checks passed\n", g_fail);
    return g_fail ? 1 : 0;
}
