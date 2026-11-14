#include "history.hpp"

#include <utility>

namespace uuepbs
{
    namespace
    {
        bool same_bone(const BoneEdit& a, const BoneEdit& b)
        {
            for (int i = 0; i < 3; ++i)
            {
                if (a.axis[i] != b.axis[i] || a.turn[i] != b.turn[i] || a.shift[i] != b.shift[i])
                {
                    return false;
                }
            }
            return a.spread == b.spread;
        }

        bool same_bones(const EditBook& a, const EditBook& b)
        {
            auto ia = a.begin();
            auto ib = b.begin();
            for (;;)
            {
                while (ia != a.end() && ia->second.edit.is_neutral())
                {
                    ++ia;
                }
                while (ib != b.end() && ib->second.edit.is_neutral())
                {
                    ++ib;
                }
                if (ia == a.end() || ib == b.end())
                {
                    return ia == a.end() && ib == b.end();
                }
                if (ia->first != ib->first || !same_bone(ia->second.edit, ib->second.edit))
                {
                    return false;
                }
                ++ia;
                ++ib;
            }
        }

        bool same_morphs(const MorphBook& a, const MorphBook& b)
        {
            if (a.size() != b.size())
            {
                return false;
            }
            for (auto ia = a.begin(), ib = b.begin(); ia != a.end(); ++ia, ++ib)
            {
                if (ia->first != ib->first || ia->second.weight != ib->second.weight)
                {
                    return false;
                }
            }
            return true;
        }

        void push_capped(std::deque<EditSnapshot>& stack, EditSnapshot s)
        {
            stack.push_back(std::move(s));
            while (stack.size() > EditHistory::kLimit)
            {
                stack.pop_front();
            }
        }
    } // namespace

    bool same_edits(const EditSnapshot& a, const EditSnapshot& b)
    {
        return same_bones(a.bones, b.bones) && same_morphs(a.morphs, b.morphs);
    }

    void EditHistory::commit(Track& t)
    {
        if (same_edits(t.pending, t.baseline))
        {
            return;
        }
        push_capped(t.undo, std::move(t.baseline));
        t.redo.clear();
        t.baseline = t.pending;
    }

    void EditHistory::observe(const std::string& actor, const EditSnapshot& now, bool busy, Clock::time_point time)
    {
        Track& t = m_tracks[actor];
        if (!t.started)
        {
            t.started = true;
            t.baseline = now;
            t.pending = now;
            t.pending_since = time;
            return;
        }
        if (!same_edits(now, t.pending))
        {
            t.pending = now;
            t.pending_since = time;
        }
        if (!busy && time - t.pending_since >= kSettle)
        {
            commit(t);
        }
    }

    bool EditHistory::can_undo(const std::string& actor) const
    {
        const auto it = m_tracks.find(actor);
        return it != m_tracks.end() && (!it->second.undo.empty() || !same_edits(it->second.pending, it->second.baseline));
    }

    bool EditHistory::can_redo(const std::string& actor) const
    {
        const auto it = m_tracks.find(actor);
        return it != m_tracks.end() && !it->second.redo.empty() && same_edits(it->second.pending, it->second.baseline);
    }

    size_t EditHistory::undo_steps(const std::string& actor) const
    {
        const auto it = m_tracks.find(actor);
        if (it == m_tracks.end())
        {
            return 0;
        }
        return it->second.undo.size() + (same_edits(it->second.pending, it->second.baseline) ? 0 : 1);
    }

    size_t EditHistory::redo_steps(const std::string& actor) const
    {
        return can_redo(actor) ? m_tracks.at(actor).redo.size() : 0;
    }

    std::optional<EditSnapshot> EditHistory::undo(const std::string& actor, const EditSnapshot& now)
    {
        Track& t = m_tracks[actor];
        if (!t.started)
        {
            observe(actor, now, false);
            return std::nullopt;
        }
        t.pending = now;
        commit(t); // an edit still settling becomes its own step
        if (t.undo.empty())
        {
            return std::nullopt;
        }
        push_capped(t.redo, std::move(t.baseline));
        t.baseline = std::move(t.undo.back());
        t.undo.pop_back();
        t.pending = t.baseline;
        return t.baseline;
    }

    std::optional<EditSnapshot> EditHistory::redo(const std::string& actor, const EditSnapshot& now)
    {
        Track& t = m_tracks[actor];
        if (!t.started)
        {
            observe(actor, now, false);
            return std::nullopt;
        }
        t.pending = now;
        if (!same_edits(t.pending, t.baseline))
        {
            commit(t); // a new edit since the undo: nothing left to redo
            return std::nullopt;
        }
        if (t.redo.empty())
        {
            return std::nullopt;
        }
        push_capped(t.undo, std::move(t.baseline));
        t.baseline = std::move(t.redo.back());
        t.redo.pop_back();
        t.pending = t.baseline;
        return t.baseline;
    }

    void EditHistory::forget(const std::string& actor)
    {
        m_tracks.erase(actor);
    }

    void EditHistory::retain(const std::set<std::string>& keep)
    {
        for (auto it = m_tracks.begin(); it != m_tracks.end();)
        {
            it = keep.count(it->first) ? std::next(it) : m_tracks.erase(it);
        }
    }
} // namespace uuepbs
