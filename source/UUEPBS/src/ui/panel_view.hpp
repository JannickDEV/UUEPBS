// UUEPBS - contents of the slider window (pure Dear ImGui).
//
// Kept apart from the Win32/D3D11 host so the same drawing code can be run
// headless (tests, screenshots) and the host stays small.
//
// Tabs: Simplified Panel (curated body-part sliders, plus morph groups from the game
// profile), Detailed Panel (every bone, and every morph target), Presets, Status.
// Every widget is drawn through ui::skin(), so a window skin can restyle all of it.
#pragma once

#include "../core/body_groups.hpp"
#include "../core/morphs.hpp"
#include "../core/presets.hpp"
#include "../core/proportions.hpp"
#include "../core/registry.hpp"

#include <imgui.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace uuepbs::ui
{
    // What the view needs from whoever hosts it.
    class PanelHost
    {
      public:
        virtual ~PanelHost() = default;
        virtual bool hook_active() = 0;
        virtual std::string hook_text() = 0;
        virtual void open_folder(const std::filesystem::path& folder) = 0;
        virtual void set_topmost(bool on) = 0;
        virtual std::string renderer_text() { return {}; }
    };

    struct TargetChoice
    {
        std::string id;
        std::string label;
        std::string key{"player"}; // actor key: "player" for the default character, else its address
        std::string engine; // engine object name, e.g. BP_Rokuv3_C_0 (empty from older scripts)
    };

    // Characters the user can switch the sliders to (fed by the Lua script).
    class TargetSource
    {
      public:
        virtual ~TargetSource() = default;
        virtual std::vector<TargetChoice> targets() = 0;
        virtual TargetChoice current_target() = 0;
        virtual void pick_target(const std::string& id) = 0; // "auto" = back to the default
        virtual void refresh_targets() = 0;                    // relist after searching the whole world (Refresh)
        virtual void relist_targets() { refresh_targets(); }   // re-sort what is already known (opening the list)
        virtual void request_rescan() = 0;
        virtual std::string link_text() = 0;   // state of the Lua link, for the Status tab
        virtual std::string hotkey_text() = 0; // e.g. "F6"
        virtual std::string write_diagnostics() { return {}; } // returns where it was written
        virtual std::string refresh_hotkey_text() { return {}; } // e.g. "F7", empty = no hotkey
        // Who an actor key is across reloads (character_memory.hpp), "" when unknown. Renames are
        // stored under it, so they survive reloads.
        virtual std::string identity_of(const std::string& key) { (void)key; return {}; }
        // Ask the script for a portrait of character `id` ("auto" or an address), saved as
        // "<presets>\_portraits\<stem>.auto.png". `retake` takes a new one even if it has one.
        virtual void request_portrait(const std::string& id, const std::string& stem, bool retake)
        {
            (void)id;
            (void)stem;
            (void)retake;
        }
    };


    class PanelView
    {
      public:
        using Clock = std::chrono::steady_clock;

        void configure(const PresetShelf* shelf, PanelHost* host, TargetSource* targets, std::string version, bool topmost);
        // Applies the active skin's colours and sizes (the built-in look without a skin).
        static void apply_theme(float dpi);

        enum class Tab
        {
            Simplified, // was "Body" before 2.2
            Detailed,   // was "Bones" before 2.2
            Presets,
            Status,
            Scale, // Character Scale (2.8), drawn after Body
        };
        enum class Detail
        {
            Bones,
            Morphs
        };

        void draw();
        void refresh_presets();
        void focus_tab(Tab tab) { m_focus_tab = static_cast<int>(tab); }
        void focus_detail(Detail d) { m_focus_detail = static_cast<int>(d); }
        void select_bone(const std::string& bone) { m_selected = bone; }
        void focus_category(const std::string& name) { m_category = name; }
        void focus_scale_section(const std::string& name) { m_scale_section = name; }
        // Opens (true) or closes (false) the "Choose character" grid on the next frame.
        void show_character_chooser(bool show) { (show ? m_choose_open : m_choose_close) = true; }
        void set_message(const std::string& text);

      private:
        void sync();
        bool has_bone(const std::string& bone) const;
        bool has_morph(const std::string& morph) const;
        Spread spread_of(const std::string& bone, Spread fallback) const;
        BoneEdit scale_of(const std::string& bone, Spread fallback) const;
        void write(const std::string& bone, BoneEdit s);
        void write_group(const GroupBones& group, const double axis[3]);
        std::optional<double> morph_of(const std::string& morph) const;
        void write_morph(const std::string& morph, std::optional<double> weight);
        static void tooltip(const char* text);
        void draw_header();
        void draw_target_picker();
        void draw_simplified();
        // Character-creator layout of the Body tab (2.7): body-part categories on the left,
        // the picked category's sliders on the right, in a bordered group under a section bar.
        struct Category
        {
            std::string name;
            size_t parts{0};
            size_t changed{0};
        };
        std::vector<Category> categories() const;
        void draw_category_rail(const std::vector<Category>& cats, float width);
        // The left column of the Body and Character Scale tabs: a heading, one row per entry, a note.
        void draw_rail(const char* id, const char* heading, const std::vector<Category>& cats, float width, std::string& picked_name,
                       const char* note);
        // Character Scale tab (height and proportions, see core/proportions.hpp).
        void draw_scale();
        std::vector<Category> scale_categories() const;
        bool scale_available(const ScaleSlider& sl) const;
        void write_scale(const ScaleSlider& sl, double value);
        void draw_category(const Category& cat);
        // One slider row:  label | hairline track | number box (click to type) | [XYZ] | Reset.
        struct RowResult
        {
            bool changed{false};
            bool axes{false};  // XYZ pressed
            bool reset{false}; // Reset pressed
        };
        struct RowSpec
        {
            const char* id{};
            std::string title;
            std::string tip;
            bool edited{false};
            float lo{0.0f}, hi{1.0f};
            bool log{false};
            float neutral{0.0f};          // where the centre tick goes
            const char* format{"%.2f"};   // number box
            bool unset{false};            // number box shows `unset_hint` instead of a value
            const char* unset_hint{"game"};
            bool has_axes{false};
            bool axes_open{false};
            bool can_reset{false};
            float label_w{0.0f};
        };
        static RowResult part_row(const RowSpec& spec, float& v);
        // Flat 2px track with a centre tick and a 6x16 thumb; a skin with slider images keeps its own slider.
        static bool hairline_slider(const char* id, float* v, float lo, float hi, bool log, float neutral);
        // Lighter full-width bar with a title (and optional right-aligned button). True when the button was pressed.
        static bool section_bar(const char* title, const char* button = nullptr, bool button_enabled = true, const char* icon_key = "reset");
        static bool compact_button(const char* label, const char* icon_key);
        static float compact_button_width(const char* label, const char* icon_key);
        // Title row, character banner (portrait/monogram, name, details, Change character / Rename).
        void draw_character_banner();
        // "Choose character" modal: a grid of square tiles.
        void draw_character_chooser();
        void draw_rename_popup();
        std::string identity_of(const std::string& key);
        ImTextureData* portrait_for(const std::string& key, const std::string& identity, const std::string& label, const std::string& engine);
        // Same lookup; when there is no image, asks the script to take one (once per character).
        ImTextureData* portrait_or_request(const std::string& id, const std::string& key, const std::string& identity, const std::string& label,
                                           const std::string& engine);
        static std::string portrait_stem(const std::string& key, const std::string& identity, const std::string& label, const std::string& engine);
        void draw_detailed();
        void draw_detailed_bones();
        void draw_detailed_morphs();
        void load_preset(const std::string& name);
        void save_preset(const std::string& name);
        void draw_presets();
        void draw_status();
        void draw_skin_picker();
        void draw_edited_characters();
        bool is_edited(const std::string& key) const;

        const PresetShelf* m_shelf{};
        PanelHost* m_host{};
        TargetSource* m_targets{};
        std::vector<TargetChoice> m_target_list{};
        TargetChoice m_target{};
        Clock::time_point m_target_time{};
        // While the character list is open it takes the fresh nearest-first order that opening it asked for,
        // then stays put, so entries don't move under the mouse.
        bool m_target_combo_open{false};
        char m_rename_buf[96]{};
        bool m_rename_open{false};
        bool m_choose_open{false};
        bool m_choose_close{false};
        std::string m_rename_key{};      // CharacterNames key being renamed
        bool m_rename_stable{false};
        std::string m_rename_label{};    // the game's name, shown in the rename popup
        std::map<std::string, std::string> m_identity_cache{}; // actor key -> identity (refreshed with the list)
        Clock::time_point m_target_combo_since{};
        BodyMap m_body{};
        uint64_t m_body_revision{0};
        std::string m_body_status{};
        std::map<std::string, std::string> m_bone_group{}; // folded bone -> Simplified Panel group title
        std::string m_mirror_text{};
        std::string m_version{};
        bool m_topmost{true};
        int m_focus_tab{-1};
        int m_focus_detail{-1};

        SkeletonView m_skeleton{};
        std::set<std::string> m_bone_keys{};
        uint64_t m_skeleton_revision{~0ull};
        EditBook m_book{};
        uint64_t m_book_revision{~0ull};
        std::vector<RigSummary> m_rigs{};
        std::vector<ActorSummary> m_edited{}; // characters with sliders (active first)
        std::vector<std::string> m_remembered{}; // NPC identities with saved sliders
        Clock::time_point m_rigs_time{};

        // morph targets
        std::vector<std::string> m_morph_names{}; // after the profile's ExcludeMorphs
        std::set<std::string> m_morph_keys{};
        uint64_t m_morph_names_revision{~0ull};
        MorphBook m_morphs{};
        uint64_t m_morph_revision{~0ull};
        MorphProfile m_morph_profile{};
        std::string m_morph_status{};
        uint64_t m_morph_profile_revision{0};
        std::vector<MorphGroupMorphs> m_morph_groups{};
        std::set<std::string> m_animated{};
        size_t m_morphs_hidden{0};
        char m_morph_filter[64]{};
        bool m_morph_only_edited{false};

        std::map<std::string, bool> m_group_open{};
        std::string m_category{}; // Simplified Panel category shown on the right
        std::string m_scale_section{}; // Character Scale section shown on the right
        std::map<std::string, Spread> m_spread_hint{};
        char m_filter[64]{};
        bool m_only_edited{false};
        bool m_mirror{true};
        std::string m_selected{};

        std::vector<std::string> m_presets{};
        Clock::time_point m_presets_time{};
        char m_preset_name[96]{};
        std::string m_picked{};
        std::string m_pending{};

        std::vector<std::string> m_skins{};
        Clock::time_point m_skins_time{};

        std::mutex m_message_lock;
        std::string m_message{};
        Clock::time_point m_message_time{};
    };
} // namespace uuepbs::ui
