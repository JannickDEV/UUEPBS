// UUEPBS - automatic portrait requests (DLL -> Lua, "native\portrait_requests.txt")
//
// The window asks for a portrait of a character that has none yet (or when you press
// "Retake portrait"). The Lua script reads the file, takes the picture in the game and saves
// it as "<presets>\_portraits\<stem>.auto.png", which the window then shows.
//
//   UUEPBS portraits 1
//   <character id>\t<file stem>\t<nonce>
//
// The id is what the picker uses ("auto" or the actor's address). A new nonce asks again
// for a character that already has a picture (Retake). Lua remembers what it has done.
#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>

namespace uuepbs
{
    class PortraitRequests
    {
      public:
        static constexpr const char* kHeader = "UUEPBS portraits 1";
        static constexpr size_t kMaxEntries = 256;

        // True when the request list changed (the file needs writing).
        bool add(const std::string& id, const std::string& stem, bool retake);
        std::string serialize() const;
        bool write(const std::filesystem::path& file) const;
        size_t size() const { return m_entries.size(); }

      private:
        struct Entry
        {
            std::string stem;
            uint32_t nonce{0};
        };
        std::map<std::string, Entry> m_entries; // id -> request
        uint32_t m_next_nonce{1};
    };
} // namespace uuepbs
