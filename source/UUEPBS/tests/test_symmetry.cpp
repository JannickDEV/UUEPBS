// Host-side tests for the symmetry tools (core/symmetry): Mirror L to R / R to L,
// Randomize within limits, and that each of them is exactly one undo step.
#include "core/history.hpp"
#include "core/symmetry.hpp"

#include <cmath>
#include <cstdio>

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

static void set(EditSnapshot& s, const std::string& bone, double scale, double turn_y = 0.0, double move_x = 0.0)
{
    EditEntry e;
    e.bone = bone;
    e.edit.axis[0] = e.edit.axis[1] = e.edit.axis[2] = scale;
    e.edit.turn[1] = turn_y;
    e.edit.shift[0] = move_x;
    s.bones[fold_case(bone)] = e;
}

static const BoneEdit* get(const EditSnapshot& s, const std::string& bone)
{
    const auto it = s.bones.find(fold_case(bone));
    return it == s.bones.end() ? nullptr : &it->second.edit;
}

static const std::vector<std::string> kBones = {"pelvis", "spine_01", "thigh_l", "thigh_r", "calf_l", "calf_r", "upperarm_l", "upperarm_r",
                                                "hand_l", "Breast_L"}; // Breast_L has no partner

static void test_mirror()
{
    EditSnapshot s;
    set(s, "thigh_l", 1.3, 10.0, 2.0);
    set(s, "calf_r", 0.8);        // replaced: calf_l has no edit
    set(s, "upperarm_r", 1.1);    // replaced by upperarm_l's edit
    set(s, "upperarm_l", 1.2);
    set(s, "spine_01", 1.5);      // centre bone: untouched
    set(s, "Breast_L", 1.4);      // no partner on this skeleton: untouched
    s.morphs["belly_l"] = MorphEntry{"Belly_L", 0.4};
    s.morphs["belly_r"] = MorphEntry{"Belly_R", 0.9};
    s.morphs["nose_r"] = MorphEntry{"Nose_R", 0.3}; // cleared: Nose_L is unset
    const std::vector<std::string> morphs = {"Belly_L", "Belly_R", "Nose_L", "Nose_R", "Smile"};

    MirrorTable table; // empty = generic rule (only X flips)
    MirrorCount count;
    const EditSnapshot m = mirror_sides(s, kBones, morphs, Side::Left, table, &count);

    const BoneEdit* tr = get(m, "thigh_r");
    CHECK(tr && std::fabs(tr->axis[0] - 1.3) < 1e-9);
    CHECK(tr && std::fabs(tr->shift[0] + 2.0) < 1e-9);  // move flips across the centre
    CHECK(tr && std::fabs(tr->turn[1] + 10.0) < 1e-9);  // rotation mirrors (generic rule: Y and Z flip)
    CHECK(get(m, "thigh_l") && std::fabs(get(m, "thigh_l")->axis[0] - 1.3) < 1e-9); // source kept
    CHECK(get(m, "calf_r") == nullptr);
    CHECK(get(m, "upperarm_r") && std::fabs(get(m, "upperarm_r")->axis[0] - 1.2) < 1e-9);
    CHECK(get(m, "spine_01") && std::fabs(get(m, "spine_01")->axis[0] - 1.5) < 1e-9);
    CHECK(get(m, "Breast_L") != nullptr);
    CHECK(m.bones.find("thigh_r")->second.bone == "thigh_r"); // display spelling from the skeleton
    CHECK(m.morphs.count("belly_r") && std::fabs(m.morphs.at("belly_r").weight - 0.4) < 1e-12);
    CHECK(m.morphs.at("belly_r").name == "Belly_R");
    CHECK(m.morphs.count("nose_r") == 0);
    CHECK(count.bones == 3 && count.morphs == 2); // thigh_r, calf_r (cleared), upperarm_r; belly_r, nose_r

    // The other way round: right side wins.
    const EditSnapshot r = mirror_sides(s, kBones, morphs, Side::Right, table);
    CHECK(get(r, "thigh_l") == nullptr);
    CHECK(get(r, "calf_l") && std::fabs(get(r, "calf_l")->axis[0] - 0.8) < 1e-9);
    CHECK(get(r, "upperarm_l") && std::fabs(get(r, "upperarm_l")->axis[0] - 1.1) < 1e-9);
    CHECK(r.morphs.at("belly_l").weight == 0.9);
    CHECK(r.morphs.count("nose_l") && r.morphs.at("nose_l").name == "Nose_L");

    // Mirroring twice in the same direction changes nothing more.
    CHECK(same_edits(mirror_sides(m, kBones, morphs, Side::Left, table), m));
    // Side::None is a no-op.
    CHECK(same_edits(mirror_sides(s, kBones, morphs, Side::None, table), s));
}

static void test_randomize()
{
    EditSnapshot s;
    set(s, "thigh_l", 1.0, 15.0); // rotation is kept
    std::vector<RandomPart> parts(2);
    parts[0].bones = {"thigh_l", "thigh_r"};
    parts[1].bones = {"upperarm_l", "upperarm_r"};
    parts[1].spread = Spread::Keep;
    const std::vector<std::vector<std::string>> morphs = {{"Belly_L", "Belly_R"}, {"Smile"}};
    RandomLimits lim;
    lim.size = 0.2;
    lim.shape = 0.0;
    lim.morph_lo = 0.1;
    lim.morph_hi = 0.5;

    for (uint64_t seed = 1; seed <= 200; ++seed)
    {
        const EditSnapshot r = randomize_parts(s, parts, morphs, lim, seed);
        for (const auto& part : parts)
        {
            const BoneEdit* a = get(r, part.bones[0]);
            const BoneEdit* b = get(r, part.bones[1]);
            CHECK(a && b);
            if (!a || !b)
            {
                continue;
            }
            for (int i = 0; i < 3; ++i)
            {
                CHECK(a->axis[i] >= 0.8 - 1e-9 && a->axis[i] <= 1.2 + 1e-9);
                CHECK(a->axis[i] == b->axis[i]);       // both sides alike
                CHECK(a->axis[i] == a->axis[0]);       // shape 0 keeps proportions
            }
        }
        CHECK(get(r, "thigh_l")->turn[1] == 15.0);
        CHECK(get(r, "upperarm_r")->spread == Spread::Keep);
        const double w = r.morphs.at("belly_l").weight;
        CHECK(w >= 0.1 - 1e-12 && w <= 0.5 + 1e-12);
        CHECK(r.morphs.at("belly_r").weight == w);
    }
    // Deterministic per seed, different across seeds.
    CHECK(same_edits(randomize_parts(s, parts, morphs, lim, 7), randomize_parts(s, parts, morphs, lim, 7)));
    CHECK(!same_edits(randomize_parts(s, parts, morphs, lim, 7), randomize_parts(s, parts, morphs, lim, 8)));

    // Shape gives each axis its own value, still within size * (1 +/- shape).
    lim.shape = 0.1;
    bool differs = false;
    for (uint64_t seed = 1; seed <= 50; ++seed)
    {
        const BoneEdit* a = get(randomize_parts(s, parts, morphs, lim, seed), "thigh_r");
        differs |= a->axis[0] != a->axis[1];
        for (int i = 0; i < 3; ++i)
        {
            CHECK(a->axis[i] >= 0.8 * 0.9 - 1e-9 && a->axis[i] <= 1.2 * 1.1 + 1e-9);
        }
    }
    CHECK(differs);

    // Switches: parts off leaves bones alone, morphs off leaves morphs alone.
    RandomLimits only_morphs = lim;
    only_morphs.parts = false;
    const EditSnapshot om = randomize_parts(s, parts, morphs, only_morphs, 3);
    CHECK(get(om, "thigh_r") == nullptr && om.morphs.size() == 3);
    RandomLimits only_parts = lim;
    only_parts.morphs = false;
    CHECK(randomize_parts(s, parts, morphs, only_parts, 3).morphs.empty());

    // Limits are clamped.
    RandomLimits wild;
    wild.size = 5.0;
    wild.shape = -1.0;
    wild.morph_lo = 9.0;
    wild.morph_hi = -9.0;
    wild.clamp();
    CHECK(wild.size == 0.9 && wild.shape == 0.0 && wild.morph_lo == kMorphMin && wild.morph_hi == kMorphMax);
}

static void test_one_undo_step()
{
    using Clock = EditHistory::Clock;
    const Clock::time_point t0{};
    const auto at = [&](int ms) { return t0 + std::chrono::milliseconds(ms); };
    EditHistory h;
    EditSnapshot s;
    h.observe("player", s, false, at(0));

    set(s, "thigh_l", 1.2);
    h.observe("player", s, false, at(10)); // still settling when the button is pressed
    h.checkpoint("player", s, at(20));
    const EditSnapshot before = s;
    s = mirror_sides(s, kBones, {}, Side::Left, MirrorTable{});
    h.checkpoint("player", s, at(20));
    h.observe("player", s, false, at(2000));
    CHECK(h.undo_steps("player") == 2); // the slider edit, then the mirror

    const auto u = h.undo("player", s);
    CHECK(u && same_edits(*u, before)); // one Ctrl+Z takes back the whole mirror
    const auto u2 = h.undo("player", *u);
    CHECK(u2 && u2->bones.empty());
    const auto r = h.redo("player", *u2);
    const auto r2 = r ? h.redo("player", *r) : std::nullopt;
    CHECK(r2 && get(*r2, "thigh_r") != nullptr);

    // A checkpoint with nothing new adds no step.
    h.checkpoint("player", *r2, at(3000));
    CHECK(h.undo_steps("player") == 2);
}

int main()
{
    test_mirror();
    test_randomize();
    test_one_undo_step();
    if (g_fail)
    {
        std::printf("%d check(s) failed\n", g_fail);
        return 1;
    }
    std::printf("symmetry: all checks passed\n");
    return 0;
}
