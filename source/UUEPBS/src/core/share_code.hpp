// UUEPBS - preset share codes: a whole preset as one line of text you can paste into chat.
//
//   UUEPBS1.<base64url of a compact preset JSON>.<CRC-32 of that JSON, 8 hex digits>
//
// The JSON inside is an ordinary preset ("format": "UUEPBS preset", the same "bones" and "morphs"
// sections as a preset file, plus an optional "name"), written without whitespace and without fields
// that are at their defaults, so a code is the same thing a preset file is and nothing new to learn.
// Decoding never trusts the text: size limits come first, every character is checked, the checksum
// must match, and the JSON goes through the regular preset reader (nesting limit, finite numbers,
// values clamped to the slider ranges) with caps on how many bones and morphs it may hold.
#pragma once

#include "morphs.hpp"
#include "sculpt.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace uuepbs
{
    namespace share
    {
        constexpr std::string_view kPrefix = "UUEPBS1.";
        constexpr size_t kMaxCodeChars = 256 * 1024; // pasted text, whitespace included
        constexpr size_t kMaxJsonBytes = 160 * 1024;
        constexpr size_t kMaxBones = 1024;
        constexpr size_t kMaxMorphs = 1024;
        constexpr size_t kMaxNameBytes = 200; // bone and morph names

        struct Decoded
        {
            EditBook bones;
            MorphBook morphs;
            std::string name; // suggested preset name ("" when the code has none), already file-name safe
            std::string summary; // "12 bone(s), 3 morph(s)"
        };

        uint32_t crc32(std::string_view data);
        std::string base64url_encode(std::string_view data);
        // false on any character outside A-Z a-z 0-9 - _ (trailing '=' padding is accepted) or a bad length.
        bool base64url_decode(std::string_view text, std::string& out);

        // Compact preset JSON (no whitespace, default fields left out). `name` may be empty.
        std::string compact_json(const EditBook& bones, const MorphBook* morphs, const std::string& name);
        std::string encode(const EditBook& bones, const MorphBook* morphs, const std::string& name = {});
        // Whitespace anywhere in `text` is ignored (codes wrapped by chat apps still work).
        // false + a message a player can act on when the text is not a usable code.
        bool decode(std::string_view text, Decoded& out, std::string& message);
        // Cheap check for "does the clipboard hold a share code at all".
        bool looks_like_code(std::string_view text);
    } // namespace share
} // namespace uuepbs
