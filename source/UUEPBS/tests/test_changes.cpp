// Host-side tests for undo/redo (core/history) and the change summaries behind the
// Simplified Panel's "Changed only" filter and the preset details (core/changes).
#include "core/changes.hpp"
#include "core/history.hpp"
#include "core/presets.hpp"
#include "core/rig_names.hpp"

#include <algorithm>
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

using Clock = EditHistory::Clock;
using std::chrono::milliseconds;

static void set_bone(EditSnapshot& s, const std::string& bone, double scale)
{
    EditEntry e;
    e.bone = bone;
    e.edit.axis[0] = e.edit.axis[1] = e.edit.axis[2] = scale;
    s.bones[fold_case(bone)] = e;
}

static double scale_of(const EditSnapshot& s, const std::string& bone)
{
    const auto it = s.bones.find(fold_case(bone));
    return it == s.bones.end() ? 1.0 : it->second.edit.axis[0];
}

static void test_history()
{
    const Clock::time_point t0{};
    const auto later = [&](int ms) { return t0 + milliseconds(ms); };
    EditHistory h;
    EditSnapshot s; // empty = game's own shape

    h.observe("player", s, false, later(0));
    CHECK(!h.can_undo("player") && !h.can_redo("player"));

    // One drag: many intermediate values while busy, one step once let go and settled.
    for (int i = 1; i <= 10; ++i)
    {
        set_bone(s, "thigh_l", 1.0 + 0.01 * i);
        h.observe("player", s, true, later(10 * i));
    }
    h.observe("player", s, false, later(120)); // let go, not settled yet
    CHECK(h.undo_steps("player") == 1);        // counts as a step already (Undo would take it back)
    h.observe("player", s, false, later(1000));
    CHECK(h.can_undo("player") && h.undo_steps("player") == 1 && !h.can_redo("player"));

    // Ctrl+wheel nudges in quick succession are one step too.
    for (int i = 1; i <= 5; ++i)
    {
        set_bone(s, "pelvis", 1.0 + 0.01 * i);
        h.observe("player", s, false, later(1000 + 100 * i));
    }
    h.observe("player", s, false, later(2000));
    CHECK(h.undo_steps("player") == 2);

    // Undo twice goes back to the start; redo replays.
    auto u = h.undo("player", s);
    CHECK(u && scale_of(*u, "pelvis") == 1.0 && std::abs(scale_of(*u, "thigh_l") - 1.10) < 1e-9);
    s = *u;
    h.observe("player", s, false, later(2100));
    u = h.undo("player", s);
    CHECK(u && u->bones.empty());
    s = *u;
    CHECK(!h.can_undo("player") && h.can_redo("player") && h.redo_steps("player") == 2);
    CHECK(!h.undo("player", s));

    auto r = h.redo("player", s);
    CHECK(r && std::abs(scale_of(*r, "thigh_l") - 1.10) < 1e-9 && scale_of(*r, "pelvis") == 1.0);
    s = *r;
    h.observe("player", s, false, later(3000));
    CHECK(h.can_redo("player") && h.redo_steps("player") == 1);

    // A new edit after an undo drops the redo branch.
    set_bone(s, "head", 0.9);
    h.observe("player", s, false, later(3100));
    CHECK(!h.can_redo("player"));
    CHECK(!h.redo("player", s));
    h.observe("player", s, false, later(4000));
    CHECK(h.undo_steps("player") == 2);

    // Undo right after a change that has not settled: that change is undone first.
    set_bone(s, "head", 0.8);
    h.observe("player", s, false, later(4010));
    u = h.undo("player", s);
    CHECK(u && std::abs(scale_of(*u, "head") - 0.9) < 1e-9);
    s = *u;

    // Neutral entries do not count as a change (a bone dragged back to 1.0x).
    EditSnapshot with_neutral = s;
    set_bone(with_neutral, "spine_01", 1.0);
    CHECK(same_edits(s, with_neutral));
    // Morphs are part of a step; a morph set to 0 is a change (it overrides the game).
    EditSnapshot morphed = s;
    morphed.morphs["belly"] = MorphEntry{"Belly", 0.0};
    CHECK(!same_edits(s, morphed));

    // Characters have separate histories.
    EditSnapshot npc;
    h.observe("0x1234", npc, false, later(5000));
    set_bone(npc, "head", 1.3);
    h.observe("0x1234", npc, false, later(5500));
    h.observe("0x1234", npc, false, later(6000));
    CHECK(h.undo_steps("0x1234") == 1);
    const size_t player_steps = h.undo_steps("player");
    u = h.undo("0x1234", npc);
    CHECK(u && u->bones.empty() && h.undo_steps("player") == player_steps);

    // Released characters lose their history; the cap holds.
    h.retain({"player"});
    CHECK(!h.can_undo("0x1234") && !h.can_redo("0x1234"));
    EditHistory capped;
    EditSnapshot c;
    capped.observe("player", c, false, t0);
    for (int i = 1; i <= static_cast<int>(EditHistory::kLimit) + 20; ++i)
    {
        set_bone(c, "pelvis", 1.0 + 0.001 * i);
        capped.observe("player", c, false, later(1000 * i));
        capped.observe("player", c, false, later(1000 * i + 500));
    }
    CHECK(capped.undo_steps("player") == EditHistory::kLimit);
}

static GroupBones group(const char* section, const char* title, std::vector<std::string> bones)
{
    GroupBones g;
    g.group.section = section;
    g.group.title = title;
    g.bones = std::move(bones);
    return g;
}

static void test_changes()
{
    BodyMap body;
    body.groups.push_back(group("Legs", "Thighs", {"thigh_l", "thigh_r"}));
    body.groups.push_back(group("Torso", "Waist", {"spine_01"}));
    body.groups.push_back(group("Torso", "Hips", {"pelvis", "thigh_l"})); // thigh_l is the Thighs' already

    MorphGroupMorphs mg;
    mg.group.title = "Belly";
    mg.morphs = {"Belly", "Belly_Soft"};
    const std::vector<MorphGroupMorphs> morph_groups{mg};

    // "Changed only": any bone edited (rotation/move count), or any morph set.
    EditSnapshot s;
    CHECK(!group_changed(body.groups[0], s.bones));
    set_bone(s, "Thigh_R", 1.2);
    CHECK(group_changed(body.groups[0], s.bones) && !group_changed(body.groups[1], s.bones));
    EditEntry turned;
    turned.bone = "spine_01";
    turned.edit.turn[2] = 10.0;
    s.bones["spine_01"] = turned;
    CHECK(group_changed(body.groups[1], s.bones));
    set_bone(s, "pelvis", 1.0); // neutral entry
    CHECK(!group_changed(body.groups[2], s.bones));
    CHECK(!morph_group_changed(mg, s.morphs));
    s.morphs["belly_soft"] = MorphEntry{"Belly_Soft", 0.0};
    CHECK(morph_group_changed(mg, s.morphs));

    // Preset details, from a real preset text.
    const std::string preset = R"({
      "format": "UUEPBS preset", "version": 3,
      "bones": {
        "thigh_l":  { "length": 1.15, "width": 1.15, "depth": 1.15, "children": "keep_size" },
        "thigh_r":  { "length": 1.15, "width": 1.15, "depth": 1.15, "children": "keep_size" },
        "pelvis":   { "length": 1, "width": 1, "depth": 1, "rotate": [0, 0, 5] },
        "tail_01":  { "scale": 2 },
        "wing_l":   { "scale": 0.5 },
        "head":     { "length": 1, "width": 1, "depth": 1 }
      },
      "morphs": { "Belly": 0.4, "Smile": 1, "Horns": 0.2 }
    })";
    EditBook book;
    MorphBook morphs;
    std::string msg;
    CHECK(PresetShelf::parse(preset, book, msg, &morphs));
    const std::vector<std::string> skeleton{"root", "pelvis", "spine_01", "thigh_l", "thigh_r", "head", "tail_01"};
    const std::vector<std::string> morph_names{"Belly", "Belly_Soft", "Smile"};
    const EditSummary d = summarize_edits(book, morphs, body, morph_groups, skeleton, morph_names);

    CHECK(d.bones == 5); // head is neutral
    CHECK(d.parts.size() == 2);
    if (d.parts.size() == 2)
    {
        CHECK(d.parts[0].title == "Thighs" && d.parts[0].section == "Legs" && d.parts[0].bones.size() == 2);
        CHECK(std::abs(d.parts[0].scale - 1.15) < 1e-9 && !d.parts[0].turn && !d.parts[0].shift);
        CHECK(d.parts[1].title == "Hips" && d.parts[1].bones.size() == 1 && d.parts[1].turn && d.parts[1].scale == 1.0);
    }
    CHECK(d.other_bones.size() == 1 && d.other_bones[0].title == "tail_01" && d.other_bones[0].scale == 2.0);
    CHECK(d.missing.size() == 1 && d.missing[0] == "wing_l");
    CHECK(d.morphs.size() == 3);
    for (const MorphChange& m : d.morphs)
    {
        if (m.name == "Belly")
        {
            CHECK(m.group == "Belly" && m.present && std::abs(m.weight - 0.4) < 1e-9);
        }
        else if (m.name == "Horns")
        {
            CHECK(!m.present && m.group.empty());
        }
        else
        {
            CHECK(m.name == "Smile" && m.present && m.group.empty());
        }
    }

    // Unknown skeleton / morph list (no character yet): nothing is reported missing.
    const EditSummary blind = summarize_edits(book, morphs, BodyMap{}, {}, {}, {});
    CHECK(blind.missing.empty() && blind.parts.empty() && blind.other_bones.size() == 5);
    CHECK(std::all_of(blind.morphs.begin(), blind.morphs.end(), [](const MorphChange& m) { return m.present; }));

    // A bones-only preset lists no morphs (loading it gives every morph back to the game).
    EditBook only;
    MorphBook none;
    CHECK(PresetShelf::parse(R"({"bones": {"pelvis": {"scale": 1.1}}})", only, msg, &none));
    const EditSummary b = summarize_edits(only, none, body, morph_groups, skeleton, morph_names);
    CHECK(b.morphs.empty() && b.parts.size() == 1 && b.parts[0].title == "Hips");
}

int main()
{
    test_history();
    test_changes();
    if (g_fail == 0)
    {
        std::printf("ALL TESTS PASSED\n");
        return 0;
    }
    std::printf("%d FAILURE(S)\n", g_fail);
    return 1;
}
