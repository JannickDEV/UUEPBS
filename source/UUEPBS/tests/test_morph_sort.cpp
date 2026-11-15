// Host-side tests for the automatic Body tab morph sliders (core/morph_sort): word splitting,
// abbreviations, sides, categories, labels, left/right pairs, profile overrides and order.
// The main fixture is the 81 morph targets of a real mesh (Advanced > Morphs screenshots).
#include "core/morph_sort.hpp"
#include "core/morphs.hpp"

#include <algorithm>
#include <cstdio>
#include <map>
#include <string>
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

#define CHECK_EQ(a, b)                                                                                                                               \
    do                                                                                                                                               \
    {                                                                                                                                                \
        const std::string va = (a), vb = (b);                                                                                                        \
        if (va != vb)                                                                                                                                \
        {                                                                                                                                            \
            std::printf("FAIL %s:%d  %s == \"%s\", want \"%s\"\n", __FILE__, __LINE__, #a, va.c_str(), vb.c_str());                                 \
            ++g_fail;                                                                                                                                \
        }                                                                                                                                            \
    } while (0)

static const std::vector<std::string> kMesh = {
    "Morph_BreastReduction", "Morph_BreastEnl",    "Morph_BreastsHu",     "Morph_NippleHei",    "Morph_NippleWid",   "Morph_WaistNarr",
    "Morph_LegFat",          "Morph_UpperBody",    "Morph_BellyFat",      "Morph_PregnantF",    "Morph_HipsWide",    "Morph_Labia",
    "Morph_PussyGape",       "Morph_AnalGape",     "Jiggle_Ass",          "Morph_CockBulge",    "Morph_SHuge",       "Morph_SReduce",
    "Morph_Buttocks",        "Morph_PussyIn",      "Morph_PussyOut",      "Morph_BigCockBulge", "Morph_Gooch",       "Morph_SFinery",
    "Morph_AnalIn",          "Morph_AnalOut",      "Crctve_DeepSquatL",   "Crctve_DeepSquatR",  "Crctve_KneeCapL",   "Crctve_KneeCapR",
    "Morph_Chubby",          "Morph_CrackCorrect", "Morph_PussyHugeGape", "Morph_AnalHugeGape", "Morph_HugeCockBulge", "Morph_PussyShape",
    "Look-Left",             "Look-Right",         "Look-Up",             "Look-Down",          "Eyes_Close",        "Morph_Helmet",
    "Eye_CloseL",            "Eye_CloseR",         "Morph_EarSquish",     "Speech_Ooh",         "Speech_Ah",         "Speech_Oh",
    "Speech_Eee",            "Morph_Eth1",         "Morph_Eth2",          "Oral_Open",          "Oral_Suck",         "Oral_Bulge",
    "Exp_Happy",             "Exp_Anger",          "Exp_BrowUpR",         "Exp_BrowUpL",        "Ear_DownL",         "Ear_DownR",
    "Speech_Pout",           "Oral_Widen",         "Oral_Smush",          "Oral_Kiss",          "Morph_RegCock",     "Morph_BigCock",
    "Morph_PFlash",          "Morph_Crotchless",   "Morph_PFlashGape",    "Morph_RegPackage",   "Morph_BigPackage",  "Morph_RCFlash",
    "Morph_BCFlash",         "Morph_AFlash",       "Morph_AFlashGape",    "Form_Felian",        "Form_Jackal",       "mGen_Flaccid",
    "mGen_Glans",            "mGen_Balls",         "mGen_Balls2",
};

static std::string words(const std::string& name)
{
    std::string out;
    for (const std::string& w : morph_words(name))
    {
        out += (out.empty() ? "" : " ") + w;
    }
    return out;
}

static const MorphGroupMorphs* find(const std::vector<MorphGroupMorphs>& groups, const std::string& title)
{
    for (const MorphGroupMorphs& g : groups)
    {
        if (g.group.title == title)
        {
            return &g;
        }
    }
    return nullptr;
}

int main()
{
    CHECK(kMesh.size() == 81);

    // Words: prefixes, camelCase, acronyms, digits, sides, abbreviations.
    CHECK_EQ(words("Morph_BreastEnl"), "breast enlarge");
    CHECK_EQ(words("Morph_NippleHei"), "nipple height");
    CHECK_EQ(words("Morph_WaistNarr"), "waist narrow");
    CHECK_EQ(words("Morph_RCFlash"), "rc flash");
    CHECK_EQ(words("Morph_SHuge"), "s huge");
    CHECK_EQ(words("mGen_Balls2"), "m gen balls 2");
    CHECK_EQ(words("Eye_CloseL"), "eye close");
    CHECK_EQ(words("Crctve_KneeCap_R"), "corrective knee cap");
    CHECK_EQ(words("Look-Left"), "look left"); // a direction, not a side
    CHECK_EQ(words("Morph"), "morph");          // nothing else to show
    CHECK(sort_morph("Eye_CloseL").side == MorphSide::Left && sort_morph("Exp_BrowUpR").side == MorphSide::Right);
    CHECK(sort_morph("Look-Left").side == MorphSide::None && sort_morph("Morph_PregnantF").side == MorphSide::None);

    // Every morph of the mesh: category (empty = Advanced only) and label.
    const std::map<std::string, std::pair<std::string, std::string>> want = {
        {"Morph_BreastReduction", {"Chest", "Breast reduction"}},
        {"Morph_BreastEnl", {"Chest", "Breast enlarge"}},
        {"Morph_BreastsHu", {"Chest", "Breasts huge"}},
        {"Morph_NippleHei", {"Chest", "Nipple height"}},
        {"Morph_NippleWid", {"Chest", "Nipple width"}},
        {"Morph_WaistNarr", {"Torso", "Waist narrow"}},
        {"Morph_LegFat", {"Hips & Legs", "Leg fat"}},
        {"Morph_UpperBody", {"Torso", "Upper body"}},
        {"Morph_BellyFat", {"Torso", "Belly fat"}},
        {"Morph_PregnantF", {"Torso", "Pregnant F"}},
        {"Morph_HipsWide", {"Hips & Legs", "Hips wide"}},
        {"Morph_Labia", {"Groin", "Labia"}},
        {"Morph_PussyGape", {"Groin", "Pussy gape"}},
        {"Morph_AnalGape", {"Groin", "Anal gape"}},
        {"Jiggle_Ass", {"Hips & Legs", "Jiggle ass"}},
        {"Morph_CockBulge", {"Groin", "Cock bulge"}},
        {"Morph_SHuge", {"Other", "S huge"}},
        {"Morph_SReduce", {"Other", "S reduce"}},
        {"Morph_Buttocks", {"Hips & Legs", "Buttocks"}},
        {"Morph_PussyIn", {"Groin", "Pussy in"}},
        {"Morph_PussyOut", {"Groin", "Pussy out"}},
        {"Morph_BigCockBulge", {"Groin", "Big cock bulge"}},
        {"Morph_Gooch", {"Groin", "Gooch"}},
        {"Morph_SFinery", {"Other", "S finery"}},
        {"Morph_AnalIn", {"Groin", "Anal in"}},
        {"Morph_AnalOut", {"Groin", "Anal out"}},
        {"Crctve_DeepSquatL", {"", "Corrective deep squat"}},
        {"Crctve_DeepSquatR", {"", "Corrective deep squat"}},
        {"Crctve_KneeCapL", {"", "Corrective knee cap"}},
        {"Crctve_KneeCapR", {"", "Corrective knee cap"}},
        {"Morph_Chubby", {"Body shape", "Chubby"}},
        {"Morph_CrackCorrect", {"", "Crack correct"}},
        {"Morph_PussyHugeGape", {"Groin", "Pussy huge gape"}},
        {"Morph_AnalHugeGape", {"Groin", "Anal huge gape"}},
        {"Morph_HugeCockBulge", {"Groin", "Huge cock bulge"}},
        {"Morph_PussyShape", {"Groin", "Pussy shape"}},
        {"Look-Left", {"Face", "Look left"}},
        {"Look-Right", {"Face", "Look right"}},
        {"Look-Up", {"Face", "Look up"}},
        {"Look-Down", {"Face", "Look down"}},
        {"Eyes_Close", {"Face", "Eyes close"}},
        {"Morph_Helmet", {"", "Helmet"}},
        {"Eye_CloseL", {"Face", "Eye close"}},
        {"Eye_CloseR", {"Face", "Eye close"}},
        {"Morph_EarSquish", {"Face", "Ear squish"}},
        {"Speech_Ooh", {"Face", "Speech ooh"}},
        {"Speech_Ah", {"Face", "Speech ah"}},
        {"Speech_Oh", {"Face", "Speech oh"}},
        {"Speech_Eee", {"Face", "Speech eee"}},
        {"Morph_Eth1", {"Body shape", "Ethnicity 1"}},
        {"Morph_Eth2", {"Body shape", "Ethnicity 2"}},
        {"Oral_Open", {"Face", "Mouth open"}},
        {"Oral_Suck", {"Face", "Mouth suck"}},
        {"Oral_Bulge", {"Face", "Mouth bulge"}},
        {"Exp_Happy", {"Face", "Happy"}},
        {"Exp_Anger", {"Face", "Anger"}},
        {"Exp_BrowUpR", {"Face", "Brow up"}},
        {"Exp_BrowUpL", {"Face", "Brow up"}},
        {"Ear_DownL", {"Face", "Ear down"}},
        {"Ear_DownR", {"Face", "Ear down"}},
        {"Speech_Pout", {"Face", "Speech pout"}},
        {"Oral_Widen", {"Face", "Mouth widen"}},
        {"Oral_Smush", {"Face", "Mouth smush"}},
        {"Oral_Kiss", {"Face", "Mouth kiss"}},
        {"Morph_RegCock", {"Groin", "Regular cock"}},
        {"Morph_BigCock", {"Groin", "Big cock"}},
        {"Morph_PFlash", {"Groin", "P flash"}},
        {"Morph_Crotchless", {"Groin", "Crotchless"}},
        {"Morph_PFlashGape", {"Groin", "P flash gape"}},
        {"Morph_RegPackage", {"Groin", "Regular package"}},
        {"Morph_BigPackage", {"Groin", "Big package"}},
        {"Morph_RCFlash", {"Groin", "RC flash"}},
        {"Morph_BCFlash", {"Groin", "BC flash"}},
        {"Morph_AFlash", {"Groin", "A flash"}},
        {"Morph_AFlashGape", {"Groin", "A flash gape"}},
        {"Form_Felian", {"Body shape", "Felian form"}},
        {"Form_Jackal", {"Body shape", "Jackal form"}},
        {"mGen_Flaccid", {"Groin", "Flaccid"}},
        {"mGen_Glans", {"Groin", "Glans"}},
        {"mGen_Balls", {"Groin", "Balls"}},
        {"mGen_Balls2", {"Groin", "Balls 2"}},
    };
    CHECK(want.size() == kMesh.size());
    for (const std::string& m : kMesh)
    {
        const auto it = want.find(m);
        CHECK(it != want.end());
        if (it == want.end())
        {
            continue;
        }
        const MorphSort s = sort_morph(m);
        CHECK_EQ(m + " -> " + s.category, m + " -> " + it->second.first);
        CHECK_EQ(s.label, it->second.second);
    }
    CHECK_EQ(morph_label("Eye_CloseL"), "Eye close (left)");
    CHECK_EQ(morph_label("Morph_BreastEnl"), "Breast enlarge");

    // Randomize leaves faces, genitals, forms, pregnancy and unknown morphs alone.
    CHECK(sort_morph("Morph_BreastEnl").randomize && sort_morph("Morph_HipsWide").randomize && sort_morph("Morph_Chubby").randomize);
    CHECK(!sort_morph("Exp_Happy").randomize && !sort_morph("Morph_Labia").randomize && !sort_morph("Form_Felian").randomize);
    CHECK(!sort_morph("Morph_PregnantF").randomize && !sort_morph("Morph_SFinery").randomize);

    // Other common rigs.
    CHECK_EQ(sort_morph("BreastSize").category, "Chest");
    CHECK_EQ(sort_morph("ThighThickness").category, "Hips & Legs");
    CHECK_EQ(sort_morph("Face_Blink_L").category, "Face");
    CHECK_EQ(sort_morph("Muscle").category, "Body shape");
    CHECK_EQ(sort_morph("Corrective_Elbow").category, "");
    CHECK_EQ(sort_morph("head_round").category, "Head & Neck");
    CHECK_EQ(sort_morph("ForearmThick").category, "Arms");
    CHECK_EQ(sort_morph("ForearmThick").label, "Forearm thick");
    CHECK_EQ(sort_morph("Zzyzx").category, "Other");

    // Groups for the whole mesh, no profile.
    MorphProfile none;
    const std::vector<MorphGroupMorphs> groups = body_morph_groups(kMesh, none);
    size_t on_body = 0;
    std::map<std::string, size_t> per_cat;
    for (const MorphGroupMorphs& g : groups)
    {
        CHECK(g.group.automatic);
        on_body += g.morphs.size();
        per_cat[g.group.section]++;
    }
    CHECK(on_body == 81 - 6); // 4 Crctve_, CrackCorrect, Helmet stay under Advanced
    // Left/right pairs share one slider; a lone single-side morph says its side.
    const MorphGroupMorphs* brow = find(groups, "Brow up");
    CHECK(brow && brow->morphs.size() == 2);
    const MorphGroupMorphs* eye = find(groups, "Eye close");
    CHECK(eye && eye->morphs.size() == 2 && eye->group.section == "Face");
    CHECK(find(groups, "Eyes close") && find(groups, "Ear down") && find(groups, "Ear down")->morphs.size() == 2);
    CHECK(per_cat["Chest"] == 5 && per_cat["Torso"] == 4 && per_cat["Hips & Legs"] == 4 && per_cat["Other"] == 3 && per_cat["Body shape"] == 5);
    CHECK(per_cat["Groin"] == 29 && per_cat["Face"] == 22);
    // Sorted by category: Face first (no Head & Neck morphs here), Other last, mesh order inside a category.
    CHECK(groups.front().group.section == "Face" && groups.back().group.section == "Other");
    for (size_t i = 1; i < groups.size(); ++i)
    {
        CHECK(section_rank(groups[i - 1].group.section) <= section_rank(groups[i].group.section));
    }
    CHECK(find(groups, "Breast reduction") < find(groups, "Breast enlarge"));
    const std::vector<std::string> one_side = {"Eye_CloseL", "Ear_DownR"};
    const auto lone = body_morph_groups(one_side, none);
    CHECK(lone.size() == 2 && find(lone, "Eye close (left)") && find(lone, "Ear down (right)"));
    // Two names that read the same keep apart (and from profile titles).
    const auto twins = body_morph_groups({"Morph_Belly", "Belly"}, none);
    CHECK(twins.size() == 2 && find(twins, "Belly") && find(twins, "Belly (Belly)"));

    // Ranks: bone categories keep the dictionary order, new ones slot in around them.
    CHECK(section_rank("Head & Neck") < section_rank("Face") && section_rank("Face") < section_rank("Torso"));
    CHECK(section_rank("Torso") < section_rank("Chest") && section_rank("Chest") < section_rank("Hips & Legs"));
    CHECK(section_rank("Arms") < section_rank("Muscles") && section_rank("Muscles") < section_rank("Body shape"));
    CHECK(section_rank("Body shape") < section_rank("Custom") && section_rank("Custom") < section_rank("Groin"));
    CHECK(section_rank("Groin") < section_rank("Other") && section_rank("other") == section_rank("Other"));

    // Profile MorphGroups win: their morphs are not sorted again; ExcludeMorphs hides; the switch turns sorting off.
    {
        MorphProfile mp;
        std::string msg;
        CHECK(parse_morph_profile(R"({ "MorphGroups": { "Bust": { "Morphs": ["Morph_Breast*"], "Section": "Chest" }, "Wink": ["Eye_CloseL"] },
                                       "ExcludeMorphs": ["Speech_*"] })",
                                  mp, msg));
        CHECK(mp.auto_groups);
        const auto g = body_morph_groups(kMesh, mp);
        CHECK(g.size() > 2 && g[0].group.title == "Bust" && !g[0].group.automatic && g[0].morphs.size() == 3);
        CHECK(g[1].group.title == "Wink" && g[1].morphs == std::vector<std::string>{"Eye_CloseL"});
        CHECK(g[0].group.section == "Chest" && g[1].group.section == "Face"); // no "Section": placed by its name
        CHECK(!find(g, "Breast enlarge") && !find(g, "Speech ah"));
        const MorphGroupMorphs* right = find(g, "Eye close (right)"); // its partner went to the profile group
        CHECK(right && right->morphs.size() == 1);
        CHECK(find(g, "Nipple height") && find(g, "Nipple height")->group.section == "Chest");

        CHECK(parse_morph_profile(R"({ "MorphGroups": { "Bust": ["Morph_Breast*"] }, "AutoMorphGroups": false })", mp, msg));
        CHECK(!mp.auto_groups && msg.find("automatic morph sliders off") != std::string::npos);
        const auto only = body_morph_groups(kMesh, mp);
        CHECK(only.size() == 1 && only[0].group.title == "Bust");
        CHECK(!parse_morph_profile(R"({ "AutoMorphGroups": "no" })", mp, msg));
        CHECK(parse_morph_profile(R"({ "MorphGroups": { "Belly": ["Morph_BellyFat"] } })", mp, msg));
        const auto clash = body_morph_groups({"Morph_BellyFat", "Belly"}, mp);
        CHECK(parse_morph_profile(R"({ "MorphGroups": { "Mystery": ["Zzyzx"], "Fat": ["Morph_SFinery"] } })", mp, msg));
        const auto unplaced = body_morph_groups({"Zzyzx", "Morph_SFinery"}, mp);
        CHECK(unplaced.size() == 2 && unplaced[0].group.section.empty() && unplaced[1].group.section == "Body shape"); // -> "Morphs"
        CHECK(clash.size() == 2 && clash[0].group.title == "Belly" && find(clash, "Belly (Belly)")); // auto title steps aside
    }

    std::printf(g_fail ? "MORPH SORT TESTS FAILED (%d)\n" : "morph sort tests passed\n", g_fail);
    return g_fail;
}
