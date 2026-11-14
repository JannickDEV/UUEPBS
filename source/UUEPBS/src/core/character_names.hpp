// UUEPBS - names the user gives characters in the window ("<presets>/_characters/_names.txt")
//
// One line per character: "<key>\t<name>". The key is "player" for the player and the NPC's
// identity (the name it is remembered by across reloads, see character_memory.hpp) otherwise.
// Characters without a stable identity can still be renamed, but only for this session: their
// key is a memory address that changes on every load, so it is never written to the file.
// The file is plain text so it can be edited by hand; lines starting with '#' are ignored.
#pragma once

#include <filesystem>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>

namespace uuepbs
{
    class CharacterNames
    {
      public:
        static constexpr const char* kFileName = "_names.txt";
        static constexpr size_t kMaxBytes = 64; // longest name kept (UTF-8 bytes, cut on a character boundary)

        static CharacterNames& instance();

        // Loads the file (missing file = no names). Any thread.
        void set_file(std::filesystem::path file);
        const std::filesystem::path& file() const { return m_file; }

        // "" when the character has no name of the user's.
        std::string get(const std::string& key) const;
        // Empty `name` removes it. `persist` = write the file (stable keys only). Returns false when
        // the file could not be written (the name is still used for this session).
        bool set(const std::string& key, const std::string& name, bool persist);
        uint64_t revision() const;

        // Trims spaces, drops control characters (tabs and line breaks included), cuts to kMaxBytes.
        static std::string clean(const std::string& raw);
        static std::string serialize(const std::map<std::string, std::string>& names);
        static std::map<std::string, std::string> parse(const std::string& text);

        // Key a name is stored under: "player", the identity, or the actor key (session only).
        // `stable` tells whether it may be written to disk.
        static std::string key_for(const std::string& actor_key, const std::string& identity, bool* stable = nullptr);
        // The user's name for the character when there is one ("(player)" kept), else `label`.
        std::string shown(const std::string& label, const std::string& actor_key, const std::string& identity) const;

      private:
        bool save_locked() const;

        mutable std::mutex m_lock;
        std::filesystem::path m_file;
        std::map<std::string, std::string> m_saved;   // written to the file
        std::map<std::string, std::string> m_session; // unstable keys (this session only)
        uint64_t m_revision{1};
    };
} // namespace uuepbs
