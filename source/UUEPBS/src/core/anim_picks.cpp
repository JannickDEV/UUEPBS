#include "anim_picks.hpp"

#include "sculpt.hpp" // fold_case

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace uuepbs::anim_picks
{
    namespace
    {
        std::string clean(std::string_view s)
        {
            std::string out;
            for (char ch : s)
            {
                out.push_back(ch == '\t' || ch == '\r' || ch == '\n' ? ' ' : ch);
            }
            while (!out.empty() && out.back() == ' ')
            {
                out.pop_back();
            }
            size_t lead = 0;
            while (lead < out.size() && out[lead] == ' ')
            {
                ++lead;
            }
            return out.substr(lead);
        }

        // Glob with * only, both sides already folded.
        bool glob(std::string_view pat, std::string_view text)
        {
            size_t p = 0, t = 0, star = std::string_view::npos, mark = 0;
            while (t < text.size())
            {
                if (p < pat.size() && pat[p] == '*')
                {
                    star = p++;
                    mark = t;
                }
                else if (p < pat.size() && pat[p] == text[t])
                {
                    ++p;
                    ++t;
                }
                else if (star != std::string_view::npos)
                {
                    p = star + 1;
                    t = ++mark;
                }
                else
                {
                    return false;
                }
            }
            while (p < pat.size() && pat[p] == '*')
            {
                ++p;
            }
            return p == pat.size();
        }

        template <class F>
        void each_line(std::string_view text, F f)
        {
            size_t at = 0;
            while (at < text.size())
            {
                size_t end = text.find('\n', at);
                if (end == std::string_view::npos)
                {
                    end = text.size();
                }
                std::string_view line = text.substr(at, end - at);
                at = end + 1;
                while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
                {
                    line.remove_suffix(1);
                }
                if (!line.empty() && line.front() != '#')
                {
                    f(line);
                }
            }
        }
    } // namespace

    const char* mode_key(Mode m)
    {
        switch (m)
        {
        case Mode::only:
            return "only";
        case Mode::except:
            return "except";
        default:
            return "every";
        }
    }

    Mode mode_from(std::string_view key, Mode fallback)
    {
        const std::string k = fold_case(clean(key));
        if (k == "every" || k == "all")
        {
            return Mode::every;
        }
        if (k == "only")
        {
            return Mode::only;
        }
        if (k == "except" || k == "but")
        {
            return Mode::except;
        }
        return fallback;
    }

    bool covers(std::string_view entry, std::string_view playing)
    {
        const std::string e = fold_case(clean(entry));
        const std::string p = fold_case(clean(playing));
        if (e.empty())
        {
            return false;
        }
        if (e == fold_case(kNormal))
        {
            return p.empty();
        }
        if (p.empty())
        {
            return false;
        }
        if (e.find('*') != std::string::npos)
        {
            return glob(e, p);
        }
        return e == p;
    }

    bool wanted(const Choice& c, std::string_view playing)
    {
        if (c.mode == Mode::every)
        {
            return true;
        }
        const bool hit = std::any_of(c.names.begin(), c.names.end(), [&](const std::string& n) { return covers(n, playing); });
        return c.mode == Mode::only ? hit : !hit;
    }

    bool ticked(const Choice& c, std::string_view name)
    {
        const std::string f = fold_case(clean(name));
        return std::any_of(c.names.begin(), c.names.end(), [&](const std::string& n) { return fold_case(n) == f; });
    }

    void tick(Choice& c, const std::string& name, bool on)
    {
        const std::string n = clean(name);
        if (n.empty())
        {
            return;
        }
        const std::string f = fold_case(n);
        std::erase_if(c.names, [&](const std::string& x) { return fold_case(x) == f; });
        if (on)
        {
            c.names.push_back(n);
        }
    }

    std::string write(const Choice& c)
    {
        std::string out = "# UUEPBS Hand Contact animations, picked in the window (Character Scale > Animation).\n"
                          "# mode = every | only | except; one anim= line per ticked animation (* matches any characters).\n";
        out += "mode=";
        out += mode_key(c.mode);
        out += "\n";
        for (const auto& n : c.names)
        {
            out += "anim=" + clean(n) + "\n";
        }
        return out;
    }

    bool read(std::string_view text, Choice& c)
    {
        Choice out;
        bool any = false;
        each_line(text, [&](std::string_view line) {
            const size_t eq = line.find('=');
            if (eq == std::string_view::npos)
            {
                return;
            }
            const std::string key = fold_case(clean(line.substr(0, eq)));
            const std::string_view val = line.substr(eq + 1);
            if (key == "mode")
            {
                out.mode = mode_from(val, Mode::every);
                any = true;
            }
            else if (key == "anim")
            {
                tick(out, std::string(val), true);
            }
        });
        if (any)
        {
            c = std::move(out);
        }
        return any;
    }

    bool History::record(const std::string& name, const std::string& who)
    {
        const std::string n = clean(name);
        if (n.empty() || n == "None")
        {
            return false;
        }
        const std::string f = fold_case(n);
        for (auto& s : m_seen)
        {
            if (fold_case(s.name) == f)
            {
                ++s.count;
                s.order = m_next++;
                if (!who.empty())
                {
                    s.who = clean(who);
                }
                return false;
            }
        }
        m_seen.push_back({n, clean(who), 1, m_next++});
        if (m_seen.size() > kHistoryLimit)
        {
            const auto oldest = std::min_element(m_seen.begin(), m_seen.end(), [](const Seen& a, const Seen& b) { return a.order < b.order; });
            m_seen.erase(oldest);
        }
        return true;
    }

    std::vector<Seen> History::entries() const
    {
        std::vector<Seen> out = m_seen;
        std::sort(out.begin(), out.end(), [](const Seen& a, const Seen& b) { return a.order > b.order; });
        return out;
    }

    std::string History::write() const
    {
        std::string out = "# UUEPBS: animations the characters played in this game (montages and single animations), newest first.\n"
                          "# name<TAB>character<TAB>times. Listed in Character Scale > Animation; safe to delete.\n";
        for (const auto& s : entries())
        {
            out += s.name + "\t" + s.who + "\t" + std::to_string(s.count) + "\n";
        }
        return out;
    }

    void History::read(std::string_view text)
    {
        m_seen.clear();
        std::vector<Seen> rows;
        each_line(text, [&](std::string_view line) {
            Seen s;
            const size_t a = line.find('\t');
            s.name = clean(line.substr(0, a));
            if (a != std::string_view::npos)
            {
                const std::string_view rest = line.substr(a + 1);
                const size_t b = rest.find('\t');
                s.who = clean(rest.substr(0, b));
                if (b != std::string_view::npos)
                {
                    s.count = static_cast<uint32_t>(std::strtoul(std::string(rest.substr(b + 1)).c_str(), nullptr, 10));
                }
            }
            if (!s.name.empty() && rows.size() < kHistoryLimit &&
                std::none_of(rows.begin(), rows.end(), [&](const Seen& r) { return fold_case(r.name) == fold_case(s.name); }))
            {
                s.count = std::max<uint32_t>(s.count, 1);
                rows.push_back(std::move(s));
            }
        });
        // the file is newest first: give the first line the largest order
        m_next = 1;
        for (auto it = rows.rbegin(); it != rows.rend(); ++it)
        {
            it->order = m_next++;
            m_seen.push_back(std::move(*it));
        }
    }
} // namespace uuepbs::anim_picks
