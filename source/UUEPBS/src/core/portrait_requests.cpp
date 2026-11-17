#include "portrait_requests.hpp"

#include <fstream>
#include <system_error>

namespace uuepbs
{
    namespace
    {
        bool usable(const std::string& s)
        {
            if (s.empty() || s.size() > 200)
            {
                return false;
            }
            for (const char c : s)
            {
                if (static_cast<unsigned char>(c) < 32) // tabs and line breaks would break the format
                {
                    return false;
                }
            }
            return true;
        }
    } // namespace

    bool PortraitRequests::add(const std::string& id, const std::string& stem, bool retake)
    {
        if (!usable(id) || !usable(stem))
        {
            return false;
        }
        const auto it = m_entries.find(id);
        if (it != m_entries.end() && it->second.stem == stem && !retake)
        {
            return false; // already asked
        }
        if (it == m_entries.end() && m_entries.size() >= kMaxEntries)
        {
            m_entries.erase(m_entries.begin());
        }
        m_entries[id] = Entry{stem, m_next_nonce++};
        return true;
    }

    std::string PortraitRequests::serialize() const
    {
        std::string out = std::string(kHeader) + "\n";
        for (const auto& [id, e] : m_entries)
        {
            out += id + "\t" + e.stem + "\t" + std::to_string(e.nonce) + "\n";
        }
        return out;
    }

    bool PortraitRequests::write(const std::filesystem::path& file) const
    {
        std::filesystem::path temp = file;
        temp += ".tmp";
        {
            std::ofstream f(temp, std::ios::binary | std::ios::trunc);
            const std::string body = serialize();
            f.write(body.data(), static_cast<std::streamsize>(body.size()));
            if (!f)
            {
                return false;
            }
        }
        std::error_code ec;
        std::filesystem::rename(temp, file, ec);
        if (ec)
        {
            std::filesystem::remove(temp, ec);
            return false;
        }
        return true;
    }
} // namespace uuepbs
