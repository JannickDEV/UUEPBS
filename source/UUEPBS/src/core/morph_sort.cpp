#include "morph_sort.hpp"

#include "sculpt.hpp" // fold_case

#include <algorithm>
#include <cctype>
#include <map>
#include <set>

namespace uuepbs
{
    namespace
    {
        bool is_upper(char c) { return std::isupper(static_cast<unsigned char>(c)) != 0; }
        bool is_lower(char c) { return std::islower(static_cast<unsigned char>(c)) != 0; }
        bool is_digit(char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; }
        bool is_alnum(char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0; }

        std::string lower(std::string_view s)
        {
            std::string out(s);
            for (char& c : out)
            {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            return out;
        }

        // "Morph_BreastEnl" -> Morph Breast Enl, "RCFlash" -> RC Flash, "mGen_Balls2" -> m Gen Balls 2,
        // "Eye_CloseL" -> Eye Close L. Original spelling is kept (acronyms stay upper case in labels).
        std::vector<std::string> split_words(std::string_view name)
        {
            std::vector<std::string> out;
            std::string cur;
            auto flush = [&] {
                if (!cur.empty())
                {
                    out.push_back(cur);
                    cur.clear();
                }
            };
            for (size_t i = 0; i < name.size(); ++i)
            {
                const char c = name[i];
                if (!is_alnum(c))
                {
                    flush();
                    continue;
                }
                if (!cur.empty())
                {
                    const char p = cur.back();
                    const char n = i + 1 < name.size() ? name[i + 1] : '\0';
                    const bool boundary = (is_upper(c) && is_lower(p)) ||                // nippleHei
                                          (is_upper(c) && is_upper(p) && is_lower(n)) || // RCFlash -> RC | Flash
                                          (is_digit(c) != is_digit(p));                  // Eth1, Balls2
                    if (boundary)
                    {
                        flush();
                    }
                }
                cur.push_back(c);
            }
            flush();
            return out;
        }

        // Abbreviations seen in game morph names, spelled out for matching and for labels.
        const std::map<std::string, std::string>& abbreviations()
        {
            static const std::map<std::string, std::string> table = {
                {"enl", "enlarge"},   {"enlg", "enlarge"},  {"hu", "huge"},      {"hei", "height"},    {"hgt", "height"},
                {"ht", "height"},     {"wid", "width"},     {"wd", "width"},     {"narr", "narrow"},   {"nar", "narrow"},
                {"reg", "regular"},   {"sm", "small"},      {"lg", "large"},     {"med", "medium"},    {"thk", "thick"},
                {"len", "length"},    {"lng", "length"},    {"sz", "size"},      {"rot", "rotate"},    {"fwd", "forward"},
                {"bwd", "backward"},  {"dn", "down"},       {"crctve", "corrective"}, {"crct", "corrective"},
                {"eth", "ethnicity"}, {"exp", "expression"}, {"mth", "mouth"},   {"brw", "brow"},      {"chk", "cheek"},
                {"tum", "tummy"},     {"shldr", "shoulder"},     {"stom", "stomach"},  {"mus", "muscle"},   {"musc", "muscle"},
            };
            return table;
        }

        std::string expand(const std::string& word_lower)
        {
            const auto& t = abbreviations();
            const auto it = t.find(word_lower);
            return it != t.end() ? it->second : word_lower;
        }

        struct Rule
        {
            const char* category; // nullptr = Advanced only
            bool randomize;
            std::vector<const char*> words; // "upper body" = two words in a row
        };

        // First match wins, so the order matters: Face before Groin keeps "Oral_Bulge" a mouth shape,
        // body parts before "Body shape" keep "LegFat" with the legs and "BellyFat" with the torso.
        const std::vector<Rule>& rules()
        {
            static const std::vector<Rule> table = {
                // Driven by the game (pose correctives, clothing fits): editing them by hand looks broken.
                {nullptr, false, {"corrective", "correct", "correction", "fix", "jcm", "pcm", "helmet", "hat", "hood", "mask",
                                  "glasses", "shoe", "boot", "glove", "cloth", "clothing", "clothes", "outfit", "armor",
                                  "armour", "garment", "collision"}},
                {"Face", false, {"look", "eye", "eyelid", "lid", "blink", "wink", "squint", "brow", "eyebrow", "expression",
                                 "emotion", "speech", "viseme", "phoneme", "oral", "mouth", "lip", "jaw", "cheek", "nose",
                                 "nostril", "ear", "smile", "frown", "happy", "sad", "anger", "angry", "fear", "scared",
                                 "surprise", "surprised", "disgust", "tongue", "teeth", "tooth", "chin", "forehead", "face",
                                 "facial", "kiss", "pout", "sneer", "grin", "snarl", "pupil", "iris", "dimple", "cheekbone", "eyelash",
                                 "lash", "eyeball", "jawline", "temple", "philtrum", "septum", "nasolabial", "earlobe", "fang",
                                 "fcl"}}, // fcl: VRoid / VRM face morphs (Fcl_EYE_Close, Fcl_MTH_A)
                // Before Groin, so "Nipple_Erect" stays with the chest.
                {"Chest", true, {"nipple", "areola", "areolae"}},
                {"Groin", false, {"labia", "pussy", "vagina", "vaginal", "vulva", "clit", "clitoris", "anal", "anus", "gooch",
                                  "perineum", "cock", "penis", "dick", "glans", "foreskin", "shaft", "ball", "scrotum",
                                  "testicle", "package", "gen", "genital", "genitalia", "crotch", "crotchless", "flash",
                                  "flaccid", "erect", "erection", "groin", "pubic", "pube"}},
                {"Chest", true, {"breast", "boob", "bust", "nipple", "areola", "chest", "pec", "pectoral", "cleavage", "tit",
                                 "mammary", "underbust", "busty"}},
                {"Head & Neck", true, {"head", "skull", "neck", "throat", "hair", "horn"}},
                {"Arms", true, {"arm", "upperarm", "forearm", "bicep", "tricep", "elbow", "hand", "finger", "thumb", "wrist",
                                "palm", "nail", "knuckle"}},
                {"Hips & Legs", true, {"hip", "butt", "buttock", "ass", "glute", "gluteus", "bum", "booty", "thigh", "leg", "knee",
                                       "calf", "calves", "shin", "foot", "feet", "ankle", "toe", "heel", "pelvis", "lower body"}},
                {"Torso", false, {"pregnant", "pregnancy"}},
                {"Torso", true, {"waist", "belly", "stomach", "tummy", "ab", "abdomen", "abdominal", "torso", "spine", "back",
                                 "rib", "ribcage", "navel", "upper body", "love handle", "shoulder", "collarbone", "clavicle",
                                 "trapezius", "lat", "trap", "sixpack", "bellybutton", "midriff", "oblique"}},
                {"Body shape", true, {"fat", "chubby", "thin", "skinny", "slim", "muscle", "muscular", "weight", "obese", "fit",
                                      "athletic", "bodybuilder", "curvy", "petite", "emaciated", "build", "physique", "body",
                                      "bulk", "toned", "plump", "heavy", "lean", "age", "old", "young", "voluptuous", "hourglass", "pear",
                                      "figure", "curvaceous", "slender", "stocky", "chunky", "thick", "height", "tall"}},
                {"Body shape", false, {"form", "ethnicity", "race", "species", "gender", "male", "female", "masculine",
                                       "feminine"}},
            };
            return table;
        }

        // "breast" also matches "breasts", "ball" matches "balls".
        bool word_is(const std::string& w, std::string_view key)
        {
            if (w == key)
            {
                return true;
            }
            if (w.size() == key.size() + 1 && w.back() == 's' && w.compare(0, key.size(), key) == 0)
            {
                return true;
            }
            return w.size() == key.size() + 2 && w.compare(0, key.size(), key) == 0 && w.compare(key.size(), 2, "es") == 0;
        }

        bool rule_matches(const Rule& r, const std::vector<std::string>& words)
        {
            for (const char* key : r.words)
            {
                const std::string_view k(key);
                const size_t space = k.find(' ');
                for (size_t i = 0; i < words.size(); ++i)
                {
                    if (space == std::string_view::npos)
                    {
                        if (word_is(words[i], k))
                        {
                            return true;
                        }
                    }
                    else if (i + 1 < words.size() && words[i] == k.substr(0, space) && word_is(words[i + 1], k.substr(space + 1)))
                    {
                        return true;
                    }
                }
            }
            return false;
        }

        // Words that only say "this is a morph" (or name the file's grouping), never part of a label.
        bool noise_prefix(const std::string& w) { return w == "morph" || w == "bs" || w == "blendshape" || w == "target" || w == "mt"; }

        struct Parsed
        {
            std::vector<std::string> raw;   // original spelling, side and noise removed
            std::vector<std::string> words; // lower case, abbreviations spelled out
            MorphSide side{MorphSide::None};
        };

        Parsed parse(std::string_view name)
        {
            Parsed p;
            std::vector<std::string> raw = split_words(name);
            // A trailing single L / R (Eye_CloseL, Crctve_KneeCap_R) is a side. "Look-Left" is a direction.
            if (raw.size() > 1)
            {
                const std::string last = lower(raw.back());
                if (last == "l" || last == "r")
                {
                    p.side = last == "l" ? MorphSide::Left : MorphSide::Right;
                    raw.pop_back();
                }
            }
            size_t start = 0;
            while (start + 1 < raw.size() && noise_prefix(lower(raw[start])))
            {
                ++start;
            }
            // mGen_ (male genitals) prefix: keep "gen" for matching, but not in the label.
            for (size_t i = start; i < raw.size(); ++i)
            {
                p.raw.push_back(raw[i]);
                p.words.push_back(expand(lower(raw[i])));
            }
            return p;
        }

        std::string capitalize_first(std::string s)
        {
            if (!s.empty())
            {
                s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
            }
            return s;
        }

        // Rig naming prefixes that say which file or tool a morph came from, never part of a label:
        // Daz Genesis (PBMBreastsSize, FBMHeavy, PHMEarsElf, CTRLBreastsNatural, eCTRLSmile, pJCMShldrUp),
        // VRoid / VRM (Fcl_ALL_Joy, Fcl_MTH_A), Character Creator's ARKit numbering (A01_Brow_Inner_Up).
        bool rig_prefix(const std::string& w)
        {
            return w == "pbm" || w == "fbm" || w == "phm" || w == "fhm" || w == "ctrl" || w == "jcm" || w == "fcl";
        }

        // VRoid face groups after "Fcl_": ALL (whole face) adds nothing, the others name the part.
        const char* vroid_group(const std::string& w)
        {
            if (w == "all")
            {
                return "";
            }
            if (w == "mouth" || w == "brow" || w == "eye")
            {
                return w.c_str(); // already spelled out (MTH, BRW)
            }
            if (w == "ha" || w == "hau")
            {
                return "teeth";
            }
            return nullptr;
        }

        std::string label_of(const Parsed& p, std::string_view name)
        {
            std::vector<std::string> words;
            size_t i = 0;
            bool vroid = false;
            // a lone "e" / "p" in front of a Daz prefix (eCTRL, pJCM, pCTRL)
            while (i + 1 < p.words.size() &&
                   (rig_prefix(p.words[i]) || ((p.words[i] == "e" || p.words[i] == "p") && rig_prefix(p.words[i + 1]))))
            {
                vroid = vroid || p.words[i] == "fcl";
                ++i;
            }
            if (i + 2 < p.raw.size() && p.raw[i].size() == 1 && is_upper(p.raw[i][0]) &&
                std::all_of(p.raw[i + 1].begin(), p.raw[i + 1].end(), [](char c) { return is_digit(c); }))
            {
                i += 2; // A01_Brow_Inner_Up -> "Brow inner up"
            }
            if (vroid && i + 1 < p.words.size() && vroid_group(p.words[i]))
            {
                const char* part = vroid_group(p.words[i]);
                if (*part)
                {
                    words.push_back(part);
                }
                ++i;
            }
            if (p.words.size() > i + 2 && p.words[i] == "m" && p.words[i + 1] == "gen")
            {
                i += 2; // mGen_Balls -> "Balls"
            }
            else if (p.words.size() > i + 1 && p.words[i] == "expression")
            {
                ++i; // Exp_Happy -> "Happy" (it sits under Face anyway)
            }
            for (; i < p.raw.size(); ++i)
            {
                const std::string& raw = p.raw[i];
                const std::string& w = p.words[i];
                std::string out;
                const bool acronym = raw.size() > 1 && std::all_of(raw.begin(), raw.end(), [](char c) { return is_upper(c); });
                if (w == "oral")
                {
                    out = "mouth";
                }
                else if (acronym || (raw.size() == 1 && is_upper(raw[0])))
                {
                    out = raw; // RC flash, S huge, Pregnant F
                }
                else
                {
                    out = w;
                }
                words.push_back(out);
            }
            // Form_Felian -> "Felian form"
            if (words.size() > 1 && words.front() == "form")
            {
                words.erase(words.begin());
                words.push_back("form");
            }
            std::string label;
            for (const std::string& w : words)
            {
                if (!label.empty())
                {
                    label += ' ';
                }
                label += w;
            }
            if (label.empty())
            {
                return std::string(name);
            }
            return capitalize_first(label);
        }

        const char* side_suffix(MorphSide s) { return s == MorphSide::Left ? " (left)" : s == MorphSide::Right ? " (right)" : ""; }
    } // namespace

    std::vector<std::string> morph_words(std::string_view name) { return parse(name).words; }

    MorphSort sort_morph(std::string_view name)
    {
        const Parsed p = parse(name);
        MorphSort out;
        out.side = p.side;
        out.label = label_of(p, name);
        out.category = "Other";
        out.randomize = false;
        for (const Rule& r : rules())
        {
            if (rule_matches(r, p.words))
            {
                out.category = r.category ? r.category : "";
                out.randomize = r.randomize;
                break;
            }
        }
        return out;
    }

    std::string morph_label(std::string_view name)
    {
        const MorphSort s = sort_morph(name);
        return s.label + side_suffix(s.side);
    }

    Side morph_side(std::string_view name)
    {
        const Side side = find_side(name).side;
        if (side != Side::None)
        {
            return side;
        }
        const MorphSide glued = parse(name).side;
        return glued == MorphSide::Left ? Side::Left : glued == MorphSide::Right ? Side::Right : Side::None;
    }

    std::string mirror_morph_name(std::string_view name)
    {
        std::string out = mirror_bone_name(name);
        if (!out.empty() || name.empty() || parse(name).side == MorphSide::None)
        {
            return out;
        }
        out.assign(name);
        char& c = out.back();
        switch (c)
        {
        case 'L': c = 'R'; break;
        case 'R': c = 'L'; break;
        case 'l': c = 'r'; break;
        case 'r': c = 'l'; break;
        default: return {};
        }
        return out;
    }

    int section_rank(std::string_view section)
    {
        static const char* const order[] = {"Head & Neck", "Face", "Torso", "Chest", "Hips & Legs", "Arms", "Muscles", "Body shape"};
        const std::string key = fold_case(section);
        for (size_t i = 0; i < std::size(order); ++i)
        {
            if (fold_case(order[i]) == key)
            {
                return static_cast<int>(i);
            }
        }
        if (key == "groin")
        {
            return 101;
        }
        if (key == "other")
        {
            return 102;
        }
        return 100;
    }

    std::vector<MorphGroupMorphs> body_morph_groups(const std::vector<std::string>& morphs, const MorphProfile& profile)
    {
        std::vector<MorphGroupMorphs> out = resolve_morph_groups(morphs, profile);
        for (MorphGroupMorphs& g : out)
        {
            // A profile group without "Section" goes where its name (or its first morph) points, else "Morphs".
            if (g.group.section.empty())
            {
                for (const std::string& name : {g.group.title, g.morphs.front()})
                {
                    const MorphSort s = sort_morph(name);
                    if (!s.category.empty() && s.category != "Other")
                    {
                        g.group.section = s.category;
                        break;
                    }
                }
            }
        }
        if (!profile.auto_groups)
        {
            return out;
        }
        std::set<std::string> claimed;
        std::set<std::string> titles;
        for (const MorphGroupMorphs& g : out)
        {
            titles.insert(fold_case(g.group.title));
            for (const std::string& m : g.morphs)
            {
                claimed.insert(fold_case(m));
            }
        }

        struct Pending
        {
            MorphGroupMorphs group;
            MorphSide side{MorphSide::None};
            bool has_left{false};
            bool has_right{false};
        };
        std::vector<Pending> autos;
        std::map<std::string, size_t> paired; // category + label -> index in autos, for left/right pairs
        for (const std::string& m : morphs)
        {
            if (profile.excluded(m) || claimed.count(fold_case(m)) > 0)
            {
                continue;
            }
            const MorphSort s = sort_morph(m);
            if (s.category.empty())
            {
                continue; // Advanced only
            }
            if (s.side != MorphSide::None)
            {
                const std::string key = s.category + "\n" + fold_case(s.label);
                const auto it = paired.find(key);
                if (it != paired.end())
                {
                    Pending& p = autos[it->second];
                    bool& has = s.side == MorphSide::Left ? p.has_left : p.has_right;
                    if (!has)
                    {
                        has = true;
                        p.group.morphs.push_back(m);
                        continue;
                    }
                }
            }
            Pending p;
            p.group.group.title = s.label;
            p.group.group.section = s.category;
            p.group.group.randomize = s.randomize;
            p.group.group.automatic = true;
            p.group.group.hint = "Placed in " + s.category + " by its name. \"MorphGroups\" in the game profile can move it.";
            p.group.morphs.push_back(m);
            p.side = s.side;
            p.has_left = s.side == MorphSide::Left;
            p.has_right = s.side == MorphSide::Right;
            if (s.side != MorphSide::None)
            {
                paired.emplace(s.category + "\n" + fold_case(s.label), autos.size());
            }
            autos.push_back(std::move(p));
        }

        for (Pending& p : autos)
        {
            std::string& title = p.group.group.title;
            if (p.group.morphs.size() == 1)
            {
                title += side_suffix(p.side); // one side only: say which
            }
            if (!titles.insert(fold_case(title)).second)
            {
                title += " (" + p.group.morphs.front() + ")"; // two morphs read the same: keep them apart
                titles.insert(fold_case(title));
            }
        }
        std::stable_sort(autos.begin(), autos.end(), [](const Pending& a, const Pending& b) {
            return section_rank(a.group.group.section) < section_rank(b.group.group.section);
        });
        for (Pending& p : autos)
        {
            out.push_back(std::move(p.group));
        }
        return out;
    }
} // namespace uuepbs
