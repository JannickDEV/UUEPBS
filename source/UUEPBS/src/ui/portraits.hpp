// UUEPBS - character portraits for the banner and the "Choose character" grid
//
// Images live in "<presets>/_portraits/": PNG or JPG named after the character. A character
// is looked up by, in order: its key ("player" or the NPC's identity), the name you gave it,
// its game name and its engine name (case does not matter, characters Windows does not allow
// in file names are dropped). Characters without an image show a 3-letter monogram instead.
//
// Files you put there always win. Automatic portraits (taken by the Lua script) are written
// to the same folder as "<key>.auto.png", so a hand-made "<key>.png" replaces them.
//
// Window thread only, except set_folder(). Images are decoded the first time they are drawn
// and reloaded when the file changes (the folder is checked about every 2 seconds).
#pragma once

#include "image_decode.hpp"

#include <imgui.h>

#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace uuepbs::ui
{
    class Portraits
    {
      public:
        static constexpr int kMaxSide = 256; // decoded images are shrunk to this
        static constexpr size_t kMaxLoaded = 96;

        void set_folder(std::filesystem::path folder); // any thread
        std::filesystem::path folder() const;

        // Rescans the folder now and then, frees replaced textures. Before ImGui::NewFrame().
        void update(double now_seconds);
        // The texture for the first candidate that has a file, or nullptr.
        ImTextureData* find(const std::vector<std::string>& candidates);
        // Draws `tex` into the square a..b, cropped to fill it (centre).
        static void draw(ImDrawList* dl, ImTextureData* tex, ImVec2 a, ImVec2 b);
        // Release every texture (after the backend shut down, before ImGui::DestroyContext()).
        void shutdown();

        // File-name form of a candidate ("Anca the Guard" -> "anca the guard"); "" when unusable.
        static std::string file_key(const std::string& name);
        size_t loaded() const { return m_loaded.size(); }

      private:
        struct Entry
        {
            std::filesystem::path file;
            std::filesystem::file_time_type stamp{};
            ImTextureData* tex{};
            bool failed{false};
        };
        void rescan();
        void retire(ImTextureData* tex);

        mutable std::mutex m_folder_lock;
        std::filesystem::path m_folder;
        std::filesystem::path m_scanned_folder;
        std::map<std::string, std::filesystem::path> m_files; // file key -> best file (hand-made before .auto)
        std::map<std::string, Entry> m_loaded;                // file key -> texture
        std::vector<ImTextureData*> m_retired;
        double m_next_scan{-1.0};
    };

    Portraits& portraits();
    // Preset thumbnails for the preset browser ("<presets>/_library/", see core/preset_library.hpp):
    // the same loader, looked up by preset name.
    Portraits& preset_thumbnails();
} // namespace uuepbs::ui
