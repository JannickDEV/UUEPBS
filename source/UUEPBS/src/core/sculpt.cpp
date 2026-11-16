#include "sculpt.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cmath>
#include <cstring>

namespace uuepbs
{
    namespace
    {
        constexpr double kMinScale = 0.02;
        constexpr double kMaxScale = 20.0;
        constexpr double kNeutralEps = 1.0e-6;
    } // namespace

    const char* spread_label(Spread s)
    {
        switch (s)
        {
        case Spread::Chain:
            return "Scale children too";
        case Spread::Keep:
            return "Keep children size";
        case Spread::Solo:
            return "This bone only";
        }
        return "?";
    }

    const char* spread_token(Spread s)
    {
        switch (s)
        {
        case Spread::Chain:
            return "chain";
        case Spread::Keep:
            return "keep";
        case Spread::Solo:
            return "solo";
        }
        return "chain";
    }

    bool spread_from_token(std::string_view token, Spread& out)
    {
        const std::string t = fold_case(token);
        if (t == "chain" || t == "full" || t == "hierarchy")
        {
            out = Spread::Chain;
            return true;
        }
        if (t == "keep" || t == "preserve")
        {
            out = Spread::Keep;
            return true;
        }
        if (t == "solo" || t == "isolated" || t == "bone")
        {
            out = Spread::Solo;
            return true;
        }
        return false;
    }

    bool BoneEdit::has_scale() const
    {
        return std::fabs(axis[0] - 1.0) >= kNeutralEps || std::fabs(axis[1] - 1.0) >= kNeutralEps || std::fabs(axis[2] - 1.0) >= kNeutralEps;
    }

    bool BoneEdit::has_turn() const
    {
        return std::fabs(turn[0]) >= kNeutralEps || std::fabs(turn[1]) >= kNeutralEps || std::fabs(turn[2]) >= kNeutralEps;
    }

    bool BoneEdit::has_shift() const
    {
        return std::fabs(shift[0]) >= kNeutralEps || std::fabs(shift[1]) >= kNeutralEps || std::fabs(shift[2]) >= kNeutralEps;
    }

    void BoneEdit::clamp()
    {
        for (int a = 0; a < 3; ++a)
        {
            if (!std::isfinite(axis[a]))
            {
                axis[a] = 1.0;
            }
            axis[a] = std::min(kMaxScale, std::max(kMinScale, axis[a]));
            if (!std::isfinite(turn[a]))
            {
                turn[a] = 0.0;
            }
            turn[a] = std::min(kMaxTurn, std::max(-kMaxTurn, turn[a]));
            if (!std::isfinite(shift[a]))
            {
                shift[a] = 0.0;
            }
            shift[a] = std::min(kMaxShift, std::max(-kMaxShift, shift[a]));
        }
        if (static_cast<uint8_t>(spread) > static_cast<uint8_t>(Spread::Solo))
        {
            spread = Spread::Chain;
        }
    }

    void turn_quaternion(const BoneEdit& edit, double out[4])
    {
        constexpr double kHalfRad = 3.14159265358979323846 / 360.0;
        const double hx = edit.turn[0] * kHalfRad;
        const double hy = edit.turn[1] * kHalfRad;
        const double hz = edit.turn[2] * kHalfRad;
        const double qx[4] = {std::sin(hx), 0.0, 0.0, std::cos(hx)};
        const double qy[4] = {0.0, std::sin(hy), 0.0, std::cos(hy)};
        const double qz[4] = {0.0, 0.0, std::sin(hz), std::cos(hz)};
        double yx[4];
        xf::qmul(qy, qx, yx);  // X first, then Y
        xf::qmul(qz, yx, out); // then Z
    }

    std::string fold_case(std::string_view text)
    {
        std::string out(text);
        for (char& c : out)
        {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return out;
    }

    namespace
    {
        bool has_any(const std::string& folded, std::initializer_list<const char*> words)
        {
            for (const char* w : words)
            {
                if (folded.find(w) != std::string::npos)
                {
                    return true;
                }
            }
            return false;
        }

        // IK targets, twist / roll helpers and control bones never carry the leg itself.
        bool helper_bone(const std::string& folded)
        {
            if (has_any(folded, {"twist", "roll", "ctrl", "pole", "target", "corrective", "correct", "socket", "attach"}))
            {
                return true;
            }
            // "ik" as its own word: ik_foot_l, foot_ik, IK_Foot_L - but not "kick" or "spike"
            const size_t n = folded.size();
            for (size_t at = folded.find("ik"); at != std::string::npos; at = folded.find("ik", at + 1))
            {
                const bool start = at == 0 || !std::isalpha(static_cast<unsigned char>(folded[at - 1]));
                const bool end = at + 2 >= n || !std::isalpha(static_cast<unsigned char>(folded[at + 2]));
                if (start && end)
                {
                    return true;
                }
            }
            return false;
        }

        double vlen(const double v[3])
        {
            return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        }

        double vdot(const double a[3], const double b[3])
        {
            return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
        }

        void vcross(const double a[3], const double b[3], double out[3])
        {
            out[0] = a[1] * b[2] - a[2] * b[1];
            out[1] = a[2] * b[0] - a[0] * b[2];
            out[2] = a[0] * b[1] - a[1] * b[0];
        }

        // Shortest rotation taking direction `from` onto direction `to`. False when they are opposite
        // (no unique answer) or degenerate.
        bool arc(const double from[3], const double to[3], double out[4])
        {
            const double lf = vlen(from), lt = vlen(to);
            if (lf < 1.0e-6 || lt < 1.0e-6)
            {
                return false;
            }
            double c[3];
            vcross(from, to, c);
            const double w = lf * lt + vdot(from, to);
            const double len = std::sqrt(c[0] * c[0] + c[1] * c[1] + c[2] * c[2] + w * w);
            if (w < 1.0e-9 * lf * lt || len < 1.0e-12)
            {
                return false;
            }
            out[0] = c[0] / len;
            out[1] = c[1] / len;
            out[2] = c[2] / len;
            out[3] = w / len;
            return true;
        }

        // angle between two rotations in degrees (robust for nearly equal, float-normalised quaternions)
        double angle_deg(const double a[4], const double b[4])
        {
            double ia[4], r[4];
            xf::qconj(a, ia);
            xf::qmul(ia, b, r);
            const double v = std::sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
            return 114.59155902616465 * std::atan2(v, std::fabs(r[3]));
        }

        void qnormalize(double q[4])
        {
            const double len = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
            if (len > 1.0e-12)
            {
                for (int a = 0; a < 4; ++a)
                {
                    q[a] /= len;
                }
            }
        }

        // p' = centre + q (p - centre)
        void turn_about(const double q[4], const double centre[3], double p[3])
        {
            const double d[3] = {p[0] - centre[0], p[1] - centre[1], p[2] - centre[2]};
            double r[3];
            xf::qrotate(q, d, r);
            for (int a = 0; a < 3; ++a)
            {
                p[a] = centre[a] + r[a];
            }
        }
    } // namespace

    std::vector<LegChain> find_legs(const std::vector<std::string>& names, const std::vector<int32_t>& parents)
    {
        std::vector<LegChain> legs;
        const int32_t n = static_cast<int32_t>(names.size());
        if (parents.size() != names.size())
        {
            return legs;
        }
        std::vector<std::string> folded(names.size());
        for (int32_t i = 0; i < n; ++i)
        {
            folded[i] = fold_case(names[i]);
        }
        for (int32_t f = 0; f < n && legs.size() < 4; ++f)
        {
            if (!has_any(folded[f], {"foot", "ankle"}) || helper_bone(folded[f]))
            {
                continue;
            }
            const int32_t c = parents[f];
            if (c < 0 || helper_bone(folded[c]) || has_any(folded[c], {"foot", "ankle"}) ||
                !has_any(folded[c], {"calf", "knee", "lowerleg", "shin", "leg", "xiaotui"}))
            {
                continue;
            }
            const int32_t t = parents[c];
            if (t < 0 || helper_bone(folded[t]) || !has_any(folded[t], {"thigh", "upleg", "upperleg", "hip", "leg", "datui", "momo"}))
            {
                continue;
            }
            bool taken = false;
            for (const LegChain& l : legs)
            {
                taken = taken || l.thigh == t || l.calf == c;
            }
            if (!taken)
            {
                int32_t ball = -1;
                for (int32_t j = f + 1; j < n && ball < 0; ++j)
                {
                    if (parents[j] == f && has_any(folded[j], {"ball", "toe"}) && !helper_bone(folded[j]))
                    {
                        ball = j;
                    }
                }
                legs.push_back({t, c, f, ball});
            }
        }
        return legs;
    }

    std::vector<ArmChain> find_arms(const std::vector<std::string>& names, const std::vector<int32_t>& parents)
    {
        std::vector<ArmChain> arms;
        const int32_t n = static_cast<int32_t>(names.size());
        if (parents.size() != names.size())
        {
            return arms;
        }
        std::vector<std::string> folded(names.size());
        for (int32_t i = 0; i < n; ++i)
        {
            folded[i] = fold_case(names[i]);
        }
        auto is_lower = [](const std::string& f) {
            if (has_any(f, {"forearm", "lowerarm", "lowarm", "lower_arm", "loarm", "elbow"}))
            {
                return true;
            }
            return has_any(f, {"arm"}) && !has_any(f, {"upper", "uparm", "shldr", "shoulder", "clav", "collar", "armature"});
        };
        auto is_upper = [](const std::string& f) {
            return has_any(f, {"upperarm", "uparm", "upper_arm", "shldr", "shoulder", "arm"}) &&
                   !has_any(f, {"forearm", "lower", "lowarm", "loarm", "elbow", "clav", "collar", "armature"});
        };
        // first non-helper ancestor within a few steps (twist / roll bones in between are skipped)
        auto up = [&](int32_t from) {
            int32_t a = from >= 0 ? parents[from] : -1;
            for (int steps = 0; a >= 0 && steps < 4; ++steps, a = parents[a])
            {
                if (!helper_bone(folded[a]))
                {
                    return a;
                }
            }
            return int32_t{-1};
        };
        for (int32_t h = 0; h < n && arms.size() < 4; ++h)
        {
            const std::string& f = folded[h];
            if (!has_any(f, {"hand", "wrist"}) || helper_bone(f) ||
                has_any(f, {"handle", "finger", "thumb", "index", "middle", "ring", "pinky", "carpal", "weapon", "gun", "prop"}) ||
                f.rfind("vb ", 0) == 0)
            {
                continue;
            }
            const int32_t lower = up(h);
            if (lower < 0 || !is_lower(folded[lower]))
            {
                continue;
            }
            const int32_t upper = up(lower);
            if (upper < 0 || !is_upper(folded[upper]))
            {
                continue;
            }
            bool taken = false;
            for (const ArmChain& a : arms)
            {
                taken = taken || a.hand == h || a.lower == lower || a.upper == upper;
            }
            if (taken)
            {
                continue;
            }
            int32_t palm = -1, index = -1;
            for (int32_t j = h + 1; j < n; ++j)
            {
                if (parents[j] != h || helper_bone(folded[j]))
                {
                    continue;
                }
                if (palm < 0 && has_any(folded[j], {"middle", "mid"}))
                {
                    palm = j;
                }
                if (index < 0 && has_any(folded[j], {"index"}))
                {
                    index = j;
                }
            }
            int32_t collar = up(upper);
            if (collar >= 0 && !has_any(folded[collar], {"clav", "collar", "shoulder"}))
            {
                collar = -1;
            }
            arms.push_back({upper, lower, h, palm >= 0 ? palm : index, collar});
        }
        return arms;
    }

    void RigSculptor::set_hands(const HandOptions& options)
    {
        HandOptions o = options;
        if (!std::isfinite(o.full_within) || o.full_within < 0.0)
        {
            o.full_within = 0.25;
        }
        if (!std::isfinite(o.fade_out) || o.fade_out <= o.full_within)
        {
            o.fade_out = o.full_within + 0.2;
        }
        o.full_within = std::min(2.0, o.full_within);
        o.fade_out = std::min(3.0, o.fade_out);
        o.strength = std::isfinite(o.strength) ? std::min(1.0, std::max(0.0, o.strength)) : 1.0;
        const bool replan = o.enabled != m_hands.enabled || o.feet != m_hands.feet;
        m_hands = o;
        if (replan)
        {
            m_revision = ~0ull; // re-plan: with Hand Contact on, the arms are rebuilt
        }
    }

    void RigSculptor::set_ground(const GroundOptions& options)
    {
        GroundOptions o = options;
        if (!std::isfinite(o.extra))
        {
            o.extra = 0.0;
        }
        o.max = std::isfinite(o.max) ? std::min(200.0, std::max(0.0, std::fabs(o.max))) : 40.0;
        o.extra = std::min(o.max, std::max(-o.max, o.extra));
        const bool same = o.enabled == m_ground.enabled && o.leg_ik == m_ground.leg_ik && o.extra == m_ground.extra && o.max == m_ground.max;
        m_ground = o;
        if (!same)
        {
            m_revision = ~0ull; // re-plan: with Auto Ground on, every bone is rebuilt
        }
    }

    bool RigSculptor::set_skeleton(std::vector<std::string> names, std::vector<int32_t> parents, std::string* why)
    {
        if (names.empty() || names.size() != parents.size())
        {
            if (why)
            {
                *why = "bone name and parent lists do not line up";
            }
            return false;
        }
        for (size_t i = 0; i < parents.size(); ++i)
        {
            // The engine keeps bones sorted so that a parent always precedes its children.
            if (parents[i] >= static_cast<int32_t>(i) || parents[i] < -1)
            {
                if (why)
                {
                    *why = "bone '" + names[i] + "' has an out-of-order parent index";
                }
                return false;
            }
        }

        m_names = std::move(names);
        m_parents = std::move(parents);
        m_folded.clear();
        m_lookup.clear();
        m_folded.reserve(m_names.size());
        for (size_t i = 0; i < m_names.size(); ++i)
        {
            m_folded.push_back(fold_case(m_names[i]));
            m_lookup.emplace(m_folded.back(), static_cast<int32_t>(i));
        }

        const size_t n = m_names.size();
        m_edit_slot.assign(n, -1);
        m_in_plan.assign(n, 0);
        m_source.assign(n, xf::identity());
        m_for_children.assign(n, xf::identity());
        m_fresh.assign(n, xf::identity());
        m_last_ok.assign(n, 0);
        m_plan.clear();
        m_inputs.clear();
        m_touched.clear();
        m_scales.clear();
        m_revision = ~0ull;
        forget_buffers();

        // Legs for Auto Ground, and which leg link every bone hangs from.
        m_legs = find_legs(m_names, m_parents);
        m_leg_of.assign(n, int8_t{-1});
        m_leg_part.assign(n, uint8_t{0});
        for (size_t li = 0; li < m_legs.size(); ++li)
        {
            const LegChain& leg = m_legs[li];
            for (size_t i = static_cast<size_t>(leg.thigh); i < n; ++i)
            {
                const int32_t bone = static_cast<int32_t>(i);
                const int32_t p = m_parents[i];
                uint8_t part = 0;
                if (bone == leg.thigh)
                {
                    part = 1;
                }
                else if (p >= 0 && m_leg_of[p] == static_cast<int8_t>(li))
                {
                    part = bone == leg.calf ? 2 : bone == leg.foot ? 3 : m_leg_part[p];
                }
                if (part)
                {
                    m_leg_of[i] = static_cast<int8_t>(li);
                    m_leg_part[i] = part;
                }
            }
        }
        m_last_lift = 0.0;

        // Arms for Hand Contact, which arm link every bone hangs from, and the body bones a hand can rest on.
        m_arms = find_arms(m_names, m_parents);
        m_arm_of.assign(n, int8_t{-1});
        m_arm_part.assign(n, uint8_t{0});
        for (size_t ai = 0; ai < m_arms.size(); ++ai)
        {
            const ArmChain& arm = m_arms[ai];
            const int32_t first = arm.collar >= 0 ? arm.collar : arm.upper;
            for (size_t i = static_cast<size_t>(first); i < n; ++i)
            {
                const int32_t bone = static_cast<int32_t>(i);
                const int32_t p = m_parents[i];
                uint8_t part = 0;
                if (bone == arm.collar)
                {
                    part = 4; // collar and what hangs from it besides the upper arm
                }
                else if (bone == arm.upper)
                {
                    part = 1;
                }
                else if (p >= 0 && m_arm_of[p] == static_cast<int8_t>(ai))
                {
                    part = bone == arm.lower ? 2 : bone == arm.hand ? 3 : m_arm_part[p];
                }
                if (part)
                {
                    m_arm_of[i] = static_cast<int8_t>(ai);
                    m_arm_part[i] = part;
                }
            }
        }
        m_body_bones.clear();
        m_body_from.clear();
        m_hand_samples.assign(m_arms.size(), HandSample{});
        m_foot_samples.assign(m_legs.size(), HandSample{});
        {
            // helpers (IK targets, virtual bones, weapon sockets...) and everything below them are not body
            std::vector<uint8_t> junk(n, 0), body(n, 0);
            for (size_t i = 0; i < n; ++i)
            {
                const std::string& f = m_folded[i];
                const int32_t p = m_parents[i];
                junk[i] = helper_bone(f) || f.rfind("vb ", 0) == 0 || has_any(f, {"curves", "weapon", "gun", "prop", "hair", "pony", "braid", "skirt", "cloth", "cape", "coat", "ribbon", "earring", "chain"}) || (p >= 0 && junk[p]);
                body[i] = p >= 0 && !junk[i] && m_arm_of[i] < 0;
                if (!body[i])
                {
                    continue;
                }
                // the segment from the nearest body ancestor to this bone (a single point when there is none)
                int32_t from = p;
                while (from >= 0 && !body[from])
                {
                    from = m_parents[from];
                }
                m_body_bones.push_back(static_cast<int32_t>(i));
                m_body_from.push_back(from >= 0 ? from : static_cast<int32_t>(i));
            }
        }

        // Landmarks for the ground diagnostics: the legs' hip (else "pelvis"), the first neck bone, the head.
        m_marks = {-1, -1, -1};
        if (!m_legs.empty())
        {
            m_marks[0] = m_parents[m_legs.front().thigh];
        }
        for (size_t i = 0; i < n; ++i)
        {
            const std::string& f = m_folded[i];
            if (m_marks[0] < 0 && (f == "pelvis" || f == "hips" || f == "hip"))
            {
                m_marks[0] = static_cast<int32_t>(i);
            }
            if (m_marks[1] < 0 && f.find("neck") != std::string::npos && !helper_bone(f))
            {
                m_marks[1] = static_cast<int32_t>(i);
            }
            if (m_marks[2] < 0 && f == "head")
            {
                m_marks[2] = static_cast<int32_t>(i);
            }
        }
        return true;
    }

    std::string RigSculptor::ground_report() const
    {
        char buf[900];
        int len = std::snprintf(buf, sizeof buf, "%s lift %.2f", m_ground_primary ? "main" : "other", m_last_lift);
        if (m_ground_primary && !m_legs.empty())
        {
            len += std::snprintf(buf + len, sizeof buf - len, " (deepest sole %.2f)", m_raw_lift);
            for (size_t li = 0; li < m_legs.size() && li < m_leg_diag.size() && len < static_cast<int>(sizeof buf) - 160; ++li)
            {
                const LegChain& l = m_legs[li];
                len += std::snprintf(buf + len, sizeof buf - len, " | %s(%s) sank: ankle %.2f sole %.2f, reach allows %.2f, foot z %.2f->%.2f", m_names[l.foot].c_str(),
                                     l.ball >= 0 ? m_names[l.ball].c_str() : "-", m_leg_diag[li][0], m_leg_diag[li][1], m_leg_diag[li][2],
                                     m_source[l.foot].pos[2], m_fresh[l.foot].pos[2]);
            }
        }
        if (!m_ground_primary)
        {
            const char* what = m_copy_state == 1 ? "copies the main mesh's pose: shared bones not edited again"
                               : m_copy_state == 2 ? "animated on its own: edited and grounded"
                                                   : "not known yet";
            len += std::snprintf(buf + len, sizeof buf - len, " (%s; off main after %.2f cm / before %.2f cm)", what, m_copy_err_after, m_copy_err_before);
        }
        static const char* labels[3] = {"hip", "neck", "head"};
        for (int k = 0; k < 3 && len < static_cast<int>(sizeof buf) - 40; ++k)
        {
            if (m_marks[k] >= 0)
            {
                len += std::snprintf(buf + len, sizeof buf - len, " | %s %s %.2f->%.2f", labels[k], m_names[m_marks[k]].c_str(), m_mark_src[k], m_mark_out[k]);
            }
        }
        return std::string(buf, static_cast<size_t>(std::max(0, std::min(len, static_cast<int>(sizeof buf) - 1))));
    }

    int32_t RigSculptor::find(std::string_view bone) const
    {
        const auto it = m_lookup.find(fold_case(bone));
        return it == m_lookup.end() ? -1 : it->second;
    }

    void RigSculptor::bind(const EditBook& book, uint64_t revision, double world)
    {
        const size_t n = m_names.size();
        m_revision = revision;
        m_world = std::isfinite(world) ? std::min(kMaxScale, std::max(kMinScale, world)) : 1.0;
        if (std::fabs(m_world - 1.0) < kNeutralEps)
        {
            m_world = 1.0;
        }
        const bool world_on = m_world != 1.0;
        m_scales.clear();
        m_turns.clear();
        m_plan.clear();
        m_inputs.clear();
        std::fill(m_edit_slot.begin(), m_edit_slot.end(), -1);
        std::fill(m_in_plan.begin(), m_in_plan.end(), 0);
        std::fill(m_last_ok.begin(), m_last_ok.end(), 0);
        if (n == 0)
        {
            return;
        }

        bool any_edit = false; // anywhere on the character, even on bones this mesh does not have
        for (const auto& [key, entry] : book)
        {
            if (entry.edit.is_neutral())
            {
                continue;
            }
            any_edit = true;
            const auto it = m_lookup.find(key);
            if (it == m_lookup.end())
            {
                continue;
            }
            BoneEdit s = entry.edit;
            s.clamp();
            m_edit_slot[it->second] = static_cast<int32_t>(m_scales.size());
            m_scales.push_back(s);
            std::array<double, 4> q{};
            turn_quaternion(s, q.data());
            m_turns.push_back(q);
        }
        // With Auto Ground a mesh none of whose bones has a slider (a head mesh when only the legs are edited) still
        // has to move with the lifted body when it is animated on its own.
        // A non-zero extra (a mod mesh whose soles sit above the game's ground) applies with no sliders too.
        if (m_scales.empty() && !world_on && !(m_ground.enabled && (any_edit || std::fabs(m_ground.extra) > 1.0e-6)))
        {
            return;
        }

        // A bone must be rebuilt when it is edited or when something above it moved it.
        std::vector<uint8_t> pushes_children(n, 0);
        for (size_t i = 0; i < n; ++i)
        {
            const int32_t p = m_parents[i];
            const bool moved_by_ancestor = p >= 0 && pushes_children[p];
            const int32_t slot = m_edit_slot[i];
            const bool edited = slot >= 0;
            const bool world_root = world_on && p < 0; // a root carries the whole-pose scale to everything below it
            const bool arm = m_hands.enabled && (m_arm_of[i] >= 0 || (m_hands.feet && m_leg_of[i] >= 0)); // Hand Contact may bend it
            if (!edited && !moved_by_ancestor && !world_root && !arm)
            {
                continue;
            }
            m_in_plan[i] = 1;
            m_plan.push_back(static_cast<int32_t>(i));
            // A "this bone only" edit leaves the children alone - except on another mesh of the character: when its
            // pose is copied from the main mesh, its own bones below arrive carrying the edit and must be put back.
            pushes_children[i] = moved_by_ancestor || world_root || (edited && (m_scales[slot].spread != Spread::Solo || !m_ground_primary));
        }

        // Auto Ground moves the whole pose (and bends legs), so every bone is rebuilt and remembered.
        if (m_ground.enabled)
        {
            m_plan.resize(n);
            for (size_t i = 0; i < n; ++i)
            {
                m_plan[i] = static_cast<int32_t>(i);
                m_in_plan[i] = 1;
            }
        }

        // Hand Contact reads every body bone (where the hand rests), so all of them are inputs.
        std::vector<uint8_t> needed(n, static_cast<uint8_t>(m_hands.enabled && !m_arms.empty() ? 1 : 0));
        for (const int32_t i : m_plan)
        {
            needed[i] = 1;
            if (m_parents[i] >= 0)
            {
                needed[m_parents[i]] = 1;
            }
        }
        for (size_t i = 0; i < n; ++i)
        {
            if (needed[i])
            {
                m_inputs.push_back(static_cast<int32_t>(i));
            }
        }
    }

    bool RigSculptor::has_work(const void* key) const
    {
        if (!m_plan.empty())
        {
            return true;
        }
        for (const Memo& m : m_memos)
        {
            if (m.buffer == key && !m.marked_list.empty())
            {
                return true;
            }
        }
        return false;
    }

    void RigSculptor::gather_inputs(const void* key, std::vector<int32_t>& out) const
    {
        out = m_inputs;
        for (const Memo& m : m_memos)
        {
            if (m.buffer == key && m.marked.size() == m_names.size())
            {
                out.insert(out.end(), m.marked_list.begin(), m.marked_list.end());
            }
        }
    }

    bool RigSculptor::last_sample(int32_t bone, Xform& source, Xform& result) const
    {
        if (bone < 0 || static_cast<size_t>(bone) >= m_last_ok.size() || !m_last_ok[bone])
        {
            return false;
        }
        source = m_source[bone];
        result = m_fresh[bone];
        return true;
    }

    Xform RigSculptor::untouched(const void* key, int32_t bone, const Xform& current) const
    {
        for (const Memo& m : m_memos)
        {
            if (m.buffer == key && bone >= 0 && static_cast<size_t>(bone) < m.marked.size() && m.marked[bone] &&
                xf::same_bits(current, m.result[bone]))
            {
                return m.source[bone];
            }
        }
        return current;
    }

    void RigSculptor::forget_buffers()
    {
        for (Memo& m : m_memos)
        {
            m.buffer = nullptr;
            m.last_use = 0;
            m.source.clear();
            m.result.clear();
            m.marked.clear();
            m.marked_list.clear();
        }
    }

    RigSculptor::Memo& RigSculptor::memo_for(const void* buffer)
    {
        const size_t n = m_names.size();
        ++m_clock;
        Memo* oldest = &m_memos[0];
        for (Memo& m : m_memos)
        {
            if (m.buffer == buffer && m.marked.size() == n)
            {
                m.last_use = m_clock;
                return m;
            }
            if (m.last_use < oldest->last_use)
            {
                oldest = &m;
            }
        }
        Memo& m = *oldest;
        m.buffer = buffer;
        m.last_use = m_clock;
        m.source.assign(n, xf::identity());
        m.result.assign(n, xf::identity());
        m.marked.assign(n, 0);
        m.marked_list.clear();
        return m;
    }

    int32_t RigSculptor::apply(Xform* pose, int32_t count, const void* key)
    {
        const int32_t n = bone_count();
        if (!pose || count != n || n == 0)
        {
            return 0;
        }
        if (!key)
        {
            key = pose;
        }
        m_touched.clear();
        m_last_lift = 0.0;
        if (!has_work(key))
        {
            return 0; // nothing planned and nothing of ours left in this buffer
        }

        Memo& memo = memo_for(key);
        int32_t written = 0;
        std::fill(m_last_ok.begin(), m_last_ok.end(), 0);

        // 1) Put back bones we edited earlier but no longer plan to touch, as long as
        //    the engine has not refreshed them since (i.e. they still hold our output).
        for (const int32_t i : memo.marked_list)
        {
            if (!m_in_plan[i])
            {
                if (xf::same_bits(pose[i], memo.result[i]))
                {
                    pose[i] = memo.source[i];
                    m_touched.push_back(i);
                    ++written;
                }
                memo.marked[i] = 0;
            }
        }

        // 2) Recover the untouched pose for everything we are about to rebuild.
        for (const int32_t i : m_plan)
        {
            const bool ours = memo.marked[i] && xf::same_bits(pose[i], memo.result[i]);
            m_source[i] = ours ? memo.source[i] : pose[i];
        }

        // 2b) Another mesh of a character: is its pose a copy of the main mesh's edited pose?
        m_scratch_pose = pose;
        decide_copy();
        // A copy already carries the main mesh's edits on every bone it shares with it: leave those alone.
        const bool copy = !m_ground_primary && m_copy_state == 1 && m_map != nullptr;
        auto shared_bone = [&](int32_t b) { return copy && b >= 0 && m_map[b] >= 0; };

        // 3) Rebuild top-down (parents always precede children).
        const Xform unit = xf::identity();
        for (const int32_t i : m_plan)
        {
            if (shared_bone(i))
            {
                // copied from the main mesh's finished pose: keep it exactly as it is
                m_fresh[i] = m_source[i];
                m_for_children[i] = m_source[i];
                const int32_t slot = m_edit_slot[i];
                if (slot >= 0 && m_scales[slot].spread == Spread::Solo)
                {
                    // "This bone only": on the main mesh its children hang from the bone as it was before its own
                    // edit. This mesh's own bones below it (face, hair...) must do the same: take the edit back out.
                    const BoneEdit& s = m_scales[slot];
                    const int32_t p = m_parents[i];
                    const Xform& parent_after = p < 0 ? unit : (m_in_plan[p] ? m_for_children[p] : pose[p]);
                    Xform local = p < 0 ? m_source[i] : xf::relative(m_source[i], parent_after);
                    for (int a = 0; a < 3; ++a)
                    {
                        local.scl[a] /= s.axis[a];
                    }
                    if (s.has_turn())
                    {
                        double undo[4], rot[4];
                        xf::qconj(m_turns[slot].data(), undo);
                        xf::qmul(local.rot, undo, rot);
                        std::memcpy(local.rot, rot, sizeof rot);
                    }
                    if (s.has_shift())
                    {
                        double offset[3];
                        xf::qrotate(local.rot, s.shift, offset);
                        for (int a = 0; a < 3; ++a)
                        {
                            local.pos[a] -= offset[a] / (std::fabs(parent_after.scl[a]) > xf::kTiny ? parent_after.scl[a] : 1.0);
                        }
                    }
                    m_for_children[i] = xf::compose(local, parent_after);
                    m_for_children[i].pos[3] = m_for_children[i].scl[3] = 0.0;
                }
                continue;
            }
            const int32_t p = m_parents[i];
            const Xform& parent_before = p < 0 ? unit : (m_in_plan[p] ? m_source[p] : pose[p]);
            const Xform& parent_after = p < 0 ? unit : (m_in_plan[p] ? m_for_children[p] : pose[p]);

            Xform local = p < 0 ? m_source[i] : xf::relative(m_source[i], parent_before);

            // "Keep children size" - also for this mesh's own bones below a bone copied from the main mesh: the copy
            // carries the parent's new size, which they must not take on.
            if (p >= 0 && m_edit_slot[p] >= 0)
            {
                const BoneEdit& ps = m_scales[m_edit_slot[p]];
                if (ps.spread == Spread::Keep)
                {
                    for (int a = 0; a < 3; ++a)
                    {
                        local.scl[a] /= ps.axis[a];
                    }
                }
                if (ps.spread != Spread::Solo)
                {
                    for (int a = 0; a < 3; ++a)
                    {
                        if (ps.joint[a] > 0.0 && std::isfinite(ps.joint[a]))
                        {
                            local.pos[a] *= ps.joint[a] / ps.axis[a]; // thicker, same joints
                        }
                    }
                }
            }

            const int32_t slot = shared_bone(i) ? -1 : m_edit_slot[i];
            if (slot >= 0)
            {
                const BoneEdit& s = m_scales[slot];
                Xform edited = local;
                // Move along the bone's own axes (expressed in the parent's space).
                if (s.has_shift())
                {
                    double offset[3];
                    xf::qrotate(local.rot, s.shift, offset);
                    for (int a = 0; a < 3; ++a)
                    {
                        edited.pos[a] += offset[a] / (std::fabs(parent_after.scl[a]) > xf::kTiny ? parent_after.scl[a] : 1.0);
                    }
                }
                // Rotate about the bone's own axes: the extra turn happens before the bone's rest rotation.
                if (s.has_turn())
                {
                    xf::qmul(local.rot, m_turns[slot].data(), edited.rot);
                }
                for (int a = 0; a < 3; ++a)
                {
                    edited.scl[a] *= s.axis[a];
                }
                m_fresh[i] = xf::compose(edited, parent_after);
                m_for_children[i] = s.spread == Spread::Solo ? xf::compose(local, parent_after) : m_fresh[i];
            }
            else
            {
                m_fresh[i] = xf::compose(local, parent_after);
                m_for_children[i] = m_fresh[i];
            }
            if (p < 0 && m_world != 1.0)
            {
                // Uniform scale about the component origin; children pick it up through parent_after.
                for (int a = 0; a < 3; ++a)
                {
                    m_fresh[i].pos[a] *= m_world;
                    m_fresh[i].scl[a] *= m_world;
                    m_for_children[i].pos[a] *= m_world;
                    m_for_children[i].scl[a] *= m_world;
                }
            }
            m_fresh[i].pos[3] = 0.0;
            m_fresh[i].scl[3] = 0.0;
        }

        // 3b) Auto Ground: lift the pose and bend the other legs back onto the ground.
        apply_ground(pose);

        // 3c) Hand Contact: hands (and elbows, fingers, feet, knees) resting on the body follow the edited body.
        apply_hands(pose);
        apply_feet(pose);

        if (m_quantize)
        {
            for (const int32_t i : m_plan)
            {
                for (int a = 0; a < 4; ++a)
                {
                    m_fresh[i].rot[a] = static_cast<double>(static_cast<float>(m_fresh[i].rot[a]));
                    m_fresh[i].pos[a] = static_cast<double>(static_cast<float>(m_fresh[i].pos[a]));
                    m_fresh[i].scl[a] = static_cast<double>(static_cast<float>(m_fresh[i].scl[a]));
                }
            }
        }

        // 4) Commit, remembering what we started from and what we wrote.
        for (const int32_t i : m_plan)
        {
            if (!xf::finite(m_fresh[i]))
            {
                continue;
            }
            pose[i] = m_fresh[i];
            memo.source[i] = m_source[i];
            memo.result[i] = m_fresh[i];
            memo.marked[i] = 1;
            m_last_ok[i] = 1;
            m_touched.push_back(i);
            ++written;
        }

        // Main mesh: publish where every bone was before and after, for the character's other meshes.
        if (m_ground_primary)
        {
            m_pub_before.resize(static_cast<size_t>(n) * kPublishStride);
            m_pub_after.resize(static_cast<size_t>(n) * kPublishStride);
            for (int32_t b = 0; b < n; ++b)
            {
                const Xform& src = m_in_plan[b] ? m_source[b] : pose[b];
                double* pb = &m_pub_before[static_cast<size_t>(b) * kPublishStride];
                double* pa = &m_pub_after[static_cast<size_t>(b) * kPublishStride];
                for (int a = 0; a < 3; ++a)
                {
                    pb[a] = src.pos[a];
                    pa[a] = pose[b].pos[a];
                    pb[3 + a] = src.scl[a];
                    pa[3 + a] = pose[b].scl[a];
                }
                for (int a = 0; a < 4; ++a)
                {
                    pb[6 + a] = src.rot[a];
                    pa[6 + a] = pose[b].rot[a];
                }
            }
        }

        // Ground diagnostics: landmark heights before (the game's pose) and after.
        for (int k = 0; k < 3; ++k)
        {
            const int32_t b = m_marks[k];
            if (b >= 0 && b < n)
            {
                m_mark_src[k] = m_in_plan[b] ? m_source[b].pos[2] : pose[b].pos[2];
                m_mark_out[k] = pose[b].pos[2];
            }
        }

        // Step 1 unmarked every earlier bone outside the plan, so only planned bones can be marked.
        memo.marked_list.clear();
        for (const int32_t i : m_plan)
        {
            if (memo.marked[i])
            {
                memo.marked_list.push_back(i);
            }
        }
        return written;
    }

    // Compares this mesh's untouched pose with where the main mesh's bones were before and after its edit
    // (bones the edit moved at least a little). Needs three frames in a row to change its mind.
    void RigSculptor::decide_copy()
    {
        if (m_ground_primary || !m_map || !m_ref_before || !m_ref_after || m_ref_count == 0 || !m_scratch_pose)
        {
            return;
        }
        const size_t n = m_names.size();
        double err_after = 0.0, err_before = 0.0;
        size_t count = 0;
        auto published = [&](const double* base, int32_t j) {
            const double* v = base + static_cast<size_t>(j) * kPublishStride;
            Xform x{};
            for (int a = 0; a < 3; ++a)
            {
                x.pos[a] = v[a];
                x.scl[a] = v[3 + a];
            }
            for (int a = 0; a < 4; ++a)
            {
                x.rot[a] = v[6 + a];
            }
            return x;
        };
        auto source_of = [&](size_t i) -> const Xform& { return m_in_plan[i] ? m_source[i] : m_scratch_pose[i]; };
        // position in cm, scale weighted so 1% counts like half a centimetre, rotation 1 degree like 2 mm
        auto gap = [](const Xform& x, const Xform& r) {
            const double dp[3] = {x.pos[0] - r.pos[0], x.pos[1] - r.pos[1], x.pos[2] - r.pos[2]};
            const double ds[3] = {x.scl[0] - r.scl[0], x.scl[1] - r.scl[1], x.scl[2] - r.scl[2]};
            return vlen(dp) + 50.0 * vlen(ds) + 0.2 * angle_deg(x.rot, r.rot);
        };
        for (size_t i = 0; i < n; ++i)
        {
            const int32_t j = m_map[i];
            if (j < 0 || static_cast<size_t>(j) >= m_ref_count)
            {
                continue;
            }
            Xform src = source_of(i);
            Xform b = published(m_ref_before, j);
            Xform a = published(m_ref_after, j);
            // Below a shared parent, compare the bone as seen from its parent: the edit shows there just the same, while
            // the motion of the body as a whole cancels out (in case this mesh is a frame apart from the main mesh).
            // Bones without one (the root, the pelvis of a head mesh) are compared as they are: that is where a lift shows.
            const int32_t p = m_parents[i];
            const int32_t pj = p >= 0 ? m_map[p] : -1;
            if (pj >= 0 && static_cast<size_t>(pj) < m_ref_count)
            {
                src = xf::relative(src, source_of(static_cast<size_t>(p)));
                b = xf::relative(b, published(m_ref_before, pj));
                a = xf::relative(a, published(m_ref_after, pj));
            }
            const double moved_pos[3] = {a.pos[0] - b.pos[0], a.pos[1] - b.pos[1], a.pos[2] - b.pos[2]};
            const double moved_scl[3] = {a.scl[0] - b.scl[0], a.scl[1] - b.scl[1], a.scl[2] - b.scl[2]};
            if (vlen(moved_pos) < 0.05 && vlen(moved_scl) < 0.002 && angle_deg(a.rot, b.rot) < 0.1)
            {
                continue; // the edit did not change this bone: says nothing
            }
            err_after += gap(src, a);
            err_before += gap(src, b);
            ++count;
        }
        if (count == 0)
        {
            return; // nothing moved: keep what we knew
        }
        err_after /= static_cast<double>(count);
        err_before /= static_cast<double>(count);
        m_copy_err_after = err_after;
        m_copy_err_before = err_before;
        const int candidate = (err_after < 1.5 && err_after < 0.5 * err_before) ? 1 : (err_before < 1.5 && err_before < 0.5 * err_after) ? 2 : 0;
        // An unclear frame (fast motion while the other mesh is a frame apart) says nothing: keep what we knew,
        // or the mesh would get its shared bones edited a second time for a moment.
        if (candidate == m_copy_state || candidate == 0)
        {
            m_copy_votes = 0;
            return;
        }
        if (candidate == m_copy_candidate)
        {
            ++m_copy_votes;
        }
        else
        {
            m_copy_candidate = candidate;
            m_copy_votes = 1;
        }
        if (m_copy_votes >= 3)
        {
            m_copy_state = candidate;
            m_copy_votes = 0;
        }
    }

    // m_source holds the game's pose (its foot IK put the feet on the ground), m_fresh the edited
    // one. Both are complete here: with Auto Ground on, every bone is in the plan.
    void RigSculptor::apply_ground(Xform* /*pose*/)
    {
        const size_t n = m_names.size();
        if (!m_ground.enabled || m_plan.size() != n || n == 0)
        {
            return;
        }

        // Other meshes of the character (head, outfit, even ones on the full skeleton with legs of their own):
        // where does their pose come from? Told apart by the hip bone against the main mesh's hip:
        //   * at the main mesh's hip *after* the edit: the pose is a copy of the main mesh's final, grounded pose
        //     (Copy Pose / Leader Pose) - it is already lifted, so nothing to add (lifting it again floats it);
        //   * at the main mesh's hip *before* the edit: animated on its own - lifted like the main mesh;
        //   * anywhere else (hanging from a bone, another space): left alone.
        m_took_shared = false;
        m_follows = false;
        bool own_legs = !m_legs.empty();
        if (!m_ground_primary)
        {
            if (m_probe >= 0 && static_cast<size_t>(m_probe) < n)
            {
                m_probe_seen = m_source[m_probe].pos[2];
            }
            if (m_copy_state == 1)
            {
                m_follows = true; // a copy of the main mesh's pose: already grounded
                return;
            }
            if (m_copy_state != 2)
            {
                return; // not known (yet) or neither: leave it alone
            }
            own_legs = !m_legs.empty(); // animated on its own: measure its own legs (or take the shared lift)
        }

        // How fade_cm each foot's sole sank because of the edit (component space, +Z is up).
        double raw = 0.0;
        double total = 0.0;
        std::array<double, 4> sink{};
        if (own_legs)
        {
            raw = -1.0e30;
            for (size_t li = 0; li < m_legs.size(); ++li)
            {
                sink[li] = contact_sink(m_legs[li]);
                raw = std::max(raw, sink[li]);
                const int32_t f = m_legs[li].foot;
                m_leg_diag[li] = {m_source[f].pos[2] - m_fresh[f].pos[2], sink[li], 0.0};
            }
            if (!std::isfinite(raw))
            {
                return;
            }
            m_raw_lift = raw;
            // Lifting by the foot that sank most leaves the others in the air; their legs bend down to the ground,
            // but a leg can only stretch until it is straight. When one would have to be longer than that (one leg
            // edited more than the other, a turned pelvis or thigh, a moved foot), lift less: every leg can then reach,
            // and a foot that would now be below the ground is bent up instead.
            if (m_ground.leg_ik)
            {
                for (size_t li = 0; li < m_legs.size(); ++li)
                {
                    const LegChain& leg = m_legs[li];
                    const double* H = m_fresh[leg.thigh].pos;
                    const double* K = m_fresh[leg.calf].pos;
                    const double* F = m_fresh[leg.foot].pos;
                    const double hk[3] = {K[0] - H[0], K[1] - H[1], K[2] - H[2]};
                    const double kf[3] = {F[0] - K[0], F[1] - K[1], F[2] - K[2]};
                    const double reach = vlen(hk) + vlen(kf) - 1.0e-3;
                    const double across2 = (F[0] - H[0]) * (F[0] - H[0]) + (F[1] - H[1]) * (F[1] - H[1]);
                    if (reach > 0.0 && across2 < reach * reach)
                    {
                        // foot target height (before the lift) minus hip height, plus the straight leg's height
                        const double most = (F[2] + sink[li] - H[2]) + std::sqrt(reach * reach - across2);
                        m_leg_diag[li][2] = most;
                        raw = std::min(raw, most);
                    }
                }
            }
            total = raw + m_ground.extra;
        }
        else if (!m_ground_primary)
        {
            total = m_shared_lift; // independent mesh without legs: lift it with the main mesh
            m_took_shared = true;
        }
        else
        {
            return; // a main mesh without legs: nothing to measure
        }
        if (!std::isfinite(total))
        {
            return;
        }
        total = std::min(m_ground.max, std::max(-m_ground.max, total));
        m_last_lift = total;

        // The foot that sank most goes back exactly onto the ground; lift everything by that much.
        if (std::fabs(total) > 1.0e-6)
        {
            for (size_t i = 0; i < n; ++i)
            {
                m_fresh[i].pos[2] += total;
            }
        }

        // Hip height before / after, for the character's other meshes to compare against.
        if (!m_legs.empty())
        {
            const int32_t hip = hip_bone();
            if (hip >= 0)
            {
                m_hip_before = m_source[hip].pos[2];
                m_hip_after = m_fresh[hip].pos[2];
            }
        }

        // Every foot is now (lift - its sink) off the ground: bend its leg down (or up) to where the game had it.
        if (own_legs && m_ground.leg_ik)
        {
            const double lifted = total - m_ground.extra; // the lift without the extra (which every foot keeps)
            for (size_t li = 0; li < m_legs.size(); ++li)
            {
                const double drop = std::min(m_ground.max, std::max(-m_ground.max, lifted - sink[li]));
                if (std::fabs(drop) > 0.01)
                {
                    bend_leg(m_legs[li], li, drop);
                }
            }
        }
    }

    // How fade_cm the sole sank: the lower of the ball (toes) bone and a heel point the ball's height below the ankle,
    // both carried by the foot - so a bigger, smaller or turned foot is measured where it touches the ground.
    // Without a ball bone, the ankle joint itself.
    double RigSculptor::contact_sink(const LegChain& leg) const
    {
        const Xform& was = m_source[leg.foot];
        const Xform& now = m_fresh[leg.foot];
        if (leg.ball < 0)
        {
            return was.pos[2] - now.pos[2];
        }
        double sink = m_source[leg.ball].pos[2] - m_fresh[leg.ball].pos[2];
        const double h = was.pos[2] - m_source[leg.ball].pos[2];
        if (h > 0.0)
        {
            const double down[3] = {0.0, 0.0, -h};
            double inv[4], local[3], scaled[3], turned[3];
            xf::qconj(was.rot, inv);
            xf::qrotate(inv, down, local);
            for (int a = 0; a < 3; ++a)
            {
                scaled[a] = now.scl[a] * local[a] * xf::safe_inverse(was.scl[a]);
            }
            xf::qrotate(now.rot, scaled, turned);
            sink = std::max(sink, (was.pos[2] - h) - (now.pos[2] + turned[2]));
        }
        return sink;
    }

    // Two-bone IK in component space: moves the foot `drop` cm down (+Z is up) by turning the thigh
    // and the calf in the plane the leg already bends in. The foot keeps its rotation; everything below
    // each link follows it rigidly.
    void RigSculptor::bend_leg(const LegChain& leg, size_t leg_index, double drop)
    {
        const double* hip = m_fresh[leg.thigh].pos;
        const double* knee = m_fresh[leg.calf].pos;
        const double* foot = m_fresh[leg.foot].pos;
        const double H[3] = {hip[0], hip[1], hip[2]};
        const double K[3] = {knee[0], knee[1], knee[2]};
        const double F[3] = {foot[0], foot[1], foot[2]};
        const double T[3] = {F[0], F[1], F[2] - drop};

        const double hk[3] = {K[0] - H[0], K[1] - H[1], K[2] - H[2]};
        const double kf[3] = {F[0] - K[0], F[1] - K[1], F[2] - K[2]};
        const double a = vlen(hk), b = vlen(kf);
        double ht[3] = {T[0] - H[0], T[1] - H[1], T[2] - H[2]};
        double d = vlen(ht);
        if (a < 1.0e-3 || b < 1.0e-3 || d < 1.0e-3)
        {
            return;
        }
        const double u[3] = {ht[0] / d, ht[1] / d, ht[2] / d};
        // Out of reach: the leg straightens (or folds) as far as it can.
        d = std::min(a + b - 1.0e-4, std::max(std::fabs(a - b) + 1.0e-3, d));

        // Bend direction: where the knee points now, across the hip -> target line.
        double v[3];
        const double along = vdot(hk, u);
        for (int k = 0; k < 3; ++k)
        {
            v[k] = hk[k] - along * u[k];
        }
        double vl = vlen(v);
        if (vl < 0.01 * a && leg.ball >= 0)
        {
            // (nearly) straight leg pointing at the target - which way it bends is noise: the knee goes the way the
            // toes point
            const double* B = m_fresh[leg.ball].pos;
            const double toes[3] = {B[0] - F[0], B[1] - F[1], B[2] - F[2]};
            const double t_along = vdot(toes, u);
            for (int k = 0; k < 3; ++k)
            {
                v[k] = toes[k] - t_along * u[k];
            }
            vl = vlen(v);
        }
        if (vl < 1.0e-4)
        {
            // still nothing to go by: bend about the thigh's own Z axis
            const double z[3] = {0.0, 0.0, 1.0};
            double w[3];
            xf::qrotate(m_fresh[leg.thigh].rot, z, w);
            vcross(w, u, v);
            vl = vlen(v);
            if (vl < 1.0e-4)
            {
                return;
            }
        }
        for (int k = 0; k < 3; ++k)
        {
            v[k] /= vl;
        }

        const double cos_hip = std::min(1.0, std::max(-1.0, (a * a + d * d - b * b) / (2.0 * a * d)));
        const double sin_hip = std::sqrt(std::max(0.0, 1.0 - cos_hip * cos_hip));
        double K2[3], F2[3];
        for (int k = 0; k < 3; ++k)
        {
            K2[k] = H[k] + a * (cos_hip * u[k] + sin_hip * v[k]);
            F2[k] = H[k] + d * u[k];
        }

        // Thigh: turn about the hip so the knee lands on K2.
        const double hk2[3] = {K2[0] - H[0], K2[1] - H[1], K2[2] - H[2]};
        double r1[4];
        if (!arc(hk, hk2, r1))
        {
            return;
        }
        // Calf: after the thigh's turn, turn about the new knee so the foot lands on F2.
        double kf_turned[3];
        xf::qrotate(r1, kf, kf_turned);
        const double kf2[3] = {F2[0] - K2[0], F2[1] - K2[1], F2[2] - K2[2]};
        double r2[4];
        if (!arc(kf_turned, kf2, r2))
        {
            r2[0] = r2[1] = r2[2] = 0.0;
            r2[3] = 1.0;
        }
        double r21[4];
        xf::qmul(r2, r1, r21);
        const double shift[3] = {F2[0] - F[0], F2[1] - F[1], F2[2] - F[2]};

        const int8_t li = static_cast<int8_t>(leg_index);
        for (size_t i = static_cast<size_t>(leg.thigh); i < m_names.size(); ++i)
        {
            if (m_leg_of[i] != li)
            {
                continue;
            }
            Xform& x = m_fresh[i];
            double rot[4];
            switch (m_leg_part[i])
            {
            case 1: // thigh and what hangs from it above the knee (twist bones, muscles)
                turn_about(r1, H, x.pos);
                xf::qmul(r1, x.rot, rot);
                break;
            case 2: // calf and what hangs from it above the foot
                turn_about(r1, H, x.pos);
                turn_about(r2, K2, x.pos);
                xf::qmul(r21, x.rot, rot);
                break;
            case 3: // foot, ball, toes: same rotation, new place
                for (int k = 0; k < 3; ++k)
                {
                    x.pos[k] += shift[k];
                }
                continue;
            default:
                continue;
            }
            qnormalize(rot);
            for (int k = 0; k < 4; ++k)
            {
                x.rot[k] = rot[k];
            }
        }
    }
    // ---- Hand Contact ---------------------------------------------------------------------------------------------

    namespace
    {
        // v (component space) as it moves with bone x's edit: into x's own axes before the edit, and back out after it
        // (turned and scaled with the bone).
        void carry(const Xform& before, const Xform& after, const double v[3], double out[3])
        {
            double inv[4], local[3];
            xf::qconj(before.rot, inv);
            xf::qrotate(inv, v, local);
            for (int a = 0; a < 3; ++a)
            {
                local[a] = local[a] * xf::safe_inverse(before.scl[a]) * after.scl[a];
            }
            xf::qrotate(after.rot, local, out);
        }

        // how bone x turned through the edit (component space)
        void turn_of(const Xform& before, const Xform& after, double out[4])
        {
            double inv[4];
            xf::qconj(before.rot, inv);
            xf::qmul(after.rot, inv, out);
            qnormalize(out);
        }

        // a + t (b - a) for quaternions, on the same side as a, normalised
        void nlerp(const double a[4], const double b[4], double t, double out[4])
        {
            const double sign = (a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3]) < 0.0 ? -1.0 : 1.0;
            for (int k = 0; k < 4; ++k)
            {
                out[k] = a[k] + t * (sign * b[k] - a[k]);
            }
            qnormalize(out);
        }

        double smooth(double x)
        {
            x = std::min(1.0, std::max(0.0, x));
            return x * x * (3.0 - 2.0 * x);
        }
    } // namespace

    // For every arm: where its palm is in the game's pose relative to the body bones full_cm it, and where that place is
    // on the edited body. The hand is moved (and turned) there by as much as it is close to the body, and the arm
    // bent with a two-bone IK to reach it.
    void RigSculptor::apply_hands(const Xform* pose)
    {
        for (HandSample& h : m_hand_samples)
        {
            h = HandSample{};
        }
        if (!m_hands.enabled || !m_ground_primary || m_arms.empty() || m_body_bones.empty() || !pose || m_hands.strength <= 0.0)
        {
            return;
        }
        auto src = [&](int32_t b) -> const Xform& { return m_in_plan[b] ? m_source[b] : pose[b]; };
        auto palm_of = [&](const ArmChain& arm, const Xform& hand, const Xform* palm, double p[3]) {
            for (int a = 0; a < 3; ++a)
            {
                p[a] = palm ? hand.pos[a] + 0.5 * (palm->pos[a] - hand.pos[a]) : hand.pos[a];
            }
            (void)arm;
        };

        for (size_t ai = 0; ai < m_arms.size(); ++ai)
        {
            const ArmChain& arm = m_arms[ai];
            if (!m_in_plan[arm.upper] || !m_in_plan[arm.lower] || !m_in_plan[arm.hand])
            {
                continue;
            }
            const Xform& su = src(arm.upper);
            const Xform& sl = src(arm.lower);
            const Xform& sh = src(arm.hand);
            const double ul[3] = {sl.pos[0] - su.pos[0], sl.pos[1] - su.pos[1], sl.pos[2] - su.pos[2]};
            const double lh[3] = {sh.pos[0] - sl.pos[0], sh.pos[1] - sl.pos[1], sh.pos[2] - sl.pos[2]};
            const double length = vlen(ul) + vlen(lh);
            if (!(length > 1.0e-3))
            {
                continue;
            }
            const double full_cm = m_hands.full_within * length;
            const double fade_cm = std::max(full_cm + 1.0e-3, m_hands.fade_out * length);

            double P[3];
            palm_of(arm, sh, arm.palm >= 0 ? &src(arm.palm) : nullptr, P);

            double best_d = 1.0e30;
            int32_t best_bone = -1;
            double goal[3], turn[4];
            const double best = body_spot(pose, P, full_cm, fade_cm, goal, turn, best_d, best_bone);
            HandSample& sample = m_hand_samples[ai];
            sample.anchor = best_bone;
            sample.distance = best_bone >= 0 ? best_d : 0.0;
            const double weight = best * m_hands.strength;

            // the elbow near the body (arms at the sides of a wider waist, an elbow on a knee): it follows the body too
            double elbow_goal[3], elbow_turn[4], elbow_d = 0.0;
            int32_t elbow_bone = -1;
            // an elbow counts only when it is close: half the hand's distances (arms hang near the body all the time)
            const double elbow_w =
                body_spot(pose, sl.pos, 0.5 * full_cm, 0.5 * fade_cm, elbow_goal, elbow_turn, elbow_d, elbow_bone) * m_hands.strength;
            double pole_at[3];
            const double* elbow_pole = nullptr;
            if (elbow_w > 1.0e-4)
            {
                const double* K = m_fresh[arm.lower].pos;
                for (int c = 0; c < 3; ++c)
                {
                    pole_at[c] = K[c] + elbow_w * (elbow_goal[c] - K[c]);
                }
                elbow_pole = pole_at;
            }
            if (weight < 1.0e-4)
            {
                if (!elbow_pole)
                {
                    continue;
                }
                // only the elbow touches: the whole arm turns about the shoulder so the elbow goes where the body wants it
                const double* H = m_fresh[arm.upper].pos;
                const double* K = m_fresh[arm.lower].pos;
                const double hk[3] = {K[0] - H[0], K[1] - H[1], K[2] - H[2]};
                const double hp[3] = {pole_at[0] - H[0], pole_at[1] - H[1], pole_at[2] - H[2]};
                double q[4];
                if (!arc(hk, hp, q))
                {
                    continue;
                }
                const Xform hand_now = m_fresh[arm.hand];
                double target[3] = {hand_now.pos[0], hand_now.pos[1], hand_now.pos[2]};
                const double Hc[3] = {H[0], H[1], H[2]};
                turn_about(q, Hc, target);
                double rot[4];
                xf::qmul(q, hand_now.rot, rot);
                qnormalize(rot);
                sample.anchor = elbow_bone;
                sample.distance = elbow_d;
                reach_limb(arm_limb(ai), sample, target, rot, elbow_pole);
                const double moved[3] = {m_fresh[arm.hand].pos[0] - hand_now.pos[0], m_fresh[arm.hand].pos[1] - hand_now.pos[1],
                                         m_fresh[arm.hand].pos[2] - hand_now.pos[2]};
                sample.weight = elbow_w;
                sample.moved = vlen(moved);
                sample.elbow = true;
                continue;
            }

            // where the edit (and Auto Ground) put the hand, and where the body wants it
            const Xform hand_now = m_fresh[arm.hand];
            double palm_now[3];
            palm_of(arm, hand_now, arm.palm >= 0 ? &m_fresh[arm.palm] : nullptr, palm_now);
            double wanted_rot[4], rot[4];
            xf::qmul(turn, sh.rot, wanted_rot);
            nlerp(hand_now.rot, wanted_rot, weight, rot);
            double spin[4], inv[4];
            xf::qconj(hand_now.rot, inv);
            xf::qmul(rot, inv, spin);
            const double v[3] = {palm_now[0] - hand_now.pos[0], palm_now[1] - hand_now.pos[1], palm_now[2] - hand_now.pos[2]};
            double v2[3];
            xf::qrotate(spin, v, v2);
            double target[3];
            for (int c = 0; c < 3; ++c)
            {
                const double palm_goal = palm_now[c] + weight * (goal[c] - palm_now[c]);
                target[c] = palm_goal - v2[c];
            }
            reach_limb(arm_limb(ai), sample, target, rot, elbow_pole);
            const double* moved_to = m_fresh[arm.hand].pos;
            const double moved[3] = {moved_to[0] - hand_now.pos[0], moved_to[1] - hand_now.pos[1], moved_to[2] - hand_now.pos[2]};
            sample.weight = weight;
            sample.moved = vlen(moved);

            // Fingers: each finger joint turns so the joint below it lands where the edited body has its spot (a
            // thicker or bigger body part opens the grip, a thinner one closes it). Any finger bones, any body part.
            if (m_hands.fingers)
            {
                // what the arm could not reach, the fingers do not chase either
                double palm_after[3];
                palm_of(arm, m_fresh[arm.hand], arm.palm >= 0 ? &m_fresh[arm.palm] : nullptr, palm_after);
                double miss[3];
                for (int c = 0; c < 3; ++c)
                {
                    miss[c] = palm_after[c] - (palm_now[c] + weight * (goal[c] - palm_now[c]));
                }
                sample.fingers = shape_fingers(pose, arm, ai, full_cm, fade_cm, miss);
            }
        }
    }

    // Feet (and knees) that rest on the body in the game's pose - crossed legs, a foot on the other knee, knees
    // together - follow the edited body like hands do: the leg bends with a two-bone IK. A foot that is only near its
    // own leg does not count, and a foot on the ground is left to Auto Ground.
    void RigSculptor::apply_feet(const Xform* pose)
    {
        for (HandSample& h : m_foot_samples)
        {
            h = HandSample{};
        }
        if (!m_hands.enabled || !m_hands.feet || !m_ground_primary || m_legs.empty() || m_body_bones.empty() || !pose || m_hands.strength <= 0.0)
        {
            return;
        }
        auto src = [&](int32_t b) -> const Xform& { return m_in_plan[b] ? m_source[b] : pose[b]; };
        for (size_t li = 0; li < m_legs.size(); ++li)
        {
            const LegChain& leg = m_legs[li];
            if (!m_in_plan[leg.thigh] || !m_in_plan[leg.calf] || !m_in_plan[leg.foot])
            {
                continue;
            }
            const Xform& st = src(leg.thigh);
            const Xform& sc = src(leg.calf);
            const Xform& sf = src(leg.foot);
            const double tc[3] = {sc.pos[0] - st.pos[0], sc.pos[1] - st.pos[1], sc.pos[2] - st.pos[2]};
            const double cf[3] = {sf.pos[0] - sc.pos[0], sf.pos[1] - sc.pos[1], sf.pos[2] - sc.pos[2]};
            const double length = vlen(tc) + vlen(cf);
            if (!(length > 1.0e-3))
            {
                continue;
            }
            // legs are thicker than arms: the same share of the leg's length would reach far; use half of it
            const double full_cm = 0.5 * m_hands.full_within * length;
            const double fade_cm = std::max(full_cm + 1.0e-3, 0.5 * m_hands.fade_out * length);
            double P[3];
            for (int c = 0; c < 3; ++c)
            {
                P[c] = leg.ball >= 0 ? 0.5 * (sf.pos[c] + src(leg.ball).pos[c]) : sf.pos[c];
            }
            HandSample& sample = m_foot_samples[li];
            double goal[3], turn[4], best_d = 0.0;
            int32_t best_bone = -1;
            const double weight = body_spot(pose, P, full_cm, fade_cm, goal, turn, best_d, best_bone, static_cast<int>(li)) * m_hands.strength;
            sample.anchor = best_bone;
            sample.distance = best_bone >= 0 ? best_d : 0.0;
            double knee_goal[3], knee_turn[4], knee_d = 0.0;
            int32_t knee_bone = -1;
            const double knee_w = body_spot(pose, sc.pos, full_cm, fade_cm, knee_goal, knee_turn, knee_d, knee_bone, static_cast<int>(li)) * m_hands.strength;
            if (weight < 1.0e-4)
            {
                continue; // knees alone are not moved (that would lift the foot off the ground); they only steer the bend
            }
            double pole_at[3];
            const double* pole = nullptr;
            if (knee_w > 1.0e-4)
            {
                const double* K = m_fresh[leg.calf].pos;
                for (int c = 0; c < 3; ++c)
                {
                    pole_at[c] = K[c] + knee_w * (knee_goal[c] - K[c]);
                }
                pole = pole_at;
            }
            const Xform foot_now = m_fresh[leg.foot];
            double point_now[3];
            for (int c = 0; c < 3; ++c)
            {
                point_now[c] = leg.ball >= 0 ? 0.5 * (foot_now.pos[c] + m_fresh[leg.ball].pos[c]) : foot_now.pos[c];
            }
            double wanted_rot[4], rot[4], spin[4], inv[4];
            xf::qmul(turn, sf.rot, wanted_rot);
            nlerp(foot_now.rot, wanted_rot, weight, rot);
            xf::qconj(foot_now.rot, inv);
            xf::qmul(rot, inv, spin);
            const double v[3] = {point_now[0] - foot_now.pos[0], point_now[1] - foot_now.pos[1], point_now[2] - foot_now.pos[2]};
            double v2[3];
            xf::qrotate(spin, v, v2);
            double target[3];
            for (int c = 0; c < 3; ++c)
            {
                target[c] = point_now[c] + weight * (goal[c] - point_now[c]) - v2[c];
            }
            reach_limb(leg_limb(li), sample, target, rot, pole);
            const double moved[3] = {m_fresh[leg.foot].pos[0] - foot_now.pos[0], m_fresh[leg.foot].pos[1] - foot_now.pos[1],
                                     m_fresh[leg.foot].pos[2] - foot_now.pos[2]};
            sample.weight = weight;
            sample.moved = vlen(moved);
        }
    }

    double RigSculptor::body_spot(const Xform* pose, const double P[3], double full_cm, double fade_cm, double goal[3], double turn[4], double& best_d,
                                  int32_t& best_bone, int skip_leg)
    {
        auto src = [&](int32_t b) -> const Xform& { return m_in_plan[b] ? m_source[b] : pose[b]; };
        auto out = [&](int32_t b) -> const Xform& { return m_in_plan[b] ? m_fresh[b] : pose[b]; };
        // every body segment near the point votes for where it belongs now; the nearest ones decide
        // (a vote fades within a few cm of the nearest distance, so moving between segments never jumps)
        std::vector<HandNear>& close_list = m_hand_scratch;
        close_list.clear();
        best_d = 1.0e30;
        best_bone = -1;
        for (size_t k = 0; k < m_body_bones.size(); ++k)
        {
            const int32_t b = m_body_bones[k];
            const int32_t a = m_body_from[k];
            // a foot does not follow its own leg, nor another shin or foot (feet stand next to each other all the time)
            if (skip_leg >= 0 && (m_leg_of[b] == skip_leg || m_leg_of[a] == skip_leg || (m_leg_of[a] >= 0 && m_leg_part[a] >= 2)))
            {
                continue;
            }
            const Xform& sa = src(a);
            const Xform& sb = src(b);
            const double ab[3] = {sb.pos[0] - sa.pos[0], sb.pos[1] - sa.pos[1], sb.pos[2] - sa.pos[2]};
            const double ap[3] = {P[0] - sa.pos[0], P[1] - sa.pos[1], P[2] - sa.pos[2]};
            const double len2 = vdot(ab, ab);
            const double t = len2 > 1.0e-9 ? std::min(1.0, std::max(0.0, vdot(ap, ab) / len2)) : 1.0;
            HandNear e{k, t, 0.0, {P[0] - (sa.pos[0] + t * ab[0]), P[1] - (sa.pos[1] + t * ab[1]), P[2] - (sa.pos[2] + t * ab[2])}};
            e.d = vlen(e.o);
            if (!(e.d < fade_cm))
            {
                continue;
            }
            if (e.d < best_d)
            {
                best_d = e.d;
                best_bone = t > 0.5 ? b : a;
            }
            close_list.push_back(e);
        }
        if (close_list.empty())
        {
            return 0.0;
        }
        constexpr double kFade = 1.5; // cm
        double sum_w = 0.0;
        for (int c = 0; c < 3; ++c)
        {
            goal[c] = 0.0;
        }
        for (int c = 0; c < 4; ++c)
        {
            turn[c] = 0.0;
        }
        for (const HandNear& e : close_list)
        {
            const double w = std::exp(-(e.d - best_d) / kFade);
            if (w < 1.0e-4)
            {
                continue;
            }
            const int32_t b = m_body_bones[e.k];
            const int32_t a = m_body_from[e.k];
            const double t = e.t;
            // the same spot on the edited segment, with the offset carried by both ends' edits
            const Xform& sa = src(a);
            const Xform& sb = src(b);
            const Xform& oa = out(a);
            const Xform& ob = out(b);
            double ca[3], cb[3];
            carry(sa, oa, e.o, ca);
            carry(sb, ob, e.o, cb);
            double qa[4], qb[4], q[4];
            turn_of(sa, oa, qa);
            turn_of(sb, ob, qb);
            nlerp(qa, qb, t, q);
            for (int c = 0; c < 3; ++c)
            {
                const double spot = oa.pos[c] + t * (ob.pos[c] - oa.pos[c]);
                goal[c] += w * (spot + ca[c] + t * (cb[c] - ca[c]));
            }
            const double sign = (turn[0] * q[0] + turn[1] * q[1] + turn[2] * q[2] + turn[3] * q[3]) < 0.0 ? -1.0 : 1.0;
            for (int c = 0; c < 4; ++c)
            {
                turn[c] += w * sign * q[c];
            }
            sum_w += w;
        }
        if (!(sum_w > 0.0))
        {
            return 0.0;
        }
        for (int c = 0; c < 3; ++c)
        {
            goal[c] /= sum_w;
        }
        qnormalize(turn);
        if (!std::isfinite(goal[0] + goal[1] + goal[2] + turn[0] + turn[1] + turn[2] + turn[3]))
        {
            return 0.0;
        }
        return best_d <= full_cm ? 1.0 : smooth((fade_cm - best_d) / (fade_cm - full_cm));
    }

    // Every finger joint below the hand (parents first) turns so the joint below it goes where the edited body has that
    // joint's spot from the game's pose, by as much as that joint touched the body. At most 45 degrees per joint, bone
    // lengths kept. Returns how many joints turned.
    int RigSculptor::shape_fingers(const Xform* pose, const ArmChain& arm, size_t arm_index, double full_cm, double fade_cm, const double miss[3])
    {
        const int8_t ai = static_cast<int8_t>(arm_index);
        const int32_t n = static_cast<int32_t>(m_names.size());
        auto below = [&](int32_t i, int32_t j) {
            for (int32_t p = m_parents[i]; p >= j; p = m_parents[p])
            {
                if (p == j)
                {
                    return true;
                }
            }
            return false;
        };
        int turned = 0;
        for (int32_t j = arm.hand + 1; j < n; ++j)
        {
            if (m_arm_of[j] != ai || m_arm_part[j] != 3 || !m_in_plan[j])
            {
                continue;
            }
            // the joint below it (first child)
            int32_t c = -1;
            for (int32_t i = j + 1; i < n && c < 0; ++i)
            {
                if (m_parents[i] == j && m_in_plan[i])
                {
                    c = i;
                }
            }
            if (c < 0)
            {
                continue;
            }
            double goal[3], turn[4], best_d = 0.0;
            int32_t best_bone = -1;
            const double w = body_spot(pose, m_source[c].pos, full_cm, fade_cm, goal, turn, best_d, best_bone) * m_hands.strength;
            if (w < 1.0e-3)
            {
                continue;
            }
            const double* J = m_fresh[j].pos;
            const double* C = m_fresh[c].pos;
            const double now[3] = {C[0] - J[0], C[1] - J[1], C[2] - J[2]};
            double want[3];
            for (int k = 0; k < 3; ++k)
            {
                want[k] = C[k] + w * (goal[k] + miss[k] - C[k]) - J[k];
            }
            double q[4];
            if (!arc(now, want, q))
            {
                continue;
            }
            // at most 45 degrees per joint
            const double half = std::acos(std::min(1.0, std::max(-1.0, q[3])));
            constexpr double kMaxHalf = 0.3926990816987241; // 22.5 degrees (half of 45)
            if (half < 1.0e-5)
            {
                continue;
            }
            if (half > kMaxHalf)
            {
                const double sh = std::sin(kMaxHalf) / std::sin(half);
                q[0] *= sh;
                q[1] *= sh;
                q[2] *= sh;
                q[3] = std::cos(kMaxHalf);
            }
            const double Jc[3] = {J[0], J[1], J[2]};
            for (int32_t i = j; i < n; ++i)
            {
                if (i != j && !below(i, j))
                {
                    continue;
                }
                Xform& x = m_fresh[i];
                turn_about(q, Jc, x.pos);
                double r[4];
                xf::qmul(q, x.rot, r);
                qnormalize(r);
                std::memcpy(x.rot, r, sizeof r);
            }
            ++turned;
        }
        return turned;
    }

    // Two-bone IK in component space: turns the upper arm about the shoulder and the forearm about the elbow so the
    // wrist lands on `target` (as far as the arm reaches), bending in the plane the elbow already bends in. The hand
    // and everything below it take rotation `rot` and move with the wrist.
    void RigSculptor::reach_limb(const Limb& arm, HandSample& sample, const double target[3], const double rot[4], const double* pole)
    {
        const int8_t ai = arm.index;
        // Out of reach: first turn the collar bone toward the spot (the shoulder comes along).
        {
            const double* h0 = m_fresh[arm.upper].pos;
            const double* k0 = m_fresh[arm.lower].pos;
            const double* f0 = m_fresh[arm.end].pos;
            const double hk0[3] = {k0[0] - h0[0], k0[1] - h0[1], k0[2] - h0[2]};
            const double kf0[3] = {f0[0] - k0[0], f0[1] - k0[1], f0[2] - k0[2]};
            const double ht0[3] = {target[0] - h0[0], target[1] - h0[1], target[2] - h0[2]};
            const double reach0 = vlen(hk0) + vlen(kf0);
            if (arm.collar >= 0 && m_in_plan[arm.collar] && m_hands.shoulder_turn > 0.0 && vlen(ht0) > reach0 * 0.999)
            {
                const double* C = m_fresh[arm.collar].pos;
                const double Cc[3] = {C[0], C[1], C[2]};
                const double ch[3] = {h0[0] - C[0], h0[1] - C[1], h0[2] - C[2]};
                const double ct[3] = {target[0] - C[0], target[1] - C[1], target[2] - C[2]};
                double axis[3];
                vcross(ch, ct, axis);
                const double al = vlen(axis);
                const double full = std::atan2(al, vdot(ch, ct));
                const double limit = std::min(180.0, m_hands.shoulder_turn) * 3.14159265358979323846 / 180.0;
                const double angle = std::min(full, limit);
                if (al > 1.0e-9 && angle > 1.0e-6)
                {
                    const double sh = std::sin(angle * 0.5);
                    double q[4] = {axis[0] / al * sh, axis[1] / al * sh, axis[2] / al * sh, std::cos(angle * 0.5)};
                    for (size_t i = static_cast<size_t>(arm.collar); i < m_names.size(); ++i)
                    {
                        if ((*arm.of)[i] != ai || !m_in_plan[i])
                        {
                            continue;
                        }
                        Xform& x = m_fresh[i];
                        turn_about(q, Cc, x.pos);
                        double r[4];
                        xf::qmul(q, x.rot, r);
                        qnormalize(r);
                        std::memcpy(x.rot, r, sizeof r);
                    }
                    sample.turned = angle * 180.0 / 3.14159265358979323846;
                }
            }
        }

        const double H[3] = {m_fresh[arm.upper].pos[0], m_fresh[arm.upper].pos[1], m_fresh[arm.upper].pos[2]};
        const double K[3] = {m_fresh[arm.lower].pos[0], m_fresh[arm.lower].pos[1], m_fresh[arm.lower].pos[2]};
        const double F[3] = {m_fresh[arm.end].pos[0], m_fresh[arm.end].pos[1], m_fresh[arm.end].pos[2]};
        const double hand_rot[4] = {m_fresh[arm.end].rot[0], m_fresh[arm.end].rot[1], m_fresh[arm.end].rot[2], m_fresh[arm.end].rot[3]};

        const double hk[3] = {K[0] - H[0], K[1] - H[1], K[2] - H[2]};
        const double kf[3] = {F[0] - K[0], F[1] - K[1], F[2] - K[2]};
        const double a0 = vlen(hk), b0 = vlen(kf);
        const double ht[3] = {target[0] - H[0], target[1] - H[1], target[2] - H[2]};
        double d = vlen(ht);
        if (a0 < 1.0e-3 || b0 < 1.0e-3 || d < 1.0e-3)
        {
            return;
        }
        // still out of reach: the arm grows (up to `stretch`) to get there
        const double grow = std::min(std::max(1.0, m_hands.stretch), std::max(1.0, d / ((a0 + b0) * 0.999)));
        sample.stretched = grow;
        const double a = a0 * grow, b = b0 * grow;
        const double u[3] = {ht[0] / d, ht[1] / d, ht[2] / d};
        sample.shortfall = std::max(0.0, d - (a + b));
        d = std::min(a + b - 1.0e-4, std::max(std::fabs(a - b) + 1.0e-3, d));

        // bend direction: where the elbow points now, across the shoulder -> target line
        double v[3];
        const double along = vdot(hk, u);
        for (int k = 0; k < 3; ++k)
        {
            v[k] = hk[k] - along * u[k];
        }
        double vl = vlen(v);
        if (pole)
        {
            // the middle joint (elbow / knee) touched the body too: bend toward where the body wants it
            const double hp[3] = {pole[0] - H[0], pole[1] - H[1], pole[2] - H[2]};
            const double pa = vdot(hp, u);
            double pv[3];
            for (int k = 0; k < 3; ++k)
            {
                pv[k] = hp[k] - pa * u[k];
            }
            const double pl = vlen(pv);
            if (pl > 0.05 * a)
            {
                for (int k = 0; k < 3; ++k)
                {
                    v[k] = pv[k];
                }
                vl = pl;
            }
        }
        if (vl < 0.01 * a)
        {
            // (nearly) straight arm pointing at the target: the elbow keeps to the side it bends to in the
            // forearm's current direction, else about the upper arm's own Z axis
            const double z[3] = {0.0, 0.0, 1.0};
            double w[3];
            xf::qrotate(m_fresh[arm.upper].rot, z, w);
            vcross(w, u, v);
            vl = vlen(v);
            if (vl < 1.0e-4)
            {
                return;
            }
        }
        for (int k = 0; k < 3; ++k)
        {
            v[k] /= vl;
        }
        const double cos_s = std::min(1.0, std::max(-1.0, (a * a + d * d - b * b) / (2.0 * a * d)));
        const double sin_s = std::sqrt(std::max(0.0, 1.0 - cos_s * cos_s));
        double K2[3], F2[3];
        for (int k = 0; k < 3; ++k)
        {
            K2[k] = H[k] + a * (cos_s * u[k] + sin_s * v[k]);
            F2[k] = H[k] + d * u[k];
        }
        const double hk2[3] = {K2[0] - H[0], K2[1] - H[1], K2[2] - H[2]};
        double r1[4];
        if (!arc(hk, hk2, r1))
        {
            r1[0] = r1[1] = r1[2] = 0.0;
            r1[3] = 1.0;
        }
        double kf_turned[3];
        xf::qrotate(r1, kf, kf_turned);
        const double kf2[3] = {F2[0] - K2[0], F2[1] - K2[1], F2[2] - K2[2]};
        double r2[4];
        if (!arc(kf_turned, kf2, r2))
        {
            r2[0] = r2[1] = r2[2] = 0.0;
            r2[3] = 1.0;
        }
        double r21[4];
        xf::qmul(r2, r1, r21);
        double spin[4], inv[4];
        xf::qconj(hand_rot, inv);
        xf::qmul(rot, inv, spin);
        qnormalize(spin);

        // the elbow after the upper arm's turn, before any growth: forearm bones move on with the grown upper arm
        double elbow_turned[3] = {K[0], K[1], K[2]};
        turn_about(r1, H, elbow_turned);
        const double grow_shift[3] = {K2[0] - elbow_turned[0], K2[1] - elbow_turned[1], K2[2] - elbow_turned[2]};
        for (size_t i = static_cast<size_t>(arm.upper); i < m_names.size(); ++i)
        {
            if ((*arm.of)[i] != ai || !m_in_plan[i])
            {
                continue;
            }
            Xform& x = m_fresh[i];
            double q[4];
            switch ((*arm.part)[i])
            {
            case 1: // upper arm and what hangs from it above the elbow (twist bones, muscles)
                turn_about(r1, H, x.pos);
                xf::qmul(r1, x.rot, q);
                break;
            case 2: // forearm and what hangs from it above the wrist
                turn_about(r1, H, x.pos);
                for (int k = 0; k < 3; ++k)
                {
                    x.pos[k] += grow_shift[k];
                }
                turn_about(r2, K2, x.pos);
                xf::qmul(r21, x.rot, q);
                break;
            case 3: // hand and fingers: turned to `rot` about the wrist, moved with it
            {
                const double rel[3] = {x.pos[0] - F[0], x.pos[1] - F[1], x.pos[2] - F[2]};
                double turned[3];
                xf::qrotate(spin, rel, turned);
                for (int k = 0; k < 3; ++k)
                {
                    x.pos[k] = F2[k] + turned[k];
                }
                xf::qmul(spin, x.rot, q);
                break;
            }
            default:
                continue;
            }
            qnormalize(q);
            for (int k = 0; k < 4; ++k)
            {
                x.rot[k] = q[k];
            }
        }
    }

    std::string RigSculptor::hands_report() const
    {
        if (!m_hands.enabled)
        {
            return "off";
        }
        std::string s;
        char line[384];
        if (m_arms.empty())
        {
            s = "no arms found (upper arm -> forearm -> hand by name)";
        }
        for (size_t ai = 0; ai < m_arms.size(); ++ai)
        {
            const ArmChain& arm = m_arms[ai];
            const HandSample h = ai < m_hand_samples.size() ? m_hand_samples[ai] : HandSample{};
            std::snprintf(line, sizeof line, "%s%s: %s %.1f cm away, corrected %.0f%%, moved %.1f cm, shoulder turned %.0f deg, arm x%.2f, %.1f cm out of reach",
                          s.empty() ? "" : "; ", m_names[arm.hand].c_str(), h.anchor >= 0 ? m_names[h.anchor].c_str() : "no body bone", h.distance,
                          h.weight * 100.0, h.moved, h.turned, h.stretched, h.shortfall);
            s += line;
            if (h.elbow || h.fingers > 0)
            {
                std::snprintf(line, sizeof line, " (%s%s%d finger bones shaped)", h.elbow ? "elbow contact" : "", h.elbow ? ", " : "", h.fingers);
                s += line;
            }
        }
        for (size_t li = 0; li < m_legs.size() && li < m_foot_samples.size(); ++li)
        {
            const HandSample h = m_foot_samples[li];
            if (h.weight <= 0.0)
            {
                continue;
            }
            std::snprintf(line, sizeof line, "; %s: %s %.1f cm away, corrected %.0f%%, moved %.1f cm, %.1f cm out of reach", m_names[m_legs[li].foot].c_str(),
                          h.anchor >= 0 ? m_names[h.anchor].c_str() : "?", h.distance, h.weight * 100.0, h.moved, h.shortfall);
            s += line;
        }
        return s;
    }
} // namespace uuepbs
