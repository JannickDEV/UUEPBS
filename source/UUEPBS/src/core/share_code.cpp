#include "share_code.hpp"

#include "json.hpp"
#include "presets.hpp"
#include "looks.hpp"
#include "proportions.hpp"

#include <array>
#include <cmath>
#include <cstdio>

namespace uuepbs::share
{
    namespace
    {
        constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

        int sextet(char c)
        {
            if (c >= 'A' && c <= 'Z')
            {
                return c - 'A';
            }
            if (c >= 'a' && c <= 'z')
            {
                return c - 'a' + 26;
            }
            if (c >= '0' && c <= '9')
            {
                return c - '0' + 52;
            }
            if (c == '-')
            {
                return 62;
            }
            if (c == '_')
            {
                return 63;
            }
            return -1;
        }

        bool is_space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

        std::string escape(const std::string& s)
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
            out.push_back('"');
            return out;
        }

        // Same rounding as preset files: 4 decimals, trailing zeros dropped.
        std::string number(double v)
        {
            if (!std::isfinite(v))
            {
                v = 0.0;
            }
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.4f", v);
            std::string s = buf;
            while (!s.empty() && s.back() == '0')
            {
                s.pop_back();
            }
            if (!s.empty() && s.back() == '.')
            {
                s.pop_back();
            }
            return s == "-0" ? "0" : s;
        }

        std::string triple(const double v[3])
        {
            return "[" + number(v[0]) + "," + number(v[1]) + "," + number(v[2]) + "]";
        }

        bool name_ok(const std::string& name)
        {
            if (name.empty() || name.size() > kMaxNameBytes)
            {
                return false;
            }
            for (const char c : name)
            {
                if (static_cast<unsigned char>(c) < 0x20)
                {
                    return false;
                }
            }
            return true;
        }
    } // namespace

    uint32_t crc32(std::string_view data)
    {
        static const std::array<uint32_t, 256> table = [] {
            std::array<uint32_t, 256> t{};
            for (uint32_t i = 0; i < 256; ++i)
            {
                uint32_t c = i;
                for (int k = 0; k < 8; ++k)
                {
                    c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
                }
                t[i] = c;
            }
            return t;
        }();
        uint32_t crc = 0xFFFFFFFFu;
        for (const char ch : data)
        {
            crc = table[(crc ^ static_cast<unsigned char>(ch)) & 0xFF] ^ (crc >> 8);
        }
        return crc ^ 0xFFFFFFFFu;
    }

    std::string base64url_encode(std::string_view data)
    {
        std::string out;
        out.reserve((data.size() + 2) / 3 * 4);
        size_t i = 0;
        for (; i + 3 <= data.size(); i += 3)
        {
            const uint32_t n = (static_cast<unsigned char>(data[i]) << 16) | (static_cast<unsigned char>(data[i + 1]) << 8) |
                               static_cast<unsigned char>(data[i + 2]);
            out.push_back(kAlphabet[(n >> 18) & 63]);
            out.push_back(kAlphabet[(n >> 12) & 63]);
            out.push_back(kAlphabet[(n >> 6) & 63]);
            out.push_back(kAlphabet[n & 63]);
        }
        const size_t rest = data.size() - i;
        if (rest == 1)
        {
            const uint32_t n = static_cast<unsigned char>(data[i]) << 16;
            out.push_back(kAlphabet[(n >> 18) & 63]);
            out.push_back(kAlphabet[(n >> 12) & 63]);
        }
        else if (rest == 2)
        {
            const uint32_t n = (static_cast<unsigned char>(data[i]) << 16) | (static_cast<unsigned char>(data[i + 1]) << 8);
            out.push_back(kAlphabet[(n >> 18) & 63]);
            out.push_back(kAlphabet[(n >> 12) & 63]);
            out.push_back(kAlphabet[(n >> 6) & 63]);
        }
        return out;
    }

    bool base64url_decode(std::string_view text, std::string& out)
    {
        out.clear();
        while (!text.empty() && text.back() == '=')
        {
            text.remove_suffix(1);
        }
        if (text.size() % 4 == 1)
        {
            return false;
        }
        out.reserve(text.size() / 4 * 3 + 2);
        uint32_t acc = 0;
        int bits = 0;
        for (const char c : text)
        {
            const int v = sextet(c);
            if (v < 0)
            {
                return false;
            }
            acc = (acc << 6) | static_cast<uint32_t>(v);
            bits += 6;
            if (bits >= 8)
            {
                bits -= 8;
                out.push_back(static_cast<char>((acc >> bits) & 0xFF));
            }
        }
        // Leftover bits must be zero, so every byte string has exactly one code.
        return (acc & ((1u << bits) - 1)) == 0;
    }

    std::string compact_json(const EditBook& bones, const MorphBook* morphs, const std::string& name)
    {
        // Looks ("look:" entries) get their own object, like in preset files; the rest are morph targets.
        const size_t look_count = morphs ? looks::count(*morphs) : 0;
        const bool with_morphs = morphs && morphs->size() > look_count;
        std::string out = "{\"format\":\"UUEPBS preset\",\"version\":";
        out += with_morphs ? "3" : "2";
        if (!name.empty())
        {
            out += ",\"name\":" + escape(name);
        }
        out += ",\"bones\":{";
        bool first = true;
        for (const auto& [key, entry] : bones)
        {
            BoneEdit e = entry.edit;
            e.clamp();
            if (e.is_neutral() || is_scale_key(key))
            {
                continue; // Character Scale values go under "proportions" below
            }
            out += first ? "" : ",";
            first = false;
            out += escape(entry.bone) + ":{";
            // Only what differs from a fresh bone (1, 1, 1 / no turn / no move / scale_too).
            std::string fields;
            auto add = [&](const std::string& f) { fields += (fields.empty() ? "" : ",") + f; };
            if (e.has_scale())
            {
                if (e.axis[0] == e.axis[1] && e.axis[1] == e.axis[2])
                {
                    add("\"scale\":" + number(e.axis[0]));
                }
                else
                {
                    add("\"scale\":" + triple(e.axis));
                }
            }
            if (e.has_turn())
            {
                add("\"rotate\":" + triple(e.turn));
            }
            if (e.has_shift())
            {
                add("\"move\":" + triple(e.shift));
            }
            if (e.spread == Spread::Keep)
            {
                add("\"children\":\"keep_size\"");
            }
            else if (e.spread == Spread::Solo)
            {
                add("\"children\":\"this_bone_only\"");
            }
            out += fields + "}";
        }
        out += "}";
        if (has_proportions(bones))
        {
            out += ",\"proportions\":{";
            bool first_scale = true;
            for (const ScaleSlider& sl : scale_sliders())
            {
                const auto it = bones.find(scale_key(sl.id));
                if (it == bones.end() || std::fabs(it->second.edit.axis[0] - 1.0) < kScaleNeutralEps)
                {
                    continue;
                }
                out += first_scale ? "" : ",";
                first_scale = false;
                out += escape(sl.id) + ":" + number(it->second.edit.axis[0]);
            }
            out += "}";
        }
        if (with_morphs)
        {
            out += ",\"morphs\":{";
            bool first_morph = true;
            for (const auto& [key, m] : *morphs)
            {
                if (looks::is_look(key))
                {
                    continue;
                }
                out += first_morph ? "" : ",";
                first_morph = false;
                out += escape(m.name) + ":" + number(clamp_morph(m.weight));
            }
            out += "}";
        }
        if (look_count > 0)
        {
            out += ",\"looks\":{";
            bool first_look = true;
            for (const auto& [key, m] : *morphs)
            {
                if (!looks::is_look(key))
                {
                    continue;
                }
                out += first_look ? "" : ",";
                first_look = false;
                out += escape(looks::id_of(key)) + ":" + number(clamp_morph(m.weight));
            }
            out += "}";
        }
        out += "}";
        return out;
    }

    std::string encode(const EditBook& bones, const MorphBook* morphs, const std::string& name)
    {
        const std::string body = compact_json(bones, morphs, PresetShelf::clean_name(name));
        char sum[16];
        std::snprintf(sum, sizeof(sum), "%08x", static_cast<unsigned>(crc32(body)));
        return std::string(kPrefix) + base64url_encode(body) + "." + sum;
    }

    bool looks_like_code(std::string_view text)
    {
        while (!text.empty() && is_space(text.front()))
        {
            text.remove_prefix(1);
        }
        return text.size() > kPrefix.size() && text.substr(0, kPrefix.size()) == kPrefix;
    }

    bool decode(std::string_view text, Decoded& out, std::string& message)
    {
        out = Decoded{};
        if (text.size() > kMaxCodeChars)
        {
            message = "That text is too long to be a preset code.";
            return false;
        }
        std::string code;
        code.reserve(text.size());
        for (const char c : text)
        {
            if (!is_space(c))
            {
                code.push_back(c);
            }
        }
        if (code.empty())
        {
            message = "Nothing to import: copy a preset code (it starts with UUEPBS1.) first.";
            return false;
        }
        if (code.size() <= kPrefix.size() || std::string_view(code).substr(0, kPrefix.size()) != kPrefix)
        {
            message = code.rfind("UUEPBS", 0) == 0 ? "This code is from a newer UUEPBS version (update the mod to import it)."
                                                    : "That is not a UUEPBS preset code (it should start with UUEPBS1.).";
            return false;
        }
        const std::string_view rest = std::string_view(code).substr(kPrefix.size());
        const size_t dot = rest.rfind('.');
        if (dot == std::string_view::npos || rest.size() - dot - 1 != 8)
        {
            message = "The code is cut off: copy all of it, including the last 8 characters after the final dot.";
            return false;
        }
        const std::string_view payload = rest.substr(0, dot);
        const std::string_view sum_text = rest.substr(dot + 1);
        uint32_t sum = 0;
        for (const char c : sum_text)
        {
            int d = -1;
            if (c >= '0' && c <= '9')
            {
                d = c - '0';
            }
            else if (c >= 'a' && c <= 'f')
            {
                d = c - 'a' + 10;
            }
            else if (c >= 'A' && c <= 'F')
            {
                d = c - 'A' + 10;
            }
            if (d < 0)
            {
                message = "The code is damaged (bad checksum characters).";
                return false;
            }
            sum = (sum << 4) | static_cast<uint32_t>(d);
        }
        if (payload.size() / 4 * 3 > kMaxJsonBytes)
        {
            message = "That code is too large to import.";
            return false;
        }
        std::string body;
        if (!base64url_decode(payload, body))
        {
            message = "The code is damaged (characters were changed or lost while copying).";
            return false;
        }
        if (crc32(body) != sum)
        {
            message = "The code is incomplete or was changed (checksum does not match). Copy it again.";
            return false;
        }
        if (body.empty() || body.front() != '{')
        {
            message = "The code does not hold a preset.";
            return false;
        }

        json::JsonValue root;
        std::string error;
        if (!json::JsonReader(body).read(root, error) || root.kind != json::JsonValue::Kind::Object)
        {
            message = "The code does not hold a readable preset.";
            return false;
        }
        const json::JsonValue* format = root.find("format");
        if (!format || format->kind != json::JsonValue::Kind::String || format->text != "UUEPBS preset")
        {
            message = "The code does not hold a UUEPBS preset.";
            return false;
        }
        for (const char* section : {"bones", "morphs"})
        {
            const json::JsonValue* s = root.find(section);
            const size_t cap = section[0] == 'b' ? kMaxBones : kMaxMorphs;
            if (s && s->kind == json::JsonValue::Kind::Object)
            {
                if (s->members.size() > cap)
                {
                    message = std::string("The code has too many ") + section + " to be a real preset.";
                    return false;
                }
                for (const auto& [name, v] : s->members)
                {
                    (void)v;
                    if (!name_ok(name))
                    {
                        message = std::string("The code has a broken ") + (section[0] == 'b' ? "bone" : "morph") + " name.";
                        return false;
                    }
                }
            }
        }
        if (const json::JsonValue* n = root.find("name"); n && n->kind == json::JsonValue::Kind::String)
        {
            out.name = PresetShelf::clean_name(n->text);
        }

        std::string summary;
        if (!PresetShelf::parse(body, out.bones, summary, &out.morphs))
        {
            message = "The code does not hold a usable preset: " + summary;
            return false;
        }
        if (out.bones.empty() && out.morphs.empty())
        {
            message = "The code holds an empty preset (nothing to apply).";
            return false;
        }
        out.summary = summary;
        message = "Preset code read: " + summary;
        return true;
    }
} // namespace uuepbs::share
