#include "preset_library.hpp"

#include "json.hpp"
#include "presets.hpp"
#include "sculpt.hpp" // fold_case

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>
#include <system_error>

namespace fs = std::filesystem;

namespace uuepbs
{
    namespace
    {
        using json::JsonReader;
        using json::JsonValue;
        using Entries = std::map<std::string, std::pair<std::string, std::vector<std::string>>>;

        std::string quote(const std::string& s)
        {
            std::string out = "\"";
            for (const char c : s)
            {
                if (c == '"' || c == '\\')
                {
                    out.push_back('\\');
                    out.push_back(c);
                }
                else if (static_cast<unsigned char>(c) < 0x20)
                {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(c));
                    out += buf;
                }
                else
                {
                    out.push_back(c);
                }
            }
            return out + "\"";
        }

        bool is_space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

        std::vector<std::string> words(const std::string& text)
        {
            std::vector<std::string> out;
            std::string cur;
            for (const char c : text)
            {
                if (is_space(c))
                {
                    if (!cur.empty())
                    {
                        out.push_back(cur);
                        cur.clear();
                    }
                }
                else
                {
                    cur.push_back(c);
                }
            }
            if (!cur.empty())
            {
                out.push_back(cur);
            }
            return out;
        }

        bool usable_line_field(const std::string& s)
        {
            if (s.empty() || s.size() > 200)
            {
                return false;
            }
            for (const char c : s)
            {
                if (static_cast<unsigned char>(c) < 32)
                {
                    return false;
                }
            }
            return true;
        }

        bool write_atomically(const fs::path& file, const std::string& body)
        {
            std::error_code ec;
            fs::create_directories(file.parent_path(), ec);
            fs::path temp = file;
            temp += ".tmp";
            {
                std::ofstream f(temp, std::ios::binary | std::ios::trunc);
                f.write(body.data(), static_cast<std::streamsize>(body.size()));
                if (!f)
                {
                    return false;
                }
            }
            fs::rename(temp, file, ec);
            if (ec)
            {
                fs::remove(temp, ec);
                return false;
            }
            return true;
        }
    } // namespace

    // ---- tags ----------------------------------------------------------------------------

    std::string PresetLibrary::clean_tag(const std::string& raw)
    {
        std::string out;
        for (const char c : raw)
        {
            if (static_cast<unsigned char>(c) < 32 || c == '#' || c == ',' || c == '"' || c == '\\')
            {
                if (c == '\t' && !out.empty() && out.back() != ' ')
                {
                    out.push_back(' ');
                }
                continue;
            }
            if (c == ' ' && (out.empty() || out.back() == ' '))
            {
                continue;
            }
            out.push_back(c);
        }
        while (!out.empty() && out.back() == ' ')
        {
            out.pop_back();
        }
        if (out.size() > kMaxTagLength)
        {
            out.resize(kMaxTagLength);
            // Never cut a UTF-8 character in half.
            while (!out.empty() && (static_cast<unsigned char>(out.back()) & 0xC0) == 0x80)
            {
                out.pop_back();
            }
            if (!out.empty() && (static_cast<unsigned char>(out.back()) & 0xC0) == 0xC0)
            {
                out.pop_back();
            }
            while (!out.empty() && out.back() == ' ')
            {
                out.pop_back();
            }
        }
        return out;
    }

    std::vector<std::string> PresetLibrary::split_tags(const std::string& text)
    {
        std::vector<std::string> out;
        std::set<std::string> seen;
        std::string cur;
        auto flush = [&] {
            const std::string t = clean_tag(cur);
            cur.clear();
            if (!t.empty() && out.size() < kMaxTags && seen.insert(fold_case(t)).second)
            {
                out.push_back(t);
            }
        };
        for (const char c : text)
        {
            if (c == ',' || c == ';' || c == '\n')
            {
                flush();
            }
            else
            {
                cur.push_back(c);
            }
        }
        flush();
        return out;
    }

    bool PresetLibrary::matches(const std::string& name, const std::vector<std::string>& tags, const std::string& query,
                                const std::set<std::string>& required)
    {
        std::vector<std::string> folded_tags;
        folded_tags.reserve(tags.size());
        for (const std::string& t : tags)
        {
            folded_tags.push_back(fold_case(t));
        }
        for (const std::string& r : required)
        {
            if (std::find(folded_tags.begin(), folded_tags.end(), r) == folded_tags.end())
            {
                return false;
            }
        }
        const std::string folded_name = fold_case(name);
        for (const std::string& raw : words(query))
        {
            std::string w = fold_case(raw);
            const bool tag_only = w[0] == '#';
            if (tag_only)
            {
                w.erase(0, 1);
                if (w.empty())
                {
                    continue; // a lone "#" while typing
                }
            }
            bool hit = !tag_only && folded_name.find(w) != std::string::npos;
            for (size_t i = 0; !hit && i < folded_tags.size(); ++i)
            {
                hit = tag_only ? folded_tags[i].rfind(w, 0) == 0 : folded_tags[i].find(w) != std::string::npos;
            }
            if (!hit)
            {
                return false;
            }
        }
        return true;
    }

    std::vector<std::string> PresetLibrary::filter(const std::vector<std::string>& presets, const std::string& query,
                                                   const std::set<std::string>& required) const
    {
        std::vector<std::string> out;
        for (const std::string& p : presets)
        {
            if (matches(p, tags(p), query, required))
            {
                out.push_back(p);
            }
        }
        return out;
    }

    // ---- file ----------------------------------------------------------------------------

    std::string PresetLibrary::serialize(const Entries& entries)
    {
        std::string out = "{\n  \"format\": \"UUEPBS preset library\",\n  \"version\": 1,\n  \"presets\": {";
        bool first = true;
        for (const auto& [key, e] : entries)
        {
            if (e.second.empty())
            {
                continue; // nothing to remember
            }
            out += first ? "\n" : ",\n";
            first = false;
            out += "    " + quote(e.first) + ": { \"tags\": [";
            for (size_t i = 0; i < e.second.size(); ++i)
            {
                out += (i ? ", " : "") + quote(e.second[i]);
            }
            out += "] }";
        }
        out += first ? "}\n}\n" : "\n  }\n}\n";
        return out;
    }

    bool PresetLibrary::parse(const std::string& text, Entries& out)
    {
        out.clear();
        std::string_view body = text;
        if (body.size() >= 3 && body.substr(0, 3) == "\xEF\xBB\xBF")
        {
            body.remove_prefix(3);
        }
        JsonValue root;
        std::string error;
        if (!JsonReader(std::string(body)).read(root, error) || root.kind != JsonValue::Kind::Object)
        {
            return false;
        }
        const JsonValue* presets = root.find("presets");
        if (!presets || presets->kind != JsonValue::Kind::Object)
        {
            return false;
        }
        for (const auto& [name, v] : presets->members)
        {
            if (name.empty() || v.kind != JsonValue::Kind::Object)
            {
                continue;
            }
            std::vector<std::string> tags;
            if (const JsonValue* t = v.find("tags"))
            {
                std::string joined;
                if (t->kind == JsonValue::Kind::Array)
                {
                    for (const JsonValue& item : t->items)
                    {
                        if (item.kind == JsonValue::Kind::String)
                        {
                            joined += clean_tag(item.text) + ",";
                        }
                    }
                }
                else if (t->kind == JsonValue::Kind::String) // "tags": "curvy, roku" typed by hand
                {
                    joined = t->text;
                }
                tags = split_tags(joined);
            }
            out[fold_case(name)] = {name, std::move(tags)};
        }
        return true;
    }

    void PresetLibrary::set_presets_folder(const fs::path& presets_folder)
    {
        m_folder = presets_folder.empty() ? fs::path() : presets_folder / kFolderName;
        m_entries.clear();
        m_loaded = false;
        m_stamp = {};
        reload_if_changed();
    }

    void PresetLibrary::reload_if_changed()
    {
        if (m_folder.empty())
        {
            return;
        }
        const fs::path file = m_folder / kFileName;
        std::error_code ec;
        const auto stamp = fs::last_write_time(file, ec);
        if (ec)
        {
            if (m_loaded)
            {
                m_entries.clear(); // deleted by hand
            }
            m_loaded = false;
            return;
        }
        if (m_loaded && stamp == m_stamp)
        {
            return;
        }
        std::ifstream f(file, std::ios::binary);
        std::ostringstream ss;
        ss << f.rdbuf();
        Entries entries;
        if (parse(ss.str(), entries))
        {
            m_entries = std::move(entries);
        }
        // A broken file (half-edited by hand) keeps what we had; it is not overwritten until a tag changes.
        m_stamp = stamp;
        m_loaded = true;
    }

    bool PresetLibrary::write() const
    {
        if (m_folder.empty())
        {
            return false;
        }
        return write_atomically(m_folder / kFileName, serialize(m_entries));
    }

    std::vector<std::string> PresetLibrary::tags(const std::string& preset) const
    {
        const auto it = m_entries.find(fold_case(preset));
        return it == m_entries.end() ? std::vector<std::string>() : it->second.second;
    }

    bool PresetLibrary::set_tags(const std::string& preset, const std::vector<std::string>& tags)
    {
        if (preset.empty() || m_folder.empty())
        {
            return false;
        }
        std::string joined;
        for (const std::string& t : tags)
        {
            joined += clean_tag(t) + ",";
        }
        auto& e = m_entries[fold_case(preset)];
        e.first = preset;
        e.second = split_tags(joined);
        const bool ok = write();
        std::error_code ec;
        m_stamp = fs::last_write_time(m_folder / kFileName, ec);
        m_loaded = !ec;
        return ok;
    }

    bool PresetLibrary::add_tag(const std::string& preset, const std::string& tag)
    {
        std::vector<std::string> t = tags(preset);
        for (const std::string& extra : split_tags(tag))
        {
            t.push_back(extra);
        }
        return set_tags(preset, t);
    }

    bool PresetLibrary::remove_tag(const std::string& preset, const std::string& tag)
    {
        std::vector<std::string> t = tags(preset);
        const std::string folded = fold_case(tag);
        std::erase_if(t, [&](const std::string& x) { return fold_case(x) == folded; });
        return set_tags(preset, t);
    }

    void PresetLibrary::forget(const std::string& preset)
    {
        if (m_folder.empty() || preset.empty())
        {
            return;
        }
        if (m_entries.erase(fold_case(preset)) > 0)
        {
            write();
            std::error_code ec;
            m_stamp = fs::last_write_time(m_folder / kFileName, ec);
        }
        std::error_code ec;
        for (const char* ext : {".auto.png", ".png", ".jpg", ".jpeg"})
        {
            fs::remove(m_folder / path_from_utf8(preset + ext), ec);
        }
    }

    std::vector<std::pair<std::string, size_t>> PresetLibrary::tag_counts(const std::vector<std::string>& presets) const
    {
        std::map<std::string, std::pair<std::string, size_t>> counts; // folded -> (first spelling, count)
        for (const std::string& p : presets)
        {
            for (const std::string& t : tags(p))
            {
                auto& c = counts[fold_case(t)];
                if (c.first.empty())
                {
                    c.first = t;
                }
                ++c.second;
            }
        }
        std::vector<std::pair<std::string, size_t>> out;
        for (const auto& [key, c] : counts)
        {
            out.push_back(c);
        }
        std::stable_sort(out.begin(), out.end(), [](const auto& a, const auto& b) {
            if (a.second != b.second)
            {
                return a.second > b.second;
            }
            return fold_case(a.first) < fold_case(b.first);
        });
        return out;
    }

    fs::path PresetLibrary::auto_thumbnail(const std::string& preset) const
    {
        return m_folder.empty() ? fs::path() : m_folder / path_from_utf8(preset + ".auto.png");
    }

    // ---- thumbnail requests --------------------------------------------------------------

    bool ThumbnailRequests::add(const std::string& character_id, const std::string& preset)
    {
        if (!usable_line_field(character_id) || !usable_line_field(preset))
        {
            return false;
        }
        if (m_next_nonce == 1)
        {
            // Not 1, 2, 3... after every start: the script remembers lines it has already done.
            m_next_nonce = static_cast<uint32_t>(std::time(nullptr) & 0x3FFFFFFF) + 1;
        }
        const std::string folded = fold_case(preset);
        std::erase_if(m_order, [&](const Entry& e) { return fold_case(e.preset) == folded; });
        if (m_order.size() >= kMaxEntries)
        {
            m_order.erase(m_order.begin());
        }
        m_order.push_back(Entry{preset, character_id, m_next_nonce++});
        return true;
    }

    std::string ThumbnailRequests::serialize() const
    {
        std::string out = std::string(kHeader) + "\n";
        for (const Entry& e : m_order)
        {
            out += e.id + "\t" + e.preset + "\t" + std::to_string(e.nonce) + "\n";
        }
        return out;
    }

    bool ThumbnailRequests::write(const fs::path& file) const
    {
        return write_atomically(file, serialize());
    }
} // namespace uuepbs
