#include "character_names.hpp"

#include "registry.hpp"


#include <fstream>
#include <sstream>
#include <system_error>

namespace fs = std::filesystem;

namespace uuepbs
{
    CharacterNames& CharacterNames::instance()
    {
        static CharacterNames names;
        return names;
    }

    std::string CharacterNames::clean(const std::string& raw)
    {
        std::string out;
        out.reserve(raw.size());
        for (const char ch : raw)
        {
            const auto c = static_cast<unsigned char>(ch);
            if (c < 0x20 || c == 0x7F)
            {
                continue;
            }
            out.push_back(ch);
        }
        const size_t first = out.find_first_not_of(' ');
        if (first == std::string::npos)
        {
            return {};
        }
        out = out.substr(first, out.find_last_not_of(' ') - first + 1);
        if (out.size() > kMaxBytes)
        {
            size_t cut = kMaxBytes;
            while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80)
            {
                --cut; // don't split a UTF-8 sequence
            }
            out.resize(cut);
            while (!out.empty() && out.back() == ' ')
            {
                out.pop_back();
            }
        }
        return out;
    }

    std::string CharacterNames::serialize(const std::map<std::string, std::string>& names)
    {
        std::string out = "# UUEPBS character names: <key><TAB><name>. Key = \"player\" or the character's identity.\n";
        for (const auto& [key, name] : names)
        {
            if (!key.empty() && !name.empty())
            {
                out += key + "\t" + name + "\n";
            }
        }
        return out;
    }

    std::map<std::string, std::string> CharacterNames::parse(const std::string& text)
    {
        std::map<std::string, std::string> out;
        std::istringstream in(text);
        std::string line;
        bool first = true;
        while (std::getline(in, line))
        {
            if (first && line.rfind("\xEF\xBB\xBF", 0) == 0)
            {
                line.erase(0, 3); // BOM from editors
            }
            first = false;
            if (!line.empty() && line.back() == '\r')
            {
                line.pop_back();
            }
            if (line.empty() || line[0] == '#')
            {
                continue;
            }
            const size_t tab = line.find('\t');
            if (tab == std::string::npos || tab == 0)
            {
                continue;
            }
            const std::string name = clean(line.substr(tab + 1));
            if (!name.empty())
            {
                out[line.substr(0, tab)] = name;
            }
        }
        return out;
    }

    std::string CharacterNames::key_for(const std::string& actor_key, const std::string& identity, bool* stable)
    {
        const bool player = actor_key == kPlayerActor;
        if (stable)
        {
            *stable = player || !identity.empty();
        }
        return player ? actor_key : !identity.empty() ? identity : actor_key;
    }

    std::string CharacterNames::shown(const std::string& label, const std::string& actor_key, const std::string& identity) const
    {
        const std::string own = get(key_for(actor_key, identity));
        if (own.empty())
        {
            return label;
        }
        static const std::string tag = " (player)";
        const bool player = label.size() >= tag.size() && label.compare(label.size() - tag.size(), tag.size(), tag) == 0;
        return player ? own + tag : own;
    }

    void CharacterNames::set_file(fs::path file)
    {
        std::map<std::string, std::string> loaded;
        {
            std::ifstream f(file, std::ios::binary);
            if (f)
            {
                std::ostringstream ss;
                ss << f.rdbuf();
                loaded = parse(ss.str());
            }
        }
        std::lock_guard guard(m_lock);
        m_file = std::move(file);
        m_saved = std::move(loaded);
        ++m_revision;
    }

    std::string CharacterNames::get(const std::string& key) const
    {
        std::lock_guard guard(m_lock);
        if (const auto it = m_session.find(key); it != m_session.end())
        {
            return it->second;
        }
        const auto it = m_saved.find(key);
        return it != m_saved.end() ? it->second : std::string();
    }

    uint64_t CharacterNames::revision() const
    {
        std::lock_guard guard(m_lock);
        return m_revision;
    }

    bool CharacterNames::set(const std::string& key, const std::string& raw, bool persist)
    {
        if (key.empty())
        {
            return false;
        }
        const std::string name = clean(raw);
        std::lock_guard guard(m_lock);
        ++m_revision;
        if (!persist)
        {
            if (name.empty())
            {
                m_session.erase(key);
            }
            else
            {
                m_session[key] = name;
            }
            return true;
        }
        m_session.erase(key);
        if (name.empty())
        {
            m_saved.erase(key);
        }
        else
        {
            m_saved[key] = name;
        }
        return save_locked();
    }

    bool CharacterNames::save_locked() const
    {
        if (m_file.empty())
        {
            return false;
        }
        std::error_code ec;
        fs::create_directories(m_file.parent_path(), ec);
        fs::path temp = m_file;
        temp += ".tmp";
        {
            std::ofstream f(temp, std::ios::binary | std::ios::trunc);
            const std::string body = serialize(m_saved);
            f.write(body.data(), static_cast<std::streamsize>(body.size()));
            if (!f)
            {
                return false;
            }
        }
        fs::rename(temp, m_file, ec);
        if (ec)
        {
            fs::remove(temp, ec);
            return false;
        }
        return true;
    }
} // namespace uuepbs
