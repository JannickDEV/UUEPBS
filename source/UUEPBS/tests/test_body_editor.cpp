// Host-side tests for the character-editor additions: Muscle (girth) sliders on the Character
// Scale tab and the Body tab's bone-built "universal morphs" (core/bone_parts, core/proportions).
// Run from tests/ (reads roku_ref.txt).
#include "core/body_groups.hpp"
#include "core/bone_parts.hpp"
#include "core/presets.hpp"
#include "core/proportions.hpp"
#include "core/sculpt.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

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

struct Skel
{
    std::vector<std::string> names;
    std::vector<int32_t> parents;
    std::vector<Xform> local;
};

static Skel load()
{
    Skel s;
    std::ifstream f("roku_ref.txt");
    std::string line;
    while (std::getline(f, line))
    {
        std::istringstream in(line);
        std::string n;
        int p;
        Xform x = xf::identity();
        in >> n >> p >> x.rot[0] >> x.rot[1] >> x.rot[2] >> x.rot[3] >> x.pos[0] >> x.pos[1] >> x.pos[2] >> x.scl[0] >> x.scl[1] >> x.scl[2];
        const double len = std::sqrt(x.rot[0] * x.rot[0] + x.rot[1] * x.rot[1] + x.rot[2] * x.rot[2] + x.rot[3] * x.rot[3]);
        for (double& q : x.rot)
            q /= len;
        s.names.push_back(n);
        s.parents.push_back(p);
        s.local.push_back(x);
    }
    return s;
}

static int index_of(const Skel& s, const char* name)
{
    for (size_t i = 0; i < s.names.size(); ++i)
        if (s.names[i] == name)
            return (int)i;
    return -1;
}

static std::vector<Xform> fk(const Skel& s)
{
    std::vector<Xform> cs(s.local.size());
    for (size_t i = 0; i < s.local.size(); ++i)
        cs[i] = s.parents[i] < 0 ? s.local[i] : xf::compose(s.local[i], cs[s.parents[i]]);
    return cs;
}

static const std::vector<std::string>* part_bones(const BodyMap& m, const char* id)
{
    for (const PartBones& p : m.parts)
        if (p.id == id)
            return &p.bones;
    return nullptr;
}

static bool has(const std::vector<std::string>* v, const char* name)
{
    return v && std::find(v->begin(), v->end(), name) != v->end();
}

static bool near(double a, double b) { return std::fabs(a - b) < 1e-9; }

int main()
{
    // ---- bone parts: name matching ------------------------------------------------------
    {
        const std::vector<std::string> names = {
            "root",        "pelvis",      "Penis_01",      "Penis_02",   "Penis_03",        "Penis_Glans", "Testicle_L", "Testicle_R",
            "ball_l",      "ball_r",      "foot_l",        "cocktail_glass", "DEF-penis.001", "Shaft2",     "CockHead",   "Labia_L",
            "Labia_R",     "Scrotum",     "c_eye_l",       "ik_hand_gun"};
        const auto parts = resolve_bone_parts(names);
        BodyMap m;
        m.parts = parts;
        const auto* penis = part_bones(m, "penis");
        const auto* glans = part_bones(m, "glans");
        const auto* balls = part_bones(m, "testicles");
        const auto* vulva = part_bones(m, "vulva");
        CHECK(penis && penis->size() == 5);
        CHECK(has(penis, "Penis_01") && has(penis, "Penis_03") && has(penis, "DEF-penis.001") && has(penis, "Shaft2"));
        CHECK(!has(penis, "Penis_Glans") && !has(penis, "cocktail_glass") && !has(penis, "CockHead"));
        CHECK(glans && glans->size() == 2 && has(glans, "Penis_Glans") && has(glans, "CockHead"));
        // UE mannequin ball_l / ball_r are toes, not testicles.
        CHECK(balls && balls->size() == 3 && has(balls, "Testicle_L") && has(balls, "Scrotum") && !has(balls, "ball_l"));
        CHECK(vulva && vulva->size() == 2);
        // Daz Genesis face bones are not genitals (Afterparty: "nasolabial" holds "labia").
        const auto daz = resolve_bone_parts({"lNasolabialLower", "rNasolabialMouthCorner", "lLipNasolabialCrease", "Labia_L"});
        BodyMap md;
        md.parts = daz;
        const auto* dv = part_bones(md, "vulva");
        CHECK(dv && dv->size() == 1 && has(dv, "Labia_L"));

        // Bones a Body tab group already took are left alone.
        std::vector<uint8_t> taken(names.size(), 0);
        taken[2] = 1;
        BodyMap m2;
        m2.parts = resolve_bone_parts(names, nullptr, &taken);
        CHECK(!has(part_bones(m2, "penis"), "Penis_01"));

        // Daz Genesis 8 as Afterparty rigs it: no nipple or glans bone by name. The ends of the breast chains are the
        // nipples, the last bone of the penis chain is the glans.
        {
            const std::vector<std::string> dn = {"hip", "pelvis", "abdomenLower", "chestLower", "chestUpper", "Breast-Root", "lBreastOuter",
                                                 "lBreastInner", "lBreastCenter", "rBreastOuter", "rBreastInner", "rBreastCenter", "shaftRoot",
                                                 "shaft1", "shaft2", "shaft3", "shaft4", "lThighBend", "lShin", "lFoot", "rThighBend", "rShin",
                                                 "rFoot"};
            const std::vector<int32_t> dp = {-1, 0, 0, 2, 3, 4, 5, 6, 7, 5, 9, 10, 1, 12, 13, 14, 15, 1, 17, 18, 1, 20, 21};
            const BodyMap dm = resolve_body_groups(dn, dp, BoneDictionary::builtin());
            const std::vector<std::string>* nipples = nullptr;
            for (const GroupBones& gb : dm.groups)
            {
                if (gb.group.title == "Nipples")
                {
                    nipples = &gb.bones;
                }
            }
            CHECK(nipples && nipples->size() == 2 && has(nipples, "lBreastCenter") && has(nipples, "rBreastCenter"));
            const auto* dg = part_bones(dm, "glans");
            const auto* dpn = part_bones(dm, "penis");
            CHECK(dg && dg->size() == 1 && has(dg, "shaft4"));
            CHECK(dpn && !has(dpn, "shaft4") && has(dpn, "shaft1"));
            CHECK(!scale_slider_bones(*find_scale_slider("nipple_length"), dm).empty());
            CHECK(!scale_slider_bones(*find_scale_slider("glans_size"), dm).empty());
        }
        // A one-bone breast (boob_l with nothing below) has no tip of its own: no Nipples group made up.
        {
            const std::vector<std::string> bn = {"root", "pelvis", "spine_03", "boob_l", "boob_r"};
            const std::vector<int32_t> bp = {-1, 0, 1, 2, 2};
            for (const GroupBones& gb : resolve_body_groups(bn, bp, BoneDictionary::builtin()).groups)
            {
                CHECK(gb.group.title != "Nipples");
            }
        }

        // Nothing anatomical on the reference skeleton: no parts at all.
        const Skel roku = load();
        CHECK(roku.names.size() == 184);
        CHECK(resolve_body_groups(roku.names, roku.parents, BoneDictionary::builtin()).parts.empty());
    }

    // ---- real morph targets win ---------------------------------------------------------
    {
        const std::vector<const char*> balls = {"ball", "testicle", "testes", "testis", "scrotum", "nut"};
        const std::vector<const char*> penis = {"penis", "cock", "dick", "phallus", "shaft"};
        const std::vector<const char*> length = {"length", "long", "longer", "short", "shorter"};
        CHECK(part_covered_by_morph("mGen_Balls", balls, {}));
        CHECK(part_covered_by_morph("mGen_Balls2", balls, {}));
        CHECK(part_covered_by_morph("Morph_TesticlesEnl", balls, {}));
        CHECK(!part_covered_by_morph("Morph_BallsLift", balls, {}));
        CHECK(!part_covered_by_morph("mGen_Flaccid", penis, {}));
        CHECK(part_covered_by_morph("Penis_Length", penis, length));
        CHECK(!part_covered_by_morph("Penis_Length", penis, {})); // a length morph is not a size morph
        CHECK(part_covered_by_morph("Morph_PenisEnl", penis, {}));
        CHECK(!part_covered_by_morph("Morph_BreastEnl", penis, {}));

        // The sliders themselves, with the morphs of the reported mesh.
        const std::vector<std::string> mesh = {"Morph_BreastEnl", "mGen_Flaccid", "mGen_Glans", "mGen_Balls", "mGen_Balls2", "Form_Felian"};
        auto covered = [&](const char* id) { return scale_slider_covered(*find_scale_slider(id), mesh); };
        CHECK(covered("glans_size") && covered("testicle_size"));
        CHECK(!covered("penis_size") && !covered("penis_length") && !covered("penis_girth") && !covered("vulva_size"));
        CHECK(!covered("muscle")); // Character Scale sliders never hide
    }

    // ---- Height range from config.lua (HeightMin / HeightMax) ------------------------------
    {
        const ScaleSlider* h = find_scale_slider("height");
        CHECK(h && scale_lo(*h) == 0.5 && scale_hi(*h) == 2.0); // default
        EditBook book;
        set_scale_value(book, "height", 3.5);
        CHECK(std::fabs(scale_value(book, "height") - 2.0) < 1e-9); // clamped to the default max
        set_height_range(0.25, 6.0);
        CHECK(scale_lo(*h) == 0.25 && scale_hi(*h) == 6.0);
        set_scale_value(book, "height", 3.5);
        CHECK(std::fabs(scale_value(book, "height") - 3.5) < 1e-9);
        double world = 0.0;
        apply_proportions(book, BodyMap{}, world);
        CHECK(std::fabs(world - 3.5) < 1e-9);
        set_scale_value(book, "height", 0.1);
        CHECK(std::fabs(scale_value(book, "height") - 0.25) < 1e-9);
        const ScaleSlider* head = find_scale_slider("head");
        CHECK(head && scale_lo(*head) == 0.2 && scale_hi(*head) == 5.0); // other sliders keep their range
        set_height_range(0.0, 100.0); // kept within 0.05..20
        CHECK(scale_lo(*h) == kHeightRangeMin && scale_hi(*h) == kHeightRangeMax);
        set_height_range(1.5, 0.5); // min above 1 / max below 1: kept on their side of 1
        CHECK(scale_lo(*h) < 1.0 && scale_hi(*h) > 1.0);
        set_height_range(std::nan(""), std::nan("")); // garbage = default
        CHECK(scale_lo(*h) == 0.5 && scale_hi(*h) == 2.0);
    }

    // ---- RaceMenu-style ranges: typed values go far past the track ---------------------------
    {
        EditBook book;
        set_scale_value(book, "tail_length", 12.0);
        CHECK(near(scale_value(book, "tail_length"), 12.0));
        set_scale_value(book, "tail_length", 50.0);
        CHECK(near(scale_value(book, "tail_length"), kTypedFactorMax));
        set_scale_value(book, "head", 0.001);
        CHECK(near(scale_value(book, "head"), kTypedFactorMin));
        set_scale_value(book, "breast_lift", 7.0);
        CHECK(near(scale_value(book, "breast_lift"), 7.0));
        set_scale_value(book, "breast_lift", -15.0);
        CHECK(near(scale_value(book, "breast_lift"), -kTypedOffsetMax));
        // Height and the camera keep their own ranges
        set_scale_value(book, "camera_distance", 9.0);
        CHECK(near(scale_value(book, "camera_distance"), scale_hi(*find_scale_slider("camera_distance"))));
        set_height_range(std::nan(""), std::nan(""));
        set_scale_value(book, "height", 9.0);
        CHECK(near(scale_value(book, "height"), kHeightDefaultMax));
    }

    // ---- Chain sliders: tail / ear / finger length and thickness along the whole chain --------------
    {
        Skel s = load();
        BodyMap map = resolve_body_groups(s.names, s.parents, BoneDictionary::builtin());
        measure_long_axes(map, s.names, s.parents, fk(s));
        const GroupBones* tail = nullptr;
        const GroupBones* hands = nullptr;
        for (const GroupBones& gb : map.groups)
        {
            if (gb.group.title == "Tail")
                tail = &gb;
            if (gb.group.title == "Hands")
                hands = &gb;
        }
        CHECK(tail && tail->bones.size() == 1 && tail->bones[0] == "tail_01" && tail->chain.size() == 8 && tail->group.section == "Tail");
        CHECK(tail && tail->chain_parent.size() == tail->chain.size() && tail->chain_parent[0] == -1 && tail->chain_parent[7] == 6);
        // the hands' chain: the fingers, never another group's bones
        CHECK(hands && std::find(hands->chain.begin(), hands->chain.end(), "index_03_l") != hands->chain.end());
        CHECK(hands && reach_bones(*hands, Reach::Below).size() + hands->bones.size() == hands->chain.size());
        CHECK(hands && std::find(hands->chain.begin(), hands->chain.end(), "lowerarm_l") == hands->chain.end());
        CHECK(scale_slider_bones(*find_scale_slider("tail_length"), map).size() == 8);
        CHECK(scale_slider_bones(*find_scale_slider("tail_root"), map).size() == 1);

        const std::vector<Xform> original = fk(s);
        auto path = [&](const std::vector<Xform>& p, const char* from, const char* to) {
            // length along the chain from `from` down to `to`
            double len = 0;
            for (int i = index_of(s, to); i >= 0 && i != index_of(s, from); i = s.parents[i])
            {
                const int q = s.parents[i];
                double d = 0;
                for (int a = 0; a < 3; ++a)
                    d += (p[i].pos[a] - p[q].pos[a]) * (p[i].pos[a] - p[q].pos[a]);
                len += std::sqrt(d);
            }
            return len;
        };
        auto run = [&](const EditBook& book) {
            RigSculptor rig;
            rig.set_skeleton(s.names, s.parents);
            double world = 1.0;
            rig.bind(apply_proportions(book, map, world), 1, world);
            std::vector<Xform> pose = original;
            rig.apply(pose.data(), (int)pose.size());
            return pose;
        };
        const double tail0 = path(original, "tail_01", "tail_08");
        const int tip = index_of(s, "tail_07");
        const int tl = long_axis_of(map, "tail_07");
        auto cross = [&](const std::vector<Xform>& p) { return p[tip].scl[(tl + 1) % 3] / original[tip].scl[(tl + 1) % 3]; };

        EditBook book;
        set_scale_value(book, "tail_length", 1.5);
        std::vector<Xform> p = run(book);
        std::printf("tail length 1.5: tail %.2f -> %.2f cm, tip thickness x%.3f\n", tail0, path(p, "tail_01", "tail_08"), cross(p));
        CHECK(std::fabs(path(p, "tail_01", "tail_08") / tail0 - 1.5) < 1e-3 && std::fabs(cross(p) - 1.0) < 1e-6);

        // Tail size (the Body tab row: tail_01, "scale children too") and Tail length together: 1.2 x 1.5 long, 1.2 thick,
        // not 1.5 compounded down eight bones.
        BoneEdit size;
        size.axis[0] = size.axis[1] = size.axis[2] = 1.2;
        size.spread = Spread::Chain;
        book["tail_01"] = EditEntry{"tail_01", size};
        p = run(book);
        std::printf("tail size 1.2 + length 1.5: tail x%.3f, tip thickness x%.3f\n", path(p, "tail_01", "tail_08") / tail0, cross(p));
        CHECK(std::fabs(path(p, "tail_01", "tail_08") / tail0 - 1.8) < 1e-3 && std::fabs(cross(p) - 1.2) < 1e-6);
        // and size alone still scales the whole tail through its root, as before
        book.erase(scale_key("tail_length"));
        p = run(book);
        CHECK(std::fabs(path(p, "tail_01", "tail_08") / tail0 - 1.2) < 1e-3 && std::fabs(cross(p) - 1.2) < 1e-6);

        // Tail thickness: same length, thicker all the way.
        EditBook thick;
        set_scale_value(thick, "tail_thickness", 1.4);
        p = run(thick);
        CHECK(std::fabs(path(p, "tail_01", "tail_08") / tail0 - 1.0) < 1e-6 && std::fabs(cross(p) - 1.4) < 1e-6);

        // Finger length: longer fingers, the hand keeps its size and place.
        EditBook fingers;
        set_scale_value(fingers, "finger_length", 1.4);
        p = run(fingers);
        const int hand = index_of(s, "hand_l");
        const double f0 = path(original, "index_01_l", "index_03_l"), f1 = path(p, "index_01_l", "index_03_l");
        double drift = 0;
        for (int a = 0; a < 3; ++a)
            drift = std::max(drift, std::fabs(p[hand].scl[a] - original[hand].scl[a]) + std::fabs(p[hand].pos[a] - original[hand].pos[a]));
        std::printf("finger length 1.4: index finger %.2f -> %.2f cm, hand drift %.3g\n", f0, f1, drift);
        CHECK(std::fabs(f1 / f0 - 1.4) < 1e-3 && drift < 1e-6);

        // Turn sliders: the measured directions decide the axis, mirrored on both sides.
        auto at = [&](const std::vector<Xform>& q, const char* bone) { return q[index_of(s, bone)]; };
        auto apart = [&](const std::vector<Xform>& q, const char* l, const char* r) {
            double d = 0;
            for (int a = 0; a < 3; ++a)
                d += (at(q, l).pos[a] - at(q, r).pos[a]) * (at(q, l).pos[a] - at(q, r).pos[a]);
            return std::sqrt(d);
        };
        // Roku's reference rig: X = left, +Y = forward (the tail runs to -Y), Z = up.
        auto pitch = [&](const std::vector<Xform>& q, const char* from, const char* to) { // forward (0) towards up (+90)
            const double dy = at(q, to).pos[1] - at(q, from).pos[1], dz = at(q, to).pos[2] - at(q, from).pos[2];
            return std::atan2(dz, dy) * 180.0 / 3.14159265358979323846;
        };
        auto heading = [&](const std::vector<Xform>& q, const char* from, const char* to, double left) { // forward (0) towards out (+90)
            const double dx = at(q, to).pos[0] - at(q, from).pos[0], dy = at(q, to).pos[1] - at(q, from).pos[1];
            return std::atan2(left * dx, dy) * 180.0 / 3.14159265358979323846;
        };
        EditBook perky;
        set_scale_value(perky, "breast_perkiness", 1.0);
        p = run(perky);
        const double tilt_l = pitch(p, "boob_l", "nipp_l") - pitch(original, "boob_l", "nipp_l");
        const double tilt_r = pitch(p, "boob_r", "nipp_r") - pitch(original, "boob_r", "nipp_r");
        std::printf("breast perkiness +1: breasts tilted up %.1f / %.1f deg\n", tilt_l, tilt_r);
        CHECK(tilt_l > 8.0 && std::fabs(tilt_l - tilt_r) < 0.5);
        // Breast angle: the breasts' heading (boob -> nipple, seen from above) turns outward on both sides.
        EditBook angle;
        set_scale_value(angle, "breast_angle", 1.0);
        p = run(angle);
        const double turn_l = heading(p, "boob_l", "nipp_l", 1.0) - heading(original, "boob_l", "nipp_l", 1.0);
        const double turn_r = heading(p, "boob_r", "nipp_r", -1.0) - heading(original, "boob_r", "nipp_r", -1.0);
        std::printf("breast angle +1: breasts turned out %.1f / %.1f deg\n", turn_l, turn_r);
        CHECK(turn_l > 8.0 && std::fabs(turn_l - turn_r) < 0.5);
        EditBook spacing;
        set_scale_value(spacing, "nipple_spacing", 1.0);
        p = run(spacing);
        CHECK(apart(p, "nipp_l", "nipp_r") < apart(original, "nipp_l", "nipp_r") - 0.3);
        // Ear angle: the ear (base -> tip) leans from up towards out.
        auto elevation = [&](const std::vector<Xform>& q, double left) {
            const double dx = at(q, "c_ear_02_l").pos[0] - at(q, "c_ear_01_l").pos[0], dz = at(q, "c_ear_02_l").pos[2] - at(q, "c_ear_01_l").pos[2];
            return std::atan2(dz, left * dx) * 180.0 / 3.14159265358979323846;
        };
        EditBook flare;
        set_scale_value(flare, "ear_angle", 1.0);
        p = run(flare);
        std::printf("ear angle +1: ear elevation %.1f -> %.1f deg\n", elevation(original, 1.0), elevation(p, 1.0));
        CHECK(elevation(p, 1.0) < elevation(original, 1.0) - 8.0);

        // Ear length: the ear's tip moves out along the ear.
        EditBook ears;
        set_scale_value(ears, "ear_length", 1.5);
        p = run(ears);
        CHECK(std::fabs(path(p, "c_ear_01_l", "c_ear_02_l") / path(original, "c_ear_01_l", "c_ear_02_l") - 1.5) < 1e-3);

        // ---- Touched by Dibella's sliders ----
        // Breast gravity: lower and pointing down, both sides alike (one slider, two effects).
        EditBook gravity;
        set_scale_value(gravity, "breast_gravity", 1.0);
        p = run(gravity);
        const double sag_l = pitch(p, "boob_l", "nipp_l") - pitch(original, "boob_l", "nipp_l");
        const double sag_r = pitch(p, "boob_r", "nipp_r") - pitch(original, "boob_r", "nipp_r");
        std::printf("breast gravity +1: breasts tilted %.1f / %.1f deg, nipple %.2f cm lower\n", sag_l, sag_r,
                    at(original, "nipp_l").pos[2] - at(p, "nipp_l").pos[2]);
        CHECK(sag_l < -6.0 && std::fabs(sag_l - sag_r) < 0.5 && at(p, "nipp_l").pos[2] < at(original, "nipp_l").pos[2] - 1.0);
        CHECK(scale_slider_bones(*find_scale_slider("breast_gravity"), map).size() == 2);

        // Lower back arch: the waist leans back and the rib cage forward again, so the chest stays upright and the
        // upper body sits a little further back.
        EditBook arch;
        set_scale_value(arch, "lower_back_arch", 1.0);
        p = run(arch);
        const double upright0 = pitch(original, "spine_03", "neck_01"), upright1 = pitch(p, "spine_03", "neck_01");
        const double back = at(original, "neck_01").pos[1] - at(p, "neck_01").pos[1];
        const double bend = pitch(p, "spine_01", "spine_02") - pitch(original, "spine_01", "spine_02");
        std::printf("lower back arch +1: lower spine turned %.1f deg, chest %.2f -> %.2f deg, neck %.2f cm further back\n", bend, upright0,
                    upright1, back);
        CHECK(std::fabs(upright1 - upright0) < 0.5 && back > 0.5 && std::fabs(bend) > 6.0);

        // Butt stretch: the butt bones scale top to bottom only.
        EditBook stretch;
        set_scale_value(stretch, "butt_stretch", 1.4);
        double unused = 1.0;
        const EditBook folded = apply_proportions(stretch, map, unused);
        const BoneFrame bf = frame_of(map, "butt_l");
        const auto butt = folded.find("butt_l");
        CHECK(bf.up != 0 && butt != folded.end());
        if (bf.up != 0 && butt != folded.end())
            for (int a = 0; a < 3; ++a)
                CHECK(near(butt->second.edit.axis[a], a == std::abs(bf.up) - 1 ? 1.4 : 1.0));

        // Nipple width: thicker, same length; arm and leg thickness: whole limbs thicker, same length, hands stay.
        EditBook wide;
        set_scale_value(wide, "nipple_width", 1.5);
        set_scale_value(wide, "arm_thickness", 1.3);
        set_scale_value(wide, "leg_thickness", 1.2);
        const EditBook wf = apply_proportions(wide, map, unused);
        auto girth_ok = [&](const char* bone, double v) {
            const auto it = wf.find(bone);
            if (it == wf.end())
                return false;
            const int along = long_axis_of(map, bone);
            for (int a = 0; a < 3; ++a)
                if (!near(it->second.edit.axis[a], a == along ? 1.0 : v))
                    return false;
            return true;
        };
        CHECK(girth_ok("nipp_l", 1.5) && girth_ok("upperarm_l", 1.3) && girth_ok("lowerarm_r", 1.3) && girth_ok("thigh_l", 1.2) &&
              girth_ok("calf_r", 1.2) && wf.find("hand_l") == wf.end());
    }

    // ---- Carnal Instinct: both tails, ear tips, toes ------------------------------------------
    {
        std::ifstream f("fixtures/carnal_instinct_cache.txt");
        std::string line;
        std::vector<std::string> names;
        std::vector<int32_t> parents;
        while (std::getline(f, line))
        {
            std::istringstream l(line);
            std::string kind, item;
            std::getline(l, kind, '\t');
            if (kind == "names" && names.empty())
                while (std::getline(l, item, '\t'))
                    names.push_back(item);
            if (kind == "parents" && parents.empty())
                while (std::getline(l, item, '\t'))
                    parents.push_back(std::stoi(item));
        }
        const BodyMap map = resolve_body_groups(names, parents, BoneDictionary::builtin());
        const std::vector<std::string> tail = scale_slider_bones(*find_scale_slider("tail_length"), map);
        const std::vector<std::string> ear = scale_slider_bones(*find_scale_slider("ear_length"), map);
        const std::vector<std::string> toes = scale_slider_bones(*find_scale_slider("toe_length"), map);
        const std::vector<std::string> fingers = scale_slider_bones(*find_scale_slider("finger_length"), map);
        std::printf("Carnal Instinct: tail %zu bones, ears %zu, toes %zu, fingers %zu\n", tail.size(), ear.size(), toes.size(), fingers.size());
        CHECK(tail.size() == 8 + 13 && has(&tail, "FTail_NUB") && has(&tail, "Tail07"));
        CHECK(ear.size() == 4 && has(&ear, "Ear_Tip_L"));
        CHECK(toes.size() == 2 && has(&toes, "ball_l"));
        CHECK(fingers.size() == 30 && !has(&fingers, "hand_l"));
        // IK and virtual bones never join a chain
        for (const GroupBones& gb : map.groups)
            for (const std::string& b : gb.chain)
                CHECK(!rigging_helper(b));
        // Top bones: a group's bones with no other bone of the group above them. Moves and turns go on these only.
        auto index_in = [&](const std::string& b) {
            return static_cast<int>(std::find(names.begin(), names.end(), b) - names.begin());
        };
        size_t below_another = 0;
        for (const GroupBones& gb : map.groups)
        {
            CHECK(!gb.tops.empty() || gb.bones.empty());
            for (const std::string& b : gb.bones)
            {
                bool under = false;
                for (int p = parents[static_cast<size_t>(index_in(b))]; p >= 0; p = parents[static_cast<size_t>(p)])
                    under = under || has(&gb.bones, names[static_cast<size_t>(p)].c_str());
                CHECK(has(&gb.tops, b.c_str()) == !under);
                below_another += under ? 1 : 0;
            }
        }
        std::printf("Carnal Instinct: %zu group bones sit below another bone of their group\n", below_another);
    }

    // ---- slider table -------------------------------------------------------------------
    {
        size_t body = 0, muscle = 0;
        for (const ScaleSlider& sl : scale_sliders())
        {
            CHECK(find_scale_slider(sl.id) == &sl); // ids are unique
            if (is_camera_axes(sl.axes))
            {
                // Character Scale > Camera: no bones; Follow height is a 0/1 toggle (off = neutral)
                CHECK(!sl.body && sl.groups.empty() && sl.parts.empty() && sl.lo <= sl.neutral() && sl.hi > sl.neutral());
                continue;
            }
            CHECK(sl.lo < sl.neutral() && sl.hi > sl.neutral());
            CHECK(sl.offset() == (sl.amount != 0.0));
            for (const SliderEffect& more : sl.also)
                CHECK(sl.offset() && is_offset_axes(more.axes) && more.amount != 0.0 && !more.groups.empty());
            if (sl.body)
            {
                ++body;
                CHECK(!sl.morph_words.empty());
                CHECK(sl.parts.empty() != sl.groups.empty()); // bone parts (Groin) or Body tab groups (shape sliders)
                for (const char* p : sl.parts)
                    CHECK(find_bone_part(p) != nullptr);
            }
            else
            {
                CHECK(sl.parts.empty());
            }
            if (std::string(sl.section) == "Muscle")
            {
                ++muscle;
                CHECK(sl.axes == ScaleAxes::Girth && sl.spread == Spread::Keep);
            }
        }
        CHECK(body == 67 && muscle == 9);
    }

    // ---- Muscle: girth only, children keep their size -----------------------------------
    {
        Skel s = load();
        BodyMap map = resolve_body_groups(s.names, s.parents, BoneDictionary::builtin());
        // The reference rig does not run its bones along X everywhere: measure it.
        measure_long_axes(map, s.names, s.parents, fk(s));
        CHECK(long_axis_of(map, "neck_01") == 2 && long_axis_of(map, "spine_02") == 2); // spine and neck run along Z
        CHECK(long_axis_of(map, "lowerarm_l") == 2 && long_axis_of(map, "LowerArm_L") == 2);
        CHECK(long_axis_of(map, "no_such_bone") == 0);
        const int fa_len = long_axis_of(map, "lowerarm_l");
        auto girth_ok = [&](const BoneEdit& e, int len, double g, double l = 1.0) {
            for (int a = 0; a < 3; ++a)
                if (!near(e.axis[a], a == len ? l : g))
                    return false;
            return true;
        };
        EditBook book;
        set_scale_value(book, "forearm_girth", 1.3);
        double world = 0;
        EditBook folded = apply_proportions(book, map, world);
        CHECK(near(world, 1.0));
        const auto fa = folded.find("lowerarm_l");
        CHECK(fa != folded.end() && girth_ok(fa->second.edit, fa_len, 1.3) && fa->second.edit.spread == Spread::Keep);
        CHECK(folded.count("hand_l") == 0 && folded.count("upperarm_l") == 0);

        // A "scale children too" edit of the user's own becomes "keep children size", and both multiply.
        BoneEdit own;
        own.axis[0] = own.axis[1] = own.axis[2] = 1.1;
        own.spread = Spread::Chain;
        book["lowerarm_l"] = EditEntry{"lowerarm_l", own};
        folded = apply_proportions(book, map, world);
        CHECK(girth_ok(folded.at("lowerarm_l").edit, fa_len, 1.43, 1.1) && folded.at("lowerarm_l").edit.spread == Spread::Keep);
        for (int a = 0; a < 3; ++a) // child joints follow the user's 1.1, not the girth
            CHECK(a == fa_len ? folded.at("lowerarm_l").edit.joint[a] == 0.0 : near(folded.at("lowerarm_l").edit.joint[a], 1.1));
        book.erase("lowerarm_l");

        // Overall muscle and a single slider multiply; lengths stay as they are.
        set_scale_value(book, "muscle", 1.2);
        set_scale_value(book, "thigh_girth", 1.1);
        folded = apply_proportions(book, map, world);
        CHECK(girth_ok(folded.at("lowerarm_l").edit, fa_len, 1.3 * 1.2));
        CHECK(girth_ok(folded.at("thigh_r").edit, long_axis_of(map, "thigh_r"), 1.2 * 1.1));
        CHECK(girth_ok(folded.at("neck_01").edit, 2, 1.2));
        CHECK(folded.count("head") == 0 && folded.count("foot_l") == 0 && folded.count("hand_r") == 0);

        // Helper muscle groups scale uniformly with their own spread.
        BodyMap helpers = map;
        GroupBones biceps;
        biceps.group.title = "Biceps";
        biceps.group.spread = Spread::Chain;
        biceps.bones = {"bicep_l", "bicep_r"};
        helpers.groups.push_back(biceps);
        EditBook arms;
        set_scale_value(arms, "upper_arm_girth", 1.25);
        folded = apply_proportions(arms, helpers, world);
        CHECK(near(folded.at("bicep_l").edit.axis[0], 1.25) && near(folded.at("bicep_l").edit.axis[2], 1.25) &&
              folded.at("bicep_l").edit.spread == Spread::Chain);
        CHECK(girth_ok(folded.at("upperarm_l").edit, long_axis_of(map, "upperarm_l"), 1.25));
        CHECK(girth_ok(folded.at("upperarm_twist_01_l").edit, long_axis_of(map, "upperarm_twist_01_l"), 1.25)); // twist helpers come along

        // Lengths follow the measured axis too (without a measurement: X, as before).
        EditBook legs;
        set_scale_value(legs, "neck", 1.2);
        folded = apply_proportions(legs, map, world);
        CHECK(girth_ok(folded.at("neck_01").edit, 2, 1.0, 1.2));
        folded = apply_proportions(legs, resolve_body_groups(s.names, s.parents, BoneDictionary::builtin()), world);
        CHECK(girth_ok(folded.at("neck_01").edit, 0, 1.0, 1.2));

        // On the real pose: the forearm gets thicker, the hand keeps its size and stays where it was.
        RigSculptor rig;
        CHECK(rig.set_skeleton(s.names, s.parents));
        const std::vector<Xform> original = fk(s);
        EditBook only_forearm;
        set_scale_value(only_forearm, "forearm_girth", 1.3);
        rig.bind(apply_proportions(only_forearm, map, world), 1, world);
        std::vector<Xform> pose = original;
        rig.apply(pose.data(), (int)pose.size());
        const int hand = index_of(s, "hand_l"), forearm = index_of(s, "lowerarm_l"), finger = index_of(s, "index_03_l");
        double size_err = 0, moved = 0;
        for (int a = 0; a < 3; ++a)
        {
            size_err = std::max(size_err, std::fabs(pose[hand].scl[a] - original[hand].scl[a]));
            size_err = std::max(size_err, std::fabs(pose[finger].scl[a] - original[finger].scl[a]));
            moved += (pose[hand].pos[a] - original[hand].pos[a]) * (pose[hand].pos[a] - original[hand].pos[a]);
        }
        moved = std::sqrt(moved);
        // The wrist joint sits a little off the forearm's axis on this rig; girth must not push it sideways.
        const Xform wrist = xf::relative(original[hand], original[forearm]);
        double side = 0;
        for (int a = 0; a < 3; ++a)
            side += a == fa_len ? 0.0 : wrist.pos[a] * wrist.pos[a];
        side = std::sqrt(side);
        std::printf("forearm girth 1.3: hand size drift %.3g, wrist moved %.3f cm (off-axis offset %.2f cm)\n", size_err, moved, side);
        CHECK(size_err < 1e-6);
        CHECK(side > 5.0 && moved < 1e-6);
        CHECK(std::fabs(pose[forearm].scl[1] / original[forearm].scl[1] - 1.3) < 1e-6);
    }

    // ---- universal morphs: bones without a morph target ---------------------------------
    {
        Skel s = load();
        const int pelvis = index_of(s, "pelvis");
        auto add = [&](const char* n, int parent, double x) {
            Xform b = xf::identity();
            b.pos[0] = x;
            s.names.push_back(n);
            s.parents.push_back(parent);
            s.local.push_back(b);
            return (int)s.names.size() - 1;
        };
        const int p1 = add("Penis_01", pelvis, 4.0);
        const int p2 = add("Penis_02", p1, 3.0);
        const int p3 = add("Penis_03", p2, 3.0);
        const int tip = add("Penis_Glans", p3, 2.5);
        add("Testicle_L", pelvis, 3.0);
        add("Testicle_R", pelvis, 3.0);
        const BodyMap map = resolve_body_groups(s.names, s.parents, BoneDictionary::builtin());
        CHECK(part_bones(map, "penis") && part_bones(map, "penis")->size() == 3);
        CHECK(has(part_bones(map, "glans"), "Penis_Glans") && part_bones(map, "testicles")->size() == 2);
        CHECK(scale_slider_bones(*find_scale_slider("penis_size"), map).size() == 4);
        CHECK(scale_slider_bones(*find_scale_slider("vulva_size"), map).empty());

        EditBook book;
        set_scale_value(book, "penis_length", 1.5);
        set_scale_value(book, "testicle_size", 1.2);
        double world = 0;
        const EditBook folded = apply_proportions(book, map, world);
        for (const char* b : {"penis_01", "penis_02", "penis_03"})
        {
            CHECK(folded.count(b) && near(folded.at(b).edit.axis[0], 1.5) && near(folded.at(b).edit.axis[1], 1.0) &&
                  folded.at(b).edit.spread == Spread::Keep);
        }
        CHECK(folded.count("penis_glans") == 0);
        CHECK(near(folded.at("testicle_l").edit.axis[1], 1.2));

        // Longer shaft: the glans moves out by half the shaft length again and keeps its size.
        RigSculptor rig;
        CHECK(rig.set_skeleton(s.names, s.parents));
        const std::vector<Xform> original = fk(s);
        rig.bind(folded, 1, world);
        std::vector<Xform> pose = original;
        rig.apply(pose.data(), (int)pose.size());
        auto dist = [](const Xform& a, const Xform& b) {
            return std::sqrt((a.pos[0] - b.pos[0]) * (a.pos[0] - b.pos[0]) + (a.pos[1] - b.pos[1]) * (a.pos[1] - b.pos[1]) +
                             (a.pos[2] - b.pos[2]) * (a.pos[2] - b.pos[2]));
        };
        const double before = dist(original[tip], original[p1]);
        const double after = dist(pose[tip], pose[p1]);
        std::printf("penis length 1.5: root to glans %.3f -> %.3f\n", before, after);
        CHECK(std::fabs(after / before - 1.5) < 1e-3);
        for (int a = 0; a < 3; ++a)
            CHECK(std::fabs(pose[tip].scl[a] - original[tip].scl[a]) < 1e-6);

        // Presets and share data carry them like the other scale sliders.
        set_scale_value(book, "muscle", 1.15);
        const std::string text = PresetShelf::serialize(book);
        CHECK(text.find("\"penis_length\"") != std::string::npos && text.find("\"muscle\"") != std::string::npos);
        EditBook back;
        std::string message;
        CHECK(PresetShelf::parse(text, back, message));
        CHECK(near(scale_value(back, "penis_length"), 1.5) && near(scale_value(back, "muscle"), 1.15) && near(scale_value(back, "testicle_size"), 1.2));
        CHECK(near(scale_value(back, "penis_girth"), 1.0));
    }


    // ---- RaceMenu-style shape sliders: directions measured from the pose -----------------
    {
        Skel s = load();
        BodyMap map = resolve_body_groups(s.names, s.parents, BoneDictionary::builtin());
        const std::vector<Xform> original = fk(s);
        measure_long_axes(map, s.names, s.parents, original);
        std::printf("measured height %.1f\n", map.height);
        CHECK(map.height > 100.0 && map.height < 250.0);
        const BoneFrame bl = frame_of(map, "boob_l"), br = frame_of(map, "boob_r");
        CHECK(bl.up != 0 && bl.forward != 0 && bl.out != 0 && br.out != 0);
        CHECK(std::abs(bl.up) != std::abs(bl.forward) && std::abs(bl.up) != std::abs(bl.out) && std::abs(bl.out) != std::abs(bl.forward));

        auto run = [&](const EditBook& book) {
            double world = 1.0;
            RigSculptor rig;
            rig.set_skeleton(s.names, s.parents);
            rig.bind(apply_proportions(book, map, world), 1, world);
            std::vector<Xform> pose = original;
            rig.apply(pose.data(), (int)pose.size());
            return pose;
        };
        const int L = index_of(s, "boob_l"), R = index_of(s, "boob_r");
        auto horiz = [](const Xform& a, const Xform& b) { return std::hypot(a.pos[0] - b.pos[0], a.pos[1] - b.pos[1]); };

        EditBook book;
        set_scale_value(book, "breast_lift", 1.0);
        std::vector<Xform> pose = run(book);
        const double rise = pose[L].pos[2] - original[L].pos[2];
        std::printf("breast lift 1.0: +%.2f cm (left), +%.2f cm (right)\n", rise, pose[R].pos[2] - original[R].pos[2]);
        CHECK(std::fabs(rise - 0.02 * map.height) < 0.05 * map.height * 0.02 + 1e-6);
        CHECK(std::fabs((pose[R].pos[2] - original[R].pos[2]) - rise) < 1e-6);

        book.clear();
        set_scale_value(book, "breast_cleavage", 1.0);
        pose = run(book);
        std::printf("breast cleavage 1.0: spacing %.2f -> %.2f cm\n", horiz(original[L], original[R]), horiz(pose[L], pose[R]));
        CHECK(horiz(pose[L], pose[R]) < horiz(original[L], original[R]) - 1.0);
        set_scale_value(book, "breast_cleavage", -1.0);
        pose = run(book);
        CHECK(horiz(pose[L], pose[R]) > horiz(original[L], original[R]) + 1.0);

        book.clear();
        set_scale_value(book, "hip_width", 1.25);
        pose = run(book);
        const int tl = index_of(s, "thigh_l"), tr = index_of(s, "thigh_r");
        std::printf("hip width 1.25: thigh joints %.2f -> %.2f cm apart\n", horiz(original[tl], original[tr]), horiz(pose[tl], pose[tr]));
        CHECK(horiz(pose[tl], pose[tr]) > horiz(original[tl], original[tr]) * 1.1);
        CHECK(std::fabs(pose[tl].pos[2] - original[tl].pos[2]) < 0.5); // wider, not higher

        book.clear();
        set_scale_value(book, "breast_width", 1.3);
        double world = 0;
        const EditBook folded = apply_proportions(book, map, world);
        const int out_axis = std::abs(bl.out) - 1;
        for (int a = 0; a < 3; ++a)
            CHECK(near(folded.at("boob_l").edit.axis[a], a == out_axis ? 1.3 : 1.0));

        // Offsets save and load like the other sliders, negative values included.
        book.clear();
        set_scale_value(book, "breast_lift", -0.4);
        set_scale_value(book, "butt_lift", 0.0); // neutral: not stored
        CHECK(book.size() == 1 && near(scale_value(book, "breast_lift"), -0.4) && near(scale_value(book, "butt_lift"), 0.0));
        EditBook back;
        std::string message;
        CHECK(PresetShelf::parse(PresetShelf::serialize(book), back, message));
        CHECK(near(scale_value(back, "breast_lift"), -0.4));
    }

    // ---- priority: named morph > bone slider > cryptic morph ------------------------------
    {
        const std::vector<std::string> mesh = {"Morph_BreastEnl", "Morph_Buttocks", "Morph_HipsWide", "Morph_WaistNarr", "Breast_L_Lift",
                                               "Belly", "Morph_NippleHei", "Morph_SHuge", "Morph_SFinery"};
        CHECK(group_covered("Breasts", mesh) && group_covered("Butt", mesh) && group_covered("Belly", mesh));
        CHECK(!group_covered("Hips", mesh) && !group_covered("Thighs", mesh) && !group_covered("Nipples", mesh));
        CHECK(!group_covered("My custom group", mesh));
        auto covered = [&](const char* id) { return scale_slider_covered(*find_scale_slider(id), mesh); };
        CHECK(covered("hip_width") && covered("waist_width") && covered("breast_lift"));
        CHECK(!covered("breast_width") && !covered("breast_cleavage") && !covered("butt_lift") && !covered("back_width"));
        // Cryptic names (SHuge) never take a slider's place.
        CHECK(!scale_slider_covered(*find_scale_slider("breast_projection"), {"Morph_SHuge", "Morph_SReduce"}));
    }

    std::printf(g_fail ? "\n%d FAILURE(S)\n" : "\nALL TESTS PASSED\n", g_fail);
    return g_fail ? 1 : 0;
}
