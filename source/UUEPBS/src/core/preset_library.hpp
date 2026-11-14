// UUEPBS - preset thumbnails, tags and the browser's search ("<presets>/_library/")
//
// Everything here lives next to the preset files, never inside them, so presets saved by older
// versions (and presets you share) load exactly as before:
//
//   UUEPBS Presets/
//     Curvy.json                    the preset itself (unchanged format)
//     _library/presets.json         tags per preset
//     _library/Curvy.auto.png       thumbnail taken in the game when the preset was saved
//     _library/Curvy.png            your own picture (PNG/JPG) always wins over the .auto one
//
//   _library/presets.json:
//   {
//     "format": "UUEPBS preset library",
//     "version": 1,
//     "presets": { "Curvy": { "tags": ["curvy", "roku"] } }
//   }
//
// The folder name starts with "_", so PresetShelf::list() never mistakes it for a preset.
#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace uuepbs
{
    class PresetLibrary
    {
      public:
        static constexpr const char* kFolderName = "_library";
        static constexpr const char* kFileName = "presets.json";
        static constexpr size_t kMaxTags = 12;      // per preset
        static constexpr size_t kMaxTagLength = 24; // characters (bytes)

        // `presets_folder` is the presets folder; the library lives in its "_library" sub-folder.
        void set_presets_folder(const std::filesystem::path& presets_folder);
        const std::filesystem::path& folder() const { return m_folder; }
        bool ready() const { return !m_folder.empty(); }

        // Re-reads presets.json when it changed on disk (cheap when it did not).
        void reload_if_changed();

        std::vector<std::string> tags(const std::string& preset) const;
        // Replaces the tags (cleaned, de-duplicated, at most kMaxTags) and writes the file.
        bool set_tags(const std::string& preset, const std::vector<std::string>& tags);
        bool add_tag(const std::string& preset, const std::string& tag);
        bool remove_tag(const std::string& preset, const std::string& tag);
        // A deleted preset: drops its tags and its thumbnails (hand-made and automatic).
        void forget(const std::string& preset);

        // Every tag in use by one of `presets` (others are stale entries), with how many presets use it,
        // most used first, then by name.
        std::vector<std::pair<std::string, size_t>> tag_counts(const std::vector<std::string>& presets) const;

        // Where the automatic thumbnail of `preset` is written ("<library>/<preset>.auto.png").
        std::filesystem::path auto_thumbnail(const std::string& preset) const;

        // "curvy, Roku ,  tall legs" -> {"curvy", "Roku", "tall legs"}. Case is kept, duplicates (ignoring case) dropped.
        static std::vector<std::string> split_tags(const std::string& text);
        // Trims, folds runs of spaces, drops control characters, '#' and ','; "" = unusable.
        static std::string clean_tag(const std::string& raw);

        // The browser's search. Every word of `query` must match the preset name or one of its tags
        // (case-insensitive, part of a word is enough); "#word" only matches a tag that starts with it.
        // Every tag in `required` (folded) must be on the preset.
        static bool matches(const std::string& name, const std::vector<std::string>& tags, const std::string& query,
                            const std::set<std::string>& required);
        std::vector<std::string> filter(const std::vector<std::string>& presets, const std::string& query, const std::set<std::string>& required) const;

        static std::string serialize(const std::map<std::string, std::pair<std::string, std::vector<std::string>>>& entries);
        // Reads presets.json text into folded name -> (name, tags). False on anything that is not a library file.
        static bool parse(const std::string& text, std::map<std::string, std::pair<std::string, std::vector<std::string>>>& out);

      private:
        bool write() const;
        std::filesystem::path m_folder;
        // folded preset name -> (name as saved, tags)
        std::map<std::string, std::pair<std::string, std::vector<std::string>>> m_entries;
        std::filesystem::file_time_type m_stamp{};
        bool m_loaded{false};
    };

    // Thumbnail requests, DLL -> Lua ("native\thumbnail_requests.txt"). Saving a preset asks the script
    // for a picture of the character it was made on; the script saves "_library\<preset>.auto.png".
    //
    //   UUEPBS thumbnails 1
    //   <character id>\t<preset name>\t<nonce>
    //
    // One line per preset; asking again (saving over it, Retake) gives it a new nonce.
    class ThumbnailRequests
    {
      public:
        static constexpr const char* kHeader = "UUEPBS thumbnails 1";
        static constexpr size_t kMaxEntries = 64;

        bool add(const std::string& character_id, const std::string& preset);
        std::string serialize() const;
        bool write(const std::filesystem::path& file) const;
        size_t size() const { return m_order.size(); }

      private:
        struct Entry
        {
            std::string preset;
            std::string id;
            uint32_t nonce{0};
        };
        std::vector<Entry> m_order; // oldest first
        uint32_t m_next_nonce{1};
    };
} // namespace uuepbs
