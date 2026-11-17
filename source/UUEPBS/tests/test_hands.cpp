// Hand Contact: hands resting on the body follow body edits (two-bone arm IK), on a real skeleton.
//
//   test_hands <skeleton_cache.txt> <bone count> <touched bone> [<touched bone> ...]
//
// Builds the skeleton's reference pose, bends the right arm until the palm rests on the first touched bone that
// exists ("game pose"), edits the body and checks that after apply():
//   * Height (whole-pose scale) with shorter arms: the palm lands exactly where the scaled body puts the touched spot;
//   * a bigger chest ("keep children size", arms unscaled): the hand moves outward with it, the arm keeps its bone
//     lengths, bends the same way and the elbow stays an elbow;
//   * a hand far from the body (reference pose arm) is left exactly as without Hand Contact;
//   * Hand Contact off changes nothing; applying twice on an unrefreshed buffer gives the same result;
//   * arms are found by name on UE, Daz, Mixamo and VRoid style skeletons.
#include "core/hand_settings.hpp"
#include "core/sculpt.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace uuepbs;

static int g_fail = 0;
#define CHECK(cond, ...)                                                                                                                             \
    do                                                                                                                                               \
    {                                                                                                                                                \
        if (!(cond))                                                                                                                                 \
        {                                                                                                                                            \
            ++g_fail;                                                                                                                                \
            std::printf("FAIL %s:%d: ", __FILE__, __LINE__);                                                                                         \
            std::printf(__VA_ARGS__);                                                                                                                \
            std::printf("\n");                                                                                                                       \
        }                                                                                                                                            \
    } while (0)

struct Skel
{
    std::vector<std::string> names;
    std::vector<int32_t> parents;
    std::vector<Xform> local;
};

static std::vector<std::string> split_tabs(const std::string& s)
{
    std::vector<std::string> out;
    size_t at = 0;
    while (true)
    {
        const size_t t = s.find('\t', at);
        out.push_back(s.substr(at, t == std::string::npos ? std::string::npos : t - at));
        if (t == std::string::npos)
        {
            break;
        }
        at = t + 1;
    }
    return out;
}

static bool load(const char* path, size_t want, Skel& sk)
{
    std::ifstream in(path, std::ios::binary);
    std::string line, names, parents;
    bool pick = false;
    while (std::getline(in, line))
    {
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        const size_t tab = line.find('\t');
        if (tab == std::string::npos)
        {
            continue;
        }
        const std::string kind = line.substr(0, tab), rest = line.substr(tab + 1);
        if (kind == "skel")
        {
            pick = rest.rfind('#') != std::string::npos && rest.substr(rest.rfind('#') + 1) == std::to_string(want);
        }
        else if (pick && kind == "names")
        {
            names = rest;
        }
        else if (pick && kind == "parents")
        {
            parents = rest;
        }
        else if (pick && kind == "ref" && rest.size() > 2)
        {
            sk.names = split_tabs(names);
            for (const auto& p : split_tabs(parents))
            {
                sk.parents.push_back(std::stoi(p));
            }
            std::istringstream nums(rest);
            sk.local.assign(sk.names.size(), xf::identity());
            for (auto& x : sk.local)
            {
                nums >> x.rot[0] >> x.rot[1] >> x.rot[2] >> x.rot[3] >> x.pos[0] >> x.pos[1] >> x.pos[2] >> x.scl[0] >> x.scl[1] >> x.scl[2];
            }
            return sk.names.size() == want && sk.parents.size() == want;
        }
    }
    return false;
}

static std::vector<Xform> component_pose(const Skel& sk)
{
    std::vector<Xform> out(sk.local.size());
    for (size_t i = 0; i < sk.local.size(); ++i)
    {
        out[i] = sk.parents[i] < 0 ? sk.local[i] : xf::compose(sk.local[i], out[sk.parents[i]]);
        out[i].pos[3] = out[i].scl[3] = 0.0;
    }
    return out;
}

static int32_t find(const Skel& sk, const char* name)
{
    for (size_t i = 0; i < sk.names.size(); ++i)
    {
        if (fold_case(sk.names[i]) == name)
        {
            return static_cast<int32_t>(i);
        }
    }
    return -1;
}

// local rotation about the bone's own axis
static void bend(Skel& sk, const char* bone, double deg, int axis)
{
    const int32_t i = find(sk, bone);
    const double h = deg * 3.14159265358979323846 / 360.0;
    double r[4] = {0, 0, 0, std::cos(h)};
    r[axis] = std::sin(h);
    double q[4];
    xf::qmul(sk.local[i].rot, r, q);
    for (int a = 0; a < 4; ++a)
    {
        sk.local[i].rot[a] = q[a];
    }
}

static double dist(const Xform& a, const Xform& b)
{
    const double x = a.pos[0] - b.pos[0], y = a.pos[1] - b.pos[1], z = a.pos[2] - b.pos[2];
    return std::sqrt(x * x + y * y + z * z);
}

static double qdiff(const Xform& a, const Xform& b)
{
    const double d = std::fabs(a.rot[0] * b.rot[0] + a.rot[1] * b.rot[1] + a.rot[2] * b.rot[2] + a.rot[3] * b.rot[3]);
    return 1.0 - std::min(1.0, d);
}


static void sub(const double a[3], const double b[3], double out[3])
{
    for (int k = 0; k < 3; ++k)
    {
        out[k] = a[k] - b[k];
    }
}

static double len3(const double v[3])
{
    return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

static void palm_point(const std::vector<Xform>& pose, const ArmChain& arm, double out[3])
{
    for (int k = 0; k < 3; ++k)
    {
        out[k] = arm.palm >= 0 ? pose[arm.hand].pos[k] + 0.5 * (pose[arm.palm].pos[k] - pose[arm.hand].pos[k]) : pose[arm.hand].pos[k];
    }
}

// Turns the arm's upper arm (3 axes) and forearm (2 axes) until the palm is as close as it gets to `goal`.
static double place_palm(Skel& sk, const ArmChain& arm, const double goal[3])
{
    const Skel base = sk;
    const std::vector<Xform> comp = component_pose(base);
    // the chain from the upper arm down to the palm (or hand), parents first
    std::vector<int32_t> chain;
    for (int32_t b = arm.palm >= 0 ? arm.palm : arm.hand; b >= 0 && b != base.parents[arm.upper]; b = base.parents[b])
    {
        chain.insert(chain.begin(), b);
    }
    auto turned = [&](const Xform& local, const double deg[3]) {
        Xform x = local;
        for (int axis = 0; axis < 3; ++axis)
        {
            const double h = deg[axis] * 3.14159265358979323846 / 360.0;
            double r[4] = {0, 0, 0, std::cos(h)};
            r[axis] = std::sin(h);
            double q[4];
            xf::qmul(x.rot, r, q);
            std::memcpy(x.rot, q, sizeof q);
        }
        return x;
    };
    auto palm_at = [&](const double deg[5], double p[3]) {
        Xform parent = comp[base.parents[arm.upper]];
        Xform hand{}, palm{};
        for (const int32_t b : chain)
        {
            Xform local = base.local[b];
            if (b == arm.upper)
            {
                local = turned(local, deg);
            }
            else if (b == arm.lower)
            {
                const double fd[3] = {0.0, deg[3], deg[4]};
                local = turned(local, fd);
            }
            parent = xf::compose(local, parent);
            if (b == arm.hand)
            {
                hand = parent;
            }
            palm = parent;
        }
        for (int k = 0; k < 3; ++k)
        {
            p[k] = arm.palm >= 0 ? hand.pos[k] + 0.5 * (palm.pos[k] - hand.pos[k]) : hand.pos[k];
        }
    };
    double best = 1.0e30;
    double best_deg[5] = {0, 0, 0, 0, 0};
    double step = 30.0;
    double centre[5] = {0, 0, 0, 0, 0};
    for (int round = 0; round < 6; ++round)
    {
        const int span = round == 0 ? 6 : 2;
        int idx[5];
        for (idx[0] = -span; idx[0] <= span; ++idx[0])
            for (idx[1] = -span; idx[1] <= span; ++idx[1])
                for (idx[2] = -span; idx[2] <= span; ++idx[2])
                    for (idx[3] = -span; idx[3] <= span; ++idx[3])
                        for (idx[4] = -span; idx[4] <= span; ++idx[4])
                        {
                            double deg[5], p[3], d[3];
                            for (int k = 0; k < 5; ++k)
                            {
                                deg[k] = centre[k] + idx[k] * step;
                            }
                            palm_at(deg, p);
                            sub(p, goal, d);
                            const double dl = len3(d);
                            if (dl < best)
                            {
                                best = dl;
                                std::memcpy(best_deg, deg, sizeof best_deg);
                            }
                        }
        std::memcpy(centre, best_deg, sizeof centre);
        step /= 2.5;
    }
    sk = base;
    sk.local[arm.upper] = turned(base.local[arm.upper], best_deg);
    const double fd[3] = {0.0, best_deg[3], best_deg[4]};
    sk.local[arm.lower] = turned(base.local[arm.lower], fd);
    return best;
}

static int32_t first_found(const Skel& sk, const std::vector<std::string>& names)
{
    for (const auto& n : names)
    {
        const int32_t i = find(sk, fold_case(n).c_str());
        if (i >= 0)
        {
            return i;
        }
    }
    return -1;
}

static EditEntry entry(const std::string& bone, double x, double y, double z, Spread sp)
{
    EditEntry e;
    e.bone = bone;
    e.edit.axis[0] = x;
    e.edit.axis[1] = y;
    e.edit.axis[2] = z;
    e.edit.spread = sp;
    return e;
}

static std::vector<Xform> run(const Skel& sk, const EditBook& book, double world, bool hands, RigSculptor* keep = nullptr, int times = 1)
{
    RigSculptor rs;
    rs.set_skeleton(sk.names, sk.parents);
    HandOptions h;
    h.enabled = hands;
    rs.set_hands(h);
    rs.bind(book, 1, world);
    std::vector<Xform> pose = component_pose(sk);
    for (int i = 0; i < times; ++i)
    {
        rs.apply(pose.data(), static_cast<int32_t>(pose.size()));
    }
    if (keep)
    {
        *keep = rs;
    }
    return pose;
}

static void find_arms_by_name()
{
    {
        // Daz Genesis 8: twist bones between the links
        std::vector<std::string> n = {"hip", "pelvis", "abdomenLower", "chestUpper", "rCollar", "rShldrBend", "rShldrTwist", "rForearmBend", "rForearmTwist", "rHand", "rThumb1", "rIndex1", "rMid1"};
        std::vector<int32_t> p = {-1, 0, 0, 2, 3, 4, 5, 6, 7, 8, 9, 9, 9};
        const auto arms = find_arms(n, p);
        CHECK(arms.size() == 1 && arms[0].upper == 5 && arms[0].lower == 7 && arms[0].hand == 9 && arms[0].palm == 12, "Daz arm not found");
    }
    {
        // Mixamo: RightShoulder is the collar bone, RightArm the upper arm
        std::vector<std::string> n = {"Hips", "Spine", "Spine2", "RightShoulder", "RightArm", "RightForeArm", "RightHand", "RightHandIndex1", "LeftShoulder", "LeftArm", "LeftForeArm", "LeftHand"};
        std::vector<int32_t> p = {-1, 0, 1, 2, 3, 4, 5, 6, 2, 8, 9, 10};
        const auto arms = find_arms(n, p);
        CHECK(arms.size() == 2 && arms[0].upper == 4 && arms[0].lower == 5 && arms[0].hand == 6 && arms[0].palm == 7, "Mixamo right arm not found");
        CHECK(arms.size() == 2 && arms[1].upper == 9 && arms[1].hand == 11 && arms[1].palm == -1, "Mixamo left arm not found");
    }
    {
        // VRoid, plus IK / weapon helpers that must not count as hands
        std::vector<std::string> n = {"J_Bip_C_Hips", "J_Bip_C_Chest", "J_Bip_R_Shoulder", "J_Bip_R_UpperArm", "J_Bip_R_LowerArm", "J_Bip_R_Hand", "ik_hand_root", "ik_hand_r", "weapon_hand_r"};
        std::vector<int32_t> p = {-1, 0, 1, 2, 3, 4, -1, 6, 5};
        const auto arms = find_arms(n, p);
        CHECK(arms.size() == 1 && arms[0].upper == 3 && arms[0].lower == 4 && arms[0].hand == 5, "VRoid arm not found");
    }
}

static void settings_file()
{
    HandOptions o;
    o.enabled = false;
    o.strength = 0.6;
    o.fade_out = 0.7;
    o.shoulder_turn = 45.0;
    o.stretch = 1.35;
    o.fingers = false;
    o.feet = false;
    HandOptions back;
    CHECK(hand_settings::read(hand_settings::write(o), back), "settings file not read back");
    CHECK(!back.enabled && std::fabs(back.strength - 0.6) < 1e-6 && std::fabs(back.fade_out - 0.7) < 1e-6 &&
              std::fabs(back.shoulder_turn - 45.0) < 1e-6 && std::fabs(back.stretch - 1.35) < 1e-6 && !back.fingers && !back.feet,
          "settings changed on the way through the file");
    HandOptions wild;
    CHECK(hand_settings::read("strength=7\r\nstretch=-3\nshoulder=900\nfar=0.01\nnear=0.5\njunk\n# far=9\n", wild), "settings not read");
    CHECK(wild.strength == 1.0 && wild.stretch == 1.0 && wild.shoulder_turn == 60.0 && wild.fade_out >= wild.full_within + 0.05 - 1e-9,
          "settings not clamped");
    HandOptions none;
    CHECK(!hand_settings::read("# nothing here\n", none), "an empty file counted as settings");
}

// A small made-up rig with a finger lying on a breast bone's surface: when the breast grows 1.6x, the finger
// joints on it move out with its surface (finger bones keep their length), instead of sinking in.
static void fingers_follow_surface()
{
    struct B
    {
        const char* name;
        int parent;
        double x, y, z;
    };
    const B bones[] = {
        {"root", -1, 0, 0, 0},          {"pelvis", 0, 0, 0, 100},        {"spine_01", 1, 0, 0, 120},      {"breast_r", 2, 10, -6, 130},
        {"clavicle_r", 2, 0, -10, 140}, {"upperarm_r", 4, 0, -20, 140},  {"lowerarm_r", 5, 0, -22, 115},  {"hand_r", 6, 12, -14, 124},
        {"middle_01_r", 7, 14, -11, 127}, {"middle_02_r", 8, 15.5, -8.5, 130}, {"middle_03_r", 9, 16, -6, 132.5}, {"middle_04_r", 10, 15.5, -3.5, 134.5},
    };
    Skel sk;
    std::vector<Xform> comp;
    for (const B& b : bones)
    {
        sk.names.push_back(b.name);
        sk.parents.push_back(b.parent);
        Xform x = xf::identity();
        x.pos[0] = b.x;
        x.pos[1] = b.y;
        x.pos[2] = b.z;
        comp.push_back(x);
    }
    for (size_t i = 0; i < comp.size(); ++i)
    {
        sk.local.push_back(sk.parents[i] < 0 ? comp[i] : xf::relative(comp[i], comp[sk.parents[i]]));
    }
    EditBook book;
    book["breast_r"] = entry("breast_r", 1.6, 1.6, 1.6, Spread::Chain);
    std::vector<Xform> stiff, shaped;
    for (bool fingers : {false, true})
    {
        RigSculptor r;
        r.set_skeleton(sk.names, sk.parents);
        HandOptions h;
        h.enabled = true;
        h.fingers = fingers;
        r.set_hands(h);
        r.bind(book, 1, 1.0);
        std::vector<Xform> pose = component_pose(sk);
        r.apply(pose.data(), static_cast<int32_t>(pose.size()));
        (fingers ? shaped : stiff) = pose;
    }
    double err_stiff = 0.0, err_shaped = 0.0, worst_len = 0.0;
    for (int b = 9; b <= 11; ++b)
    {
        double d0[3], ds[3], dh[3], l0[3], l1[3];
        sub(comp[b].pos, comp[3].pos, d0);
        sub(stiff[b].pos, stiff[3].pos, ds);
        sub(shaped[b].pos, shaped[3].pos, dh);
        const double want = 1.6 * len3(d0);
        err_stiff += std::fabs(len3(ds) - want);
        err_shaped += std::fabs(len3(dh) - want);
        sub(comp[b].pos, comp[b - 1].pos, l0);
        sub(shaped[b].pos, shaped[b - 1].pos, l1);
        worst_len = std::max(worst_len, std::fabs(len3(l0) - len3(l1)));
        if (getenv("HANDS_DEBUG"))
        {
            std::printf("  %-12s src %.2f want %.2f stiff %.2f shaped %.2f\n", sk.names[b].c_str(), len3(d0), want, len3(ds), len3(dh));
        }
    }
    std::printf("fingers on a 1.6x breast: %.2f cm off its surface on average with stiff fingers, %.2f cm shaped\n", err_stiff / 3.0, err_shaped / 3.0);
    // (finger bones keep their length, so the joints cannot all land exactly on the 1.6x surface: closer, and the tip out of it)
    CHECK(err_shaped < err_stiff * 0.85, "fingers did not follow the bigger breast (%.2f vs %.2f)", err_shaped, err_stiff);
    double tip_s[3], tip_h[3];
    sub(stiff[11].pos, stiff[3].pos, tip_s);
    sub(shaped[11].pos, shaped[3].pos, tip_h);
    CHECK(len3(tip_h) > len3(tip_s) + 0.5, "fingertip still sinks into the bigger breast (%.2f vs %.2f cm)", len3(tip_h), len3(tip_s));
    CHECK(worst_len < 1.0e-3, "finger bone lengths changed (%.4f cm)", worst_len);
}


struct MadeBone
{
    const char* name;
    int parent;
    double x, y, z;
};

// a made-up rig from component-space joint positions (no rotations)
static Skel made_rig(const std::vector<MadeBone>& bones, std::vector<Xform>& comp)
{
    Skel sk;
    comp.clear();
    for (const MadeBone& b : bones)
    {
        sk.names.push_back(b.name);
        sk.parents.push_back(b.parent);
        Xform x = xf::identity();
        x.pos[0] = b.x;
        x.pos[1] = b.y;
        x.pos[2] = b.z;
        comp.push_back(x);
    }
    for (size_t i = 0; i < comp.size(); ++i)
    {
        sk.local.push_back(sk.parents[i] < 0 ? comp[i] : xf::relative(comp[i], comp[sk.parents[i]]));
    }
    return sk;
}

static std::vector<Xform> run_with(const Skel& sk, const EditBook& book, const HandOptions& h, RigSculptor* keep = nullptr)
{
    RigSculptor r;
    r.set_skeleton(sk.names, sk.parents);
    r.set_hands(h);
    r.bind(book, 1, 1.0);
    std::vector<Xform> pose = component_pose(sk);
    r.apply(pose.data(), static_cast<int32_t>(pose.size()));
    if (keep)
    {
        *keep = r;
    }
    return pose;
}

// Not a touch animation at all: an arm hangs with its elbow against the waist and the hand held forward. A wider
// waist pushes the elbow out (the arm turns about the shoulder); with Hand Contact off it would sink in.
static void elbow_follows_waist()
{
    std::vector<Xform> comp;
    const Skel sk = made_rig({{"root", -1, 0, 0, 0},
                              {"pelvis", 0, 0, 0, 100},
                              {"spine_01", 1, 0, 0, 120},
                              {"spine_02", 2, 0, 0, 140},
                              {"clavicle_r", 3, 0, -8, 145},
                              {"upperarm_r", 4, 0, -14, 145},
                              {"lowerarm_r", 5, 0, -5, 120},
                              {"hand_r", 6, 25, -5, 120}},
                             comp);
    EditBook book;
    for (const char* b : {"pelvis", "spine_01", "spine_02"})
    {
        book[b] = entry(b, 1.0, 1.5, 1.0, Spread::Solo);
    }
    HandOptions h;
    h.enabled = false;
    const std::vector<Xform> off = run_with(sk, book, h);
    h.enabled = true;
    RigSculptor keep;
    const std::vector<Xform> on = run_with(sk, book, h, &keep);
    const double out_off = -off[6].pos[1], out_on = -on[6].pos[1];
    double ul0[3], ul1[3], lh0[3], lh1[3];
    sub(comp[6].pos, comp[5].pos, ul0);
    sub(on[6].pos, on[5].pos, ul1);
    sub(comp[7].pos, comp[6].pos, lh0);
    sub(on[7].pos, on[6].pos, lh1);
    std::printf("elbow at a 1.5x wider waist: %.2f cm out without, %.2f cm with (wanted about 7.5); %s\n", out_off, out_on, keep.hands_report().c_str());
    CHECK(out_on > out_off + 1.5, "the elbow did not move out with the wider waist (%.2f vs %.2f cm)", out_on, out_off);
    CHECK(keep.hand_samples().size() == 1 && keep.hand_samples()[0].elbow, "the elbow contact was not reported");
    CHECK(std::fabs(len3(ul0) - len3(ul1)) < 1.0e-3 && std::fabs(len3(lh0) - len3(lh1)) < 1.0e-3, "arm bone lengths changed");
}

// An arm hanging almost straight with the hand by the thigh, and wider shoulders (a longer collar bone, "keep children
// size"): the shoulder moves out, the hand's spot stays, so the spot ends up a hair out of the arm's reach. "Shoulder
// help" used to turn the collar bone by its whole limit (30 degrees) the moment that happened, so a tiny sway of the
// animation made the shoulder snap between its wide and its narrow place every few frames. The turn has to grow
// smoothly from 0 with how far out of reach the spot is.
static void shoulder_help_is_smooth()
{
    const double S[3] = {0.0, -20.0, 145.0};
    double prev_turn = -1.0, prev_shoulder[3] = {0.0, 0.0, 0.0}, worst_turn = 0.0, worst_move = 0.0, most_turn = 0.0;
    for (int step = 0; step <= 160; ++step)
    {
        const double a = (-8.0 + 0.1 * step) * 3.14159265358979323846 / 180.0; // the arm sways in toward the body
        const double b = a + 3.0 * 3.14159265358979323846 / 180.0;              // a slightly bent elbow
        const double E[3] = {0.0, S[1] + 27.0 * std::sin(a), S[2] - 27.0 * std::cos(a)};
        const double H[3] = {0.0, E[1] + 26.0 * std::sin(b), E[2] - 26.0 * std::cos(b)};
        std::vector<Xform> comp;
        const Skel sk = made_rig({{"root", -1, 0, 0, 0},
                                  {"pelvis", 0, 0, 0, 100},
                                  {"spine_01", 1, 0, 0, 120},
                                  {"spine_02", 2, 0, 0, 140},
                                  {"clavicle_r", 3, 0, -8, 145},
                                  {"upperarm_r", 4, S[0], S[1], S[2]},
                                  {"lowerarm_r", 5, E[0], E[1], E[2]},
                                  {"hand_r", 6, H[0], H[1], H[2]},
                                  {"thigh_r", 1, 0, -10, 95},
                                  {"calf_r", 8, 0, -10, 50},
                                  {"foot_r", 9, 0, -10, 8}},
                                 comp);
        EditBook book;
        book["clavicle_r"] = entry("clavicle_r", 1.0, 1.3, 1.0, Spread::Keep); // the collar bone runs along Y here
        HandOptions h;
        h.enabled = true;
        RigSculptor keep;
        const std::vector<Xform> on = run_with(sk, book, h, &keep);
        const double turned = keep.hand_samples()[0].turned;
        if (prev_turn >= 0.0)
        {
            worst_turn = std::max(worst_turn, std::fabs(turned - prev_turn));
            double d[3];
            sub(on[5].pos, prev_shoulder, d);
            worst_move = std::max(worst_move, len3(d));
        }
        most_turn = std::max(most_turn, turned);
        prev_turn = turned;
        std::memcpy(prev_shoulder, on[5].pos, sizeof prev_shoulder);
    }
    std::printf("wider shoulders, hand by the thigh, arm swaying 0.1 deg a step: shoulder help up to %.1f deg, at most %.2f deg / %.2f cm a step\n",
                most_turn, worst_turn, worst_move);
    CHECK(most_turn > 0.5, "the spot never went out of reach: the case tests nothing (%.2f deg)", most_turn);
    CHECK(worst_turn < 0.5 && worst_move < 0.3, "the shoulder jumped (%.2f deg, %.2f cm in one 0.1 deg step)", worst_turn, worst_move);
}

// The elbow rests against a wider waist (the arm turns out about the shoulder) while the hand comes close to the belly.
// When the hand's own contact starts, the hand must start from where the elbow's turn took it - it used to jump back.
static void elbow_then_hand_is_smooth()
{
    double prev[3] = {0.0, 0.0, 0.0}, worst = 0.0;
    for (int step = 0; step <= 440; ++step)
    {
        const double hx = 30.0 - 0.05 * step;
        std::vector<Xform> comp;
        const Skel sk = made_rig({{"root", -1, 0, 0, 0},
                                  {"pelvis", 0, 0, 0, 100},
                                  {"spine_01", 1, 0, 0, 120},
                                  {"spine_02", 2, 0, 0, 140},
                                  {"clavicle_r", 3, 0, -8, 145},
                                  {"upperarm_r", 4, 0, -14, 145},
                                  {"lowerarm_r", 5, 0, -5, 120},
                                  {"hand_r", 6, hx, -5, 120}},
                                 comp);
        EditBook book;
        for (const char* b : {"pelvis", "spine_01", "spine_02"})
        {
            book[b] = entry(b, 1.0, 1.5, 1.0, Spread::Solo);
        }
        HandOptions h;
        h.enabled = true;
        const std::vector<Xform> on = run_with(sk, book, h);
        if (step > 0)
        {
            double d[3];
            sub(on[7].pos, prev, d);
            worst = std::max(worst, len3(d));
        }
        std::memcpy(prev, on[7].pos, sizeof prev);
    }
    std::printf("elbow on a wider waist, hand coming in 0.5 mm a step: the hand moved at most %.2f mm a step\n", worst * 10.0);
    CHECK(worst < 0.2, "the hand jumped %.2f cm when its contact began", worst);
}

// Sitting, an arm almost straight presses on the thigh, and the thigh is made thicker: the spot rises, so the elbow has
// to bend. Its side comes from the animation's own slight bend; below 1% of the arm the elbow used to switch to a
// fallback side, so an arm straightening by a fraction of a degree threw the elbow 90 degrees sideways.
static void straight_arm_elbow_is_smooth()
{
    const double pi = 3.14159265358979323846;
    double prev[3] = {0.0, 0.0, 0.0}, worst = 0.0, most_bend = 0.0;
    for (int step = 0; step <= 200; ++step)
    {
        const double beta = (0.1 + 0.02 * step) * pi / 180.0; // the elbow's own bend: 0.1 to 4.1 degrees
        const double S[3] = {30.0, -14.0, 154.0};
        const double E[3] = {S[0], S[1], S[2] - 27.0};
        const double H[3] = {E[0] + 26.0 * std::sin(beta), E[1], E[2] - 26.0 * std::cos(beta)};
        std::vector<Xform> comp;
        const Skel sk = made_rig({{"root", -1, 0, 0, 0},
                                  {"pelvis", 0, 0, 0, 100},
                                  {"spine_01", 1, 0, 0, 120},
                                  {"spine_02", 2, 0, 0, 140},
                                  {"clavicle_r", 3, 10, -10, 150},
                                  {"upperarm_r", 4, S[0], S[1], S[2]},
                                  {"lowerarm_r", 5, E[0], E[1], E[2]},
                                  {"hand_r", 6, H[0], H[1], H[2]},
                                  {"thigh_r", 1, 0, -14, 95},
                                  {"calf_r", 8, 45, -14, 95},
                                  {"foot_r", 9, 45, -14, 50}},
                                 comp);
        EditBook book;
        book["thigh_r"] = entry("thigh_r", 1.0, 1.5, 1.5, Spread::Solo);
        HandOptions h;
        h.enabled = true;
        const std::vector<Xform> on = run_with(sk, book, h);
        // how far the elbow sits off the shoulder -> hand line
        double sh[3], se[3];
        sub(on[7].pos, on[5].pos, sh);
        sub(on[6].pos, on[5].pos, se);
        const double along = (sh[0] * se[0] + sh[1] * se[1] + sh[2] * se[2]) / (len3(sh) * len3(sh));
        const double off[3] = {se[0] - along * sh[0], se[1] - along * sh[1], se[2] - along * sh[2]};
        most_bend = std::max(most_bend, len3(off));
        if (step > 0)
        {
            double d[3];
            sub(on[6].pos, prev, d);
            worst = std::max(worst, len3(d));
        }
        std::memcpy(prev, on[6].pos, sizeof prev);
    }
    std::printf("nearly straight arm on a thicker thigh, elbow bend growing 0.02 deg a step: elbow up to %.2f cm off the line, "
                "moved at most %.2f cm a step\n",
                most_bend, worst);
    CHECK(most_bend > 3.0, "the elbow never had to bend: the case tests nothing (%.2f cm)", most_bend);
    CHECK(worst < 0.5, "the elbow jumped %.2f cm in one step", worst);
}

// Crossed legs: the right foot rests on the left knee. A thicker left leg lifts the foot with it (the right leg
// bends to reach); "Feet and knees too" off leaves it where it was.
static void foot_follows_knee()
{
    std::vector<Xform> comp;
    const Skel sk = made_rig({{"root", -1, 0, 0, 0},
                              {"pelvis", 0, 0, 0, 100},
                              {"spine_01", 1, 0, 0, 120},
                              {"thigh_l", 1, 0, 10, 95},
                              {"calf_l", 3, 5, 10, 50},
                              {"foot_l", 4, 0, 10, 8},
                              {"ball_l", 5, 12, 10, 2},
                              {"thigh_r", 1, 0, -10, 95},
                              {"calf_r", 7, 35, -10, 75},
                              {"foot_r", 8, 12, 8, 56},
                              {"ball_r", 9, 14, 18, 54}},
                             comp);
    EditBook book;
    for (const char* b : {"thigh_l", "calf_l"})
    {
        book[b] = entry(b, 1.5, 1.5, 1.0, Spread::Solo);
    }
    HandOptions h;
    h.enabled = true;
    h.feet = false;
    const std::vector<Xform> no_feet = run_with(sk, book, h);
    h.feet = true;
    RigSculptor keep;
    const std::vector<Xform> on = run_with(sk, book, h, &keep);
    auto point = [](const std::vector<Xform>& p, double out[3]) {
        for (int c = 0; c < 3; ++c)
        {
            out[c] = 0.5 * (p[9].pos[c] + p[10].pos[c]);
        }
    };
    // distance of the foot from the left thigh's line (it should grow about 1.5x)
    auto off_thigh = [&](const std::vector<Xform>& p) {
        double P[3], a[3], ab[3];
        point(p, P);
        sub(P, p[3].pos, a);
        sub(p[4].pos, p[3].pos, ab);
        const double t = (a[0] * ab[0] + a[1] * ab[1] + a[2] * ab[2]) / (ab[0] * ab[0] + ab[1] * ab[1] + ab[2] * ab[2]);
        const double d[3] = {a[0] - t * ab[0], a[1] - t * ab[1], a[2] - t * ab[2]};
        return len3(d);
    };
    const double d0 = off_thigh(comp), d_off = off_thigh(no_feet), d_on = off_thigh(on);
    std::printf("foot on a 1.5x thicker knee: %.2f cm from the thigh in the game's pose, %.2f with feet off, %.2f with feet on; %s\n", d0, d_off,
                d_on, keep.hands_report().c_str());
    CHECK(std::fabs(d_off - d0) < 1.0e-3, "the foot moved with feet switched off");
    CHECK(d_on > d0 * 1.3, "the foot did not follow the thicker leg (%.2f -> %.2f cm)", d0, d_on);
    CHECK(keep.foot_samples().size() == 2 && keep.foot_samples()[1].weight > 0.5 && keep.foot_samples()[0].weight == 0.0,
          "foot contact not found (or the standing foot was moved)");
    double l0[3], l1[3];
    sub(comp[9].pos, comp[8].pos, l0);
    sub(on[9].pos, on[8].pos, l1);
    CHECK(std::fabs(len3(l0) - len3(l1)) < 1.0e-3, "shin length changed");
    // the standing left foot stays where it was
    CHECK(dist(on[5], comp[5]) < 1.0e-6, "the standing foot moved");
}

int main(int argc, char** argv)
{
    find_arms_by_name();
    fingers_follow_surface();
    elbow_follows_waist();
    shoulder_help_is_smooth();
    elbow_then_hand_is_smooth();
    straight_arm_elbow_is_smooth();
    foot_follows_knee();
    settings_file();
    if (argc < 4)
    {
        std::printf("usage: test_hands <skeleton_cache.txt> <bone count> <touched bone> [...]\n");
        return 2;
    }
    Skel ref;
    if (!load(argv[1], static_cast<size_t>(std::atoi(argv[2])), ref))
    {
        std::printf("FAIL: no skeleton with %s bones in %s\n", argv[2], argv[1]);
        return 1;
    }
    const auto arms = find_arms(ref.names, ref.parents);
    CHECK(arms.size() == 2, "expected 2 arms, found %zu", arms.size());
    if (arms.size() < 2)
    {
        return 1;
    }
    for (const auto& a : arms)
    {
        std::printf("arm: %s -> %s -> %s (palm %s)\n", ref.names[a.upper].c_str(), ref.names[a.lower].c_str(), ref.names[a.hand].c_str(),
                    a.palm >= 0 ? ref.names[a.palm].c_str() : "-");
    }
    std::vector<std::string> touch;
    for (int i = 3; i < argc; ++i)
    {
        touch.push_back(argv[i]);
    }
    const int32_t spot = first_found(ref, touch);
    CHECK(spot >= 0, "none of the touched bones exist");
    if (spot < 0)
    {
        return 1;
    }
    // the right arm (the one whose hand name ends in r / starts with r)
    ArmChain arm = arms[0];
    for (const auto& a : arms)
    {
        const std::string f = fold_case(ref.names[a.hand]);
        if (f.back() == 'r' || f.find("right") != std::string::npos)
        {
            arm = a;
        }
    }

    // game pose: the right palm resting 4 cm out from the touched bone (away from the bone it hangs from)
    Skel game = ref;
    const std::vector<Xform> ref_pose = component_pose(ref);
    const int32_t chest = ref.parents[spot] >= 0 ? ref.parents[spot] : spot;
    double out_dir[3];
    sub(ref_pose[spot].pos, ref_pose[chest].pos, out_dir);
    const double out_len = len3(out_dir);
    const double goal[3] = {ref_pose[spot].pos[0] + 4.0 * out_dir[0] / out_len, ref_pose[spot].pos[1] + 4.0 * out_dir[1] / out_len,
                            ref_pose[spot].pos[2] + 4.0 * out_dir[2] / out_len};
    const double miss = place_palm(game, arm, goal);
    std::printf("touch %s: palm placed %.2f cm from it\n", ref.names[spot].c_str(), miss);
    CHECK(miss < 3.0, "could not put the palm on %s (%.2f cm)", ref.names[spot].c_str(), miss);
    const std::vector<Xform> src = component_pose(game);
    double palm_src[3];
    palm_point(src, arm, palm_src);

    // 1) Height 1.2 with shorter arms: without Hand Contact the palm misses the spot, with it the palm is exactly
    //    where the scaled body put it (1.2 x its old place, the whole pose scales about the component origin).
    {
        EditBook book;
        for (const auto& a : arms)
        {
            book[fold_case(ref.names[a.upper])] = entry(ref.names[a.upper], 0.85, 1.0, 1.0, Spread::Keep);
            book[fold_case(ref.names[a.lower])] = entry(ref.names[a.lower], 0.85, 1.0, 1.0, Spread::Keep);
        }
        RigSculptor rs;
        const std::vector<Xform> off = run(game, book, 1.2, false);
        const std::vector<Xform> on = run(game, book, 1.2, true, &rs);
        double p_off[3], p_on[3], d_off[3], d_on[3];
        palm_point(off, arm, p_off);
        palm_point(on, arm, p_on);
        const double want[3] = {palm_src[0] * 1.2, palm_src[1] * 1.2, palm_src[2] * 1.2};
        sub(p_off, want, d_off);
        sub(p_on, want, d_on);
        std::printf("height 1.2, arms 0.85: palm off by %.2f cm without, %.3f cm with Hand Contact (%s)\n", len3(d_off), len3(d_on), rs.hands_report().c_str());
        CHECK(len3(d_off) > 2.0, "the edit should move the palm off the body (%.2f)", len3(d_off));
        CHECK(len3(d_on) < 0.05, "palm not back on the scaled body: %.3f cm", len3(d_on));
        // the hand turns with the body (no extra turn for a uniform scale)
        CHECK(qdiff(on[arm.hand], src[arm.hand]) < 1.0e-6, "hand rotation changed under a uniform scale");
        // the other (untouched) arm: hanging away from the body, left alone
        const ArmChain& other = arms[0].hand == arm.hand ? arms[1] : arms[0];
        CHECK(dist(on[other.hand], off[other.hand]) < 1.0e-6, "the arm away from the body moved (%.4f cm)", dist(on[other.hand], off[other.hand]));
        // repeat on the same buffer: our own output is recognised and the result is the same
        const std::vector<Xform> twice = run(game, book, 1.2, true, nullptr, 2);
        double worst = 0.0;
        for (size_t i = 0; i < on.size(); ++i)
        {
            worst = std::max(worst, dist(on[i], twice[i]));
        }
        CHECK(worst < 1.0e-9, "applying twice differs by %.3g cm", worst);
    }

    // 2) a bigger chest (the touched bone and the spine bone it hangs from, keep children size): the hand moves
    //    outward with it; the arm keeps its lengths and its elbow bend.
    {
        EditBook book;
        book[fold_case(ref.names[spot])] = entry(ref.names[spot], 1.5, 1.5, 1.5, Spread::Keep);
        book[fold_case(ref.names[chest])] = entry(ref.names[chest], 1.25, 1.25, 1.25, Spread::Keep);
        RigSculptor rs;
        const std::vector<Xform> off = run(game, book, 1.0, false);
        const std::vector<Xform> on = run(game, book, 1.0, true, &rs);
        std::printf("bigger %s/%s: %s\n", ref.names[chest].c_str(), ref.names[spot].c_str(), rs.hands_report().c_str());
        double p_on[3], p_off[3], m[3];
        palm_point(on, arm, p_on);
        palm_point(off, arm, p_off);
        sub(p_on, p_off, m);
        CHECK(len3(m) > 0.5, "hand did not follow the bigger chest (%.2f cm)", len3(m));
        const auto& samples = rs.hand_samples();
        size_t ai = 0;
        for (size_t k = 0; k < rs.arms().size(); ++k)
        {
            if (rs.arms()[k].hand == arm.hand)
            {
                ai = k;
            }
        }
        CHECK(ai < samples.size() && samples[ai].weight > 0.99, "hand on the body not fully corrected (%.2f)", ai < samples.size() ? samples[ai].weight : -1.0);
        // bone lengths kept
        auto seg = [](const std::vector<Xform>& p, int32_t a, int32_t b) {
            double d[3];
            sub(p[a].pos, p[b].pos, d);
            return len3(d);
        };
        CHECK(std::fabs(seg(on, arm.upper, arm.lower) - seg(src, arm.upper, arm.lower)) < 1.0e-3, "upper arm length changed");
        CHECK(std::fabs(seg(on, arm.lower, arm.hand) - seg(src, arm.lower, arm.hand)) < 1.0e-3, "forearm length changed");
        if (arm.palm >= 0)
        {
            CHECK(std::fabs(seg(on, arm.hand, arm.palm) - seg(src, arm.hand, arm.palm)) < 1.0e-3, "finger moved off the hand");
        }
        // the elbow still bends the same way (its side of the shoulder -> wrist line)
        auto bend_dir = [](const std::vector<Xform>& p, const ArmChain& a, double out[3]) {
            double sw[3], se[3];
            sub(p[a.hand].pos, p[a.upper].pos, sw);
            sub(p[a.lower].pos, p[a.upper].pos, se);
            const double l = len3(sw);
            const double along = (se[0] * sw[0] + se[1] * sw[1] + se[2] * sw[2]) / (l * l);
            for (int k = 0; k < 3; ++k)
            {
                out[k] = se[k] - along * sw[k];
            }
        };
        double b0[3], b1[3];
        bend_dir(off, arm, b0);
        bend_dir(on, arm, b1);
        CHECK(b0[0] * b1[0] + b0[1] * b1[1] + b0[2] * b1[2] > 0.0, "elbow flipped");
        // the palm rests as far out from the 1.5x bigger bone as 1.5x its old distance (the surface grew with it)
        double rest_src[3], rest_on[3];
        sub(palm_src, src[spot].pos, rest_src);
        sub(p_on, on[spot].pos, rest_on);
        std::printf("palm %.2f cm from %s in the game's pose, %.2f cm after the edit (want %.2f)\n", len3(rest_src), ref.names[spot].c_str(), len3(rest_on),
                    1.5 * len3(rest_src));
        CHECK(std::fabs(len3(rest_on) - 1.5 * len3(rest_src)) < 1.0, "palm not carried out with the bigger %s", ref.names[spot].c_str());
    }

    // 3) the reference pose (arms away from the body): Hand Contact on changes nothing.
    {
        EditBook book;
        book[fold_case(ref.names[spot])] = entry(ref.names[spot], 1.5, 1.5, 1.5, Spread::Keep);
        for (const auto& a : arms)
        {
            book[fold_case(ref.names[a.lower])] = entry(ref.names[a.lower], 1.2, 1.0, 1.0, Spread::Chain);
        }
        const std::vector<Xform> off = run(ref, book, 1.0, false);
        RigSculptor rs;
        const std::vector<Xform> on = run(ref, book, 1.0, true, &rs);
        double worst = 0.0;
        for (size_t i = 0; i < on.size(); ++i)
        {
            worst = std::max(worst, dist(on[i], off[i]) + qdiff(on[i], off[i]));
        }
        std::printf("reference pose: %s\n", rs.hands_report().c_str());
        CHECK(worst < 1.0e-3, "hands away from the body were changed (%.3g)", worst);
    }

    // 4) a much longer torso with the hand on the thigh: the spot stays low while the shoulder rises, out of the
    //    arm's reach. The collar bone turns toward it and the arm grows a little, so the palm still gets there.
    {
        const int32_t thigh = first_found(ref, {"thigh_r", "rthighbend", "rightupleg"});
        const int32_t knee = thigh >= 0 ? first_found(ref, {"calf_r", "rshin", "rightleg"}) : -1;
        const int32_t spine = first_found(ref, {"spine_01", "spine1", "spine"});
        const int32_t pelvis = first_found(ref, {"pelvis", "hips", "hip"});
        CHECK(thigh >= 0 && knee >= 0 && spine >= 0 && pelvis >= 0, "no thigh / spine bones");
        if (thigh >= 0 && knee >= 0 && spine >= 0 && pelvis >= 0)
        {
            // as far down the thigh as the reference arm reaches
            Skel low = ref;
            double placed = 1.0e30;
            for (double t : {0.25, 0.15, 0.05, -0.05, -0.15})
            {
                low = ref;
                double g[3];
                for (int k = 0; k < 3; ++k)
                {
                    g[k] = ref_pose[thigh].pos[k] + t * (ref_pose[knee].pos[k] - ref_pose[thigh].pos[k]);
                }
                placed = place_palm(low, arm, g);
                if (placed < 3.0)
                {
                    break;
                }
            }
            const std::vector<Xform> lsrc = component_pose(low);
            double lp[3];
            palm_point(lsrc, arm, lp);
            EditBook book;
            book[fold_case(ref.names[pelvis])] = entry(ref.names[pelvis], 1.2, 1.2, 1.2, Spread::Keep);
            book[fold_case(ref.names[spine])] = entry(ref.names[spine], 1.6, 1.6, 1.6, Spread::Keep);
            if (placed >= 3.0)
            {
                std::printf("longer torso: this reference pose's arm cannot reach the thigh (%.2f cm), case left out\n", placed);
            }
            RigSculptor rs;
            const std::vector<Xform> off = run(low, book, 1.0, false);
            const std::vector<Xform> on = run(low, book, 1.0, true, &rs);
            Xform palm = xf::identity();
            std::memcpy(palm.pos, lp, sizeof lp);
            const Xform want = xf::compose(xf::relative(palm, lsrc[thigh]), off[thigh]);
            double p_on[3], p_off[3], d_on[3], d_off[3];
            palm_point(on, arm, p_on);
            palm_point(off, arm, p_off);
            sub(p_on, want.pos, d_on);
            sub(p_off, want.pos, d_off);
            size_t ai = 0;
            for (size_t k = 0; k < rs.arms().size(); ++k)
            {
                if (rs.arms()[k].hand == arm.hand)
                {
                    ai = k;
                }
            }
            const HandSample& hs = rs.hand_samples()[ai];
            std::printf("longer torso, hand on %s (placed %.2f cm): palm %.2f cm off without, %.2f cm with; shoulder %.0f deg, arm x%.2f\n",
                        ref.names[thigh].c_str(), placed, len3(d_off), len3(d_on), hs.turned, hs.stretched);
            CHECK(placed >= 3.0 || len3(d_off) > 5.0, "the longer torso should pull the hand off the thigh (%.2f)", len3(d_off));
            CHECK(placed >= 3.0 || len3(d_on) < 1.5, "palm did not reach the thigh (%.2f cm)", len3(d_on));
            CHECK(placed >= 3.0 || hs.turned > 0.0 || hs.stretched > 1.0, "out of reach, yet neither shoulder nor arm helped");
            CHECK(hs.stretched <= 1.2 + 1.0e-9 && hs.turned <= 30.0 + 1.0e-6, "shoulder / arm went past the limits");
            CHECK(rs.arms()[ai].collar >= 0, "no collar bone found");

        }
    }

    // 5) no edits at all: nothing is written even with Hand Contact on
    {
        RigSculptor rs;
        rs.set_skeleton(game.names, game.parents);
        HandOptions h;
        h.enabled = true;
        rs.set_hands(h);
        rs.bind(EditBook{}, 1, 1.0);
        std::vector<Xform> pose = component_pose(game);
        CHECK(rs.apply(pose.data(), static_cast<int32_t>(pose.size())) == 0, "wrote bones without any edit");
    }

    if (g_fail)
    {
        std::printf("%d check(s) failed\n", g_fail);
        return 1;
    }
    std::printf("test_hands: all checks passed\n");
    return 0;
}
