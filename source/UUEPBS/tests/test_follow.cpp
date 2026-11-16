// Host-side tests for extra meshes that copy the main mesh's edited pose (core/follow.hpp):
// a separate head/face mesh must end up with the body's edits once, never twice.
#include "core/follow.hpp"
#include "core/proportions.hpp"
#include "core/registry.hpp"
#include "core/sculpt.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
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
        double len = std::sqrt(x.rot[0] * x.rot[0] + x.rot[1] * x.rot[1] + x.rot[2] * x.rot[2] + x.rot[3] * x.rot[3]);
        for (double& q : x.rot)
            q /= len;
        s.names.push_back(n);
        s.parents.push_back(p);
        s.local.push_back(x);
    }
    return s;
}

static std::vector<Xform> fk(const std::vector<int32_t>& parents, const std::vector<Xform>& local)
{
    std::vector<Xform> cs(local.size());
    for (size_t i = 0; i < local.size(); ++i)
    {
        cs[i] = parents[i] < 0 ? local[i] : xf::compose(local[i], cs[parents[i]]);
        cs[i].pos[3] = 0;
        cs[i].scl[3] = 0;
    }
    return cs;
}

static double diff(const Xform& a, const Xform& b)
{
    double d = 0;
    for (int i = 0; i < 3; ++i)
    {
        d = std::max(d, std::fabs(a.pos[i] - b.pos[i]));
        d = std::max(d, std::fabs(a.scl[i] - b.scl[i]) * 100.0);
    }
    return d;
}

struct FakeComp
{
    alignas(16) unsigned char mem[0x1000] = {};
    std::vector<Xform> buf[2];
    void setup(const std::vector<Xform>& pose, int32_t index)
    {
        buf[0] = buf[1] = pose;
        auto put = [&](size_t off, auto v) { std::memcpy(mem + off, &v, sizeof(v)); };
        put(0x0, (uintptr_t)0x1234);
        put(0xC, index);
        put(0x10, (uintptr_t)0x5555);
        RawArray a0{buf[0].data(), (int32_t)buf[0].size(), (int32_t)buf[0].size()};
        RawArray a1{buf[1].data(), (int32_t)buf[1].size(), (int32_t)buf[1].size()};
        put(0x600, a0);
        put(0x610, a1);
        put(0x648, (int32_t)1);
    }
};

int main()
{
    // --- judge_follow / FollowLatch on their own -------------------------------------------
    {
        Xform a = xf::identity(), b = xf::identity();
        b.scl[0] = b.scl[1] = b.scl[2] = 1.89;
        b.pos[2] = 10;
        CHECK(judge_follow({{a, a, b}}) == FollowVerdict::Own);
        CHECK(judge_follow({{b, a, b}}) == FollowVerdict::Copies);
        CHECK(judge_follow({{a, a, a}}) == FollowVerdict::Unknown); // no edit: nothing to tell
        Xform far = xf::identity();
        far.pos[0] = 500;
        CHECK(judge_follow({{far, a, b}}) == FollowVerdict::Unknown); // a different pose altogether
        FollowLatch latch;
        CHECK(latch.feed(FollowVerdict::Copies) == FollowVerdict::Unknown);
        CHECK(latch.feed(FollowVerdict::Copies) == FollowVerdict::Unknown);
        CHECK(latch.feed(FollowVerdict::Copies) == FollowVerdict::Copies);
        CHECK(latch.feed(FollowVerdict::Own) == FollowVerdict::Copies);
        CHECK(latch.feed(FollowVerdict::Unknown) == FollowVerdict::Copies); // unclear frames change nothing
        CHECK(latch.feed(FollowVerdict::Own) == FollowVerdict::Copies);
        CHECK(latch.feed(FollowVerdict::Own) == FollowVerdict::Copies);
        CHECK(latch.feed(FollowVerdict::Own) == FollowVerdict::Own);
    }

    const Skel s = load();
    CHECK(s.names.size() > 100);
    int head = -1;
    for (size_t i = 0; i < s.names.size(); ++i)
        if (s.names[i] == "head")
            head = (int)i;
    CHECK(head >= 0);

    // The face mesh: the chain down to the head plus everything under it (same skeleton, fewer bones).
    std::vector<int32_t> keep(s.names.size(), -1);
    {
        std::vector<char> want(s.names.size(), 0), under(s.names.size(), 0);
        for (int b = head; b >= 0; b = s.parents[b])
            want[b] = 1;
        under[head] = 1;
        for (size_t i = 0; i < s.names.size(); ++i)
            if (s.parents[i] >= 0 && under[s.parents[i]])
                want[i] = under[i] = 1;
        int n = 0;
        for (size_t i = 0; i < s.names.size(); ++i)
            if (want[i])
                keep[i] = n++;
    }
    std::vector<std::string> fnames;
    std::vector<int32_t> fparents;
    std::vector<int32_t> to_body; // face bone -> body bone
    for (size_t i = 0; i < s.names.size(); ++i)
    {
        if (keep[i] < 0)
            continue;
        fnames.push_back(s.names[i]);
        fparents.push_back(s.parents[i] < 0 ? -1 : keep[s.parents[i]]);
        to_body.push_back((int32_t)i);
    }
    std::printf("face mesh: %zu of %zu bones\n", fnames.size(), s.names.size());
    CHECK(fnames.size() > 10 && fnames.size() < s.names.size());

    const auto original = fk(s.parents, s.local);
    auto face_of = [&](const std::vector<Xform>& body) {
        std::vector<Xform> f(to_body.size());
        for (size_t i = 0; i < to_body.size(); ++i)
            f[i] = body[to_body[i]];
        return f;
    };
    // How far the face is from the body's (edited) pose, over the bones they share.
    auto mismatch = [&](const std::vector<Xform>& face, const std::vector<Xform>& body) {
        double w = 0;
        for (size_t i = 0; i < to_body.size(); ++i)
            w = std::max(w, diff(face[i], body[to_body[i]]));
        return w;
    };

    static FakeComp body, face;
    Registry& reg = Registry::instance();
    reg.set_layout(PoseLayout{0x600, 0x648, 16, 96});
    reg.set_active_actor(kPlayerActor, "Player", "auto");

    // copies = the face's animation copies the body's finished pose (Copy Pose From Mesh);
    // otherwise it animates on its own (same animation as the body, untouched by us).
    auto run = [&](bool copies, int frames) {
        for (int k = 0; k < frames; ++k)
        {
            body.buf[1] = original; // the engine writes a fresh pose every frame
            reg.on_pose_finalized(body.mem);
            {
                const auto f = copies ? face_of(body.buf[1]) : face_of(original);
                std::copy(f.begin(), f.end(), face.buf[1].begin()); // keep the buffer address
            }
            reg.on_pose_finalized(face.mem);
        }
    };
    auto face_follows = [&]() {
        for (const auto& r : reg.rigs())
            if (!r.primary)
                return r.follows;
        return false;
    };

    for (const bool copies : {true, false})
    {
        body.setup(original, 201);
        face.setup(face_of(original), 202);
        std::string msg;
        CHECK(reg.track((uintptr_t)body.mem, "Body", "Player", s.names, s.parents, true, msg, kPlayerActor));
        CHECK(reg.track((uintptr_t)face.mem, "Face", "Player", fnames, fparents, false, msg, kPlayerActor));

        // Body > Head size 1.89
        EditBook book;
        EditEntry e;
        e.bone = "head";
        e.edit.axis[0] = e.edit.axis[1] = e.edit.axis[2] = 1.89;
        book["head"] = e;
        reg.replace_edits(book);
        run(copies, 6);
        const double head_scale = face.buf[1][keep[head]].scl[0];
        std::printf("%s face: head scale %.3f, mismatch to body %.4f, follows %d\n", copies ? "copying" : "own-pose", head_scale,
                    mismatch(face.buf[1], body.buf[1]), (int)face_follows());
        CHECK(std::fabs(body.buf[1][head].scl[0] - 1.89) < 1e-6);
        CHECK(std::fabs(head_scale - 1.89) < 1e-4); // once, not 1.89 * 1.89
        CHECK(mismatch(face.buf[1], body.buf[1]) < 1e-3);
        CHECK(face_follows() == copies);

        // Character Scale > Height 1.2 on top
        set_scale_value(book, "height", 1.2);
        reg.replace_edits(book);
        run(copies, 3);
        CHECK(mismatch(face.buf[1], body.buf[1]) < 1e-3);
        CHECK(face_follows() == copies);

        // Before/After: everything goes back to the game's own pose
        reg.set_compare(true);
        run(copies, 2);
        CHECK(mismatch(face.buf[1], original) < 1e-6);
        reg.set_compare(false);
        run(copies, 1);
        CHECK(mismatch(face.buf[1], body.buf[1]) < 1e-3);

        // Sliders back to neutral: the face is the game's again either way.
        reg.clear_edits();
        run(copies, 2);
        CHECK(mismatch(face.buf[1], original) < 1e-6);
        double w = 0;
        for (size_t i = 0; i < original.size(); ++i)
            w = std::max(w, diff(body.buf[1][i], original[i]));
        CHECK(w < 1e-6);
        reg.untrack_all();
    }

    // An extra mesh in a completely different pose (an inventory preview copy) is never taken for
    // a follower: it keeps getting the edits.
    {
        auto moved = original;
        for (auto& x : moved)
            x.pos[0] += 300.0;
        body.setup(original, 301);
        face.setup(face_of(moved), 302);
        std::string msg;
        CHECK(reg.track((uintptr_t)body.mem, "Body", "Player", s.names, s.parents, true, msg, kPlayerActor));
        CHECK(reg.track((uintptr_t)face.mem, "Copy", "Player", fnames, fparents, false, msg, kPlayerActor));
        EditBook book;
        EditEntry e;
        e.bone = "head";
        e.edit.axis[0] = e.edit.axis[1] = e.edit.axis[2] = 1.5;
        book["head"] = e;
        reg.replace_edits(book);
        for (int k = 0; k < 6; ++k)
        {
            body.buf[1] = original;
            reg.on_pose_finalized(body.mem);
            const auto f = face_of(moved);
            std::copy(f.begin(), f.end(), face.buf[1].begin());
            reg.on_pose_finalized(face.mem);
        }
        CHECK(!face_follows());
        CHECK(std::fabs(face.buf[1][keep[head]].scl[0] - 1.5) < 1e-4);
        reg.untrack_all();
    }

    if (g_fail)
    {
        std::printf("%d check(s) failed\n", g_fail);
        return 1;
    }
    std::printf("follow: all checks passed\n");
    return 0;
}
