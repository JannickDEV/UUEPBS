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

int main(int argc, char** argv)
{
    find_arms_by_name();
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

    // 4) no edits at all: nothing is written even with Hand Contact on
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
