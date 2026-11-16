#include "panel_view.hpp"

#include "../core/camera.hpp"
#include "../core/proportions.hpp"

#include "../core/character_names.hpp"
#include "../core/looks.hpp"
#include "../core/morph_sort.hpp"
#include "../core/rig_names.hpp"
#include "portraits.hpp"
#include "skin.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cfloat>
#include <cstring>

namespace uuepbs::ui
{
    namespace
    {
        // Colours come from the active skin (the built-in look when there is none).
        const ImVec4& accent() { return skin().color(Role::Accent); }
        const ImVec4& accent_dim() { return skin().color(Role::AccentDim); }
        const ImVec4& good() { return skin().color(Role::Good); }
        const ImVec4& warn() { return skin().color(Role::Warn); }
        const ImVec4& muted() { return skin().color(Role::Muted); }

        // Log scale from 0.1x to 10x puts 1.0x exactly in the middle of the slider.
        constexpr float kScaleMin = 0.10f;
        constexpr float kScaleMax = 10.0f;
        // Morph sliders run over the whole kMorphMin..kMorphMax range (-5..5), 0 in the middle.
        constexpr float kMorphSliderMin = static_cast<float>(kMorphMin);
        constexpr float kMorphSliderMax = static_cast<float>(kMorphMax);

        bool contains_folded(const std::string& haystack, const std::string& folded_needle)
        {
            return folded_needle.empty() || fold_case(haystack).find(folded_needle) != std::string::npos;
        }
    } // namespace

    // Ctrl + mouse wheel over the slider just drawn nudges it by `step`
    // (Ctrl + Shift: by `fine`). The plain wheel keeps scrolling the window.
    static bool wheel_nudge(float& v, float step, float fine, float lo, float hi)
    {
        const ImGuiIO& io = ImGui::GetIO();
        if (!io.KeyCtrl)
        {
            return false;
        }
        ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
        if (!ImGui::IsItemHovered() || io.MouseWheel == 0.0f)
        {
            return false;
        }
        const float delta = (io.KeyShift ? fine : step) * (io.MouseWheel > 0.0f ? 1.0f : -1.0f);
        v = std::min(hi, std::max(lo, v + delta));
        return true;
    }

    // Portrait lookup order: key ("player" / identity), the user's name, the game name, the engine name.
    ImTextureData* PanelView::portrait_for(const std::string& key, const std::string& identity, const std::string& label, const std::string& engine)
    {
        std::string game = label;
        static const std::string tag = " (player)";
        if (game.size() > tag.size() && game.compare(game.size() - tag.size(), tag.size(), tag) == 0)
        {
            game.resize(game.size() - tag.size());
        }
        const std::string own = CharacterNames::instance().get(CharacterNames::key_for(key, identity));
        return portraits().find({CharacterNames::key_for(key, identity), own, game, engine});
    }

    std::string PanelView::portrait_stem(const std::string& key, const std::string& identity, const std::string& label, const std::string& engine)
    {
        (void)key;
        std::string game = label;
        static const std::string tag = " (player)";
        if (game.size() > tag.size() && game.compare(game.size() - tag.size(), tag.size(), tag) == 0)
        {
            game.resize(game.size() - tag.size());
        }
        // The most stable name first: identity (NPCs), engine object name, then the game name.
        for (const std::string* s : {&identity, &engine, static_cast<const std::string*>(&game)})
        {
            const std::string clean = PresetShelf::clean_name(*s);
            if (!clean.empty())
            {
                return clean;
            }
        }
        return {};
    }

    ImTextureData* PanelView::portrait_or_request(const std::string& id, const std::string& key, const std::string& identity, const std::string& label,
                                                  const std::string& engine)
    {
        ImTextureData* face = portrait_for(key, identity, label, engine);
        if (!face && m_targets && !id.empty())
        {
            const std::string stem = portrait_stem(key, identity, label, engine);
            if (!stem.empty())
            {
                m_targets->request_portrait(id, stem, false);
            }
        }
        return face;
    }

    std::string PanelView::identity_of(const std::string& key)
    {
        if (key == kPlayerActor)
        {
            return {};
        }
        for (const ActorSummary& a : m_edited)
        {
            if (a.key == key && !a.identity.empty())
            {
                return a.identity;
            }
        }
        if (const auto it = m_identity_cache.find(key); it != m_identity_cache.end())
        {
            return it->second;
        }
        std::string id = m_targets ? m_targets->identity_of(key) : std::string();
        m_identity_cache[key] = id;
        return id;
    }

    void PanelView::configure(const PresetShelf* shelf, PanelHost* host, TargetSource* targets, std::string version, bool topmost)
    {
        m_shelf = shelf;
        m_host = host;
        m_targets = targets;
        m_version = std::move(version);
        m_topmost = topmost;
    }

    void PanelView::set_message(const std::string& text)
    {
        std::lock_guard guard(m_message_lock);
        m_message = text;
        m_message_time = Clock::now();
    }

    void PanelView::apply_theme(float dpi)
    {
        skin().apply_style(dpi);
    }

    void PanelView::sync()
    {
        Registry& reg = Registry::instance();
        if (reg.skeleton_revision() != m_skeleton_revision)
        {
            m_skeleton = reg.skeleton(&m_skeleton_revision);
            m_bone_keys.clear();
            for (const auto& n : m_skeleton.names)
            {
                m_bone_keys.insert(fold_case(n));
            }
            m_body_revision = 0; // re-resolve below
        }
        const uint64_t body_rev = BodyMapSource::instance().revision();
        if (body_rev != m_body_revision)
        {
            BoneDictionary dict;
            std::vector<UserGroup> user;
            BodyMapSource::instance().get(dict, user, m_body_status);
            m_body = resolve_body_groups(m_skeleton.names, m_skeleton.parents, dict, user);
            m_bone_group.clear();
            for (const GroupBones& gb : m_body.groups)
            {
                for (const std::string& b : gb.bones)
                {
                    m_bone_group[fold_case(b)] = gb.group.title;
                }
            }
            for (const PartBones& pb : m_body.parts)
            {
                const BonePart* part = find_bone_part(pb.id);
                for (const std::string& b : pb.bones)
                {
                    m_bone_group.emplace(fold_case(b), part ? part->title : pb.id);
                }
            }
            m_body_revision = body_rev;
            refresh_body_sliders();
        }
        if (reg.edit_revision() != m_book_revision)
        {
            m_book = reg.edits(&m_book_revision);
        }

        // Morph targets: names from the meshes, minus the profile's ExcludeMorphs.
        const uint64_t profile_rev = MorphProfileSource::instance().revision();
        if (reg.morph_names_revision() != m_morph_names_revision || profile_rev != m_morph_profile_revision)
        {
            MorphProfileSource::instance().get(m_morph_profile, m_morph_status);
            m_morph_profile_revision = profile_rev;
            const std::vector<std::string> all = reg.morph_names(&m_morph_names_revision);
            m_morph_names.clear();
            m_morph_keys.clear();
            for (const std::string& m : all)
            {
                if (!m_morph_profile.excluded(m))
                {
                    m_morph_names.push_back(m);
                    m_morph_keys.insert(fold_case(m));
                }
            }
            m_morphs_hidden = all.size() - m_morph_names.size();
            m_morph_groups = body_morph_groups(m_morph_names, m_morph_profile);
            m_morph_place.clear();
            for (const MorphGroupMorphs& mg : m_morph_groups)
            {
                const std::string where = (mg.group.section.empty() ? std::string("Morphs") : mg.group.section) + " > " + mg.group.title;
                for (const std::string& m : mg.morphs)
                {
                    m_morph_place.emplace(fold_case(m), where);
                }
            }
            m_morphs_advanced_only = m_morph_names.size() - std::min(m_morph_names.size(), m_morph_place.size());
            refresh_body_sliders();
        }
        if (reg.morph_revision() != m_morph_revision)
        {
            m_morphs = reg.morphs(&m_morph_revision);
        }

        const auto now = Clock::now();
        if (now - m_rigs_time > std::chrono::milliseconds(500))
        {
            m_rigs = reg.rigs();
            m_edited = reg.edited_actors();
            m_remembered = reg.remembered();
            m_rigs_time = now;
            m_mirror_text = reg.mirror_source();
            m_animated = reg.animated_morphs();
            // Released or despawned characters lose their undo history (their address keys can be reused).
            std::set<std::string> keep{kPlayerActor, reg.active_actor()};
            for (const ActorSummary& a : m_edited)
            {
                keep.insert(a.key);
            }
            m_history.retain(keep);
        }
        if (m_targets && now - m_target_time > std::chrono::milliseconds(400))
        {
            if (!m_target_combo_open || now - m_target_combo_since < std::chrono::milliseconds(2500))
            {
                m_target_list = m_targets->targets();
            }
            m_target = m_targets->current_target();
            m_target_time = now;
            m_identity_cache.clear();
        }
    }

    bool PanelView::has_bone(const std::string& bone) const
    {
        return m_bone_keys.count(fold_case(bone)) > 0;
    }

    bool PanelView::has_morph(const std::string& morph) const
    {
        return m_morph_keys.count(fold_case(morph)) > 0;
    }

    Spread PanelView::spread_of(const std::string& bone, Spread fallback) const
    {
        const std::string key = fold_case(bone);
        const auto it = m_book.find(key);
        if (it != m_book.end())
        {
            return it->second.edit.spread;
        }
        const auto hint = m_spread_hint.find(key);
        return hint != m_spread_hint.end() ? hint->second : fallback;
    }

    BoneEdit PanelView::scale_of(const std::string& bone, Spread fallback) const
    {
        const auto it = m_book.find(fold_case(bone));
        if (it != m_book.end())
        {
            return it->second.edit;
        }
        BoneEdit s;
        s.spread = spread_of(bone, fallback);
        return s;
    }

    void PanelView::write(const std::string& bone, BoneEdit s)
    {
        Registry& reg = Registry::instance();
        s.clamp();
        const std::string key = fold_case(bone);
        m_spread_hint[key] = s.spread;
        if (s.is_neutral())
        {
            reg.clear_bone(bone);
            m_book.erase(key);
        }
        else
        {
            reg.set_bone(bone, s);
            m_book[key] = EditEntry{bone, s};
        }
        m_book_revision = reg.edit_revision();
    }

    // Simplified Panel sliders only change scale; each bone keeps its own rotation/move.
    void PanelView::write_group(const GroupBones& group, const double axis[3])
    {
        for (const std::string& bone : group.bones)
        {
            if (!has_bone(bone))
            {
                continue;
            }
            BoneEdit s = scale_of(bone, group.group.spread);
            s.axis[0] = axis[0];
            s.axis[1] = axis[1];
            s.axis[2] = axis[2];
            s.spread = spread_of(bone, group.group.spread);
            write(bone, s);
        }
    }

    std::optional<double> PanelView::morph_of(const std::string& morph) const
    {
        const auto it = m_morphs.find(fold_case(morph));
        if (it == m_morphs.end())
        {
            return std::nullopt;
        }
        return it->second.weight;
    }

    // nullopt gives the morph back to the game.
    void PanelView::write_morph(const std::string& morph, std::optional<double> weight)
    {
        Registry& reg = Registry::instance();
        const std::string key = fold_case(morph);
        if (weight)
        {
            const double w = clamp_morph(*weight);
            reg.set_morph(morph, w);
            m_morphs[key] = MorphEntry{morph, w};
        }
        else
        {
            reg.clear_morph(morph);
            m_morphs.erase(key);
        }
        m_morph_revision = reg.morph_revision();
    }

    void PanelView::tooltip(const char* text)
    {
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort) && text && *text)
        {
            ImGui::SetTooltip("%s", text);
        }
    }

    namespace
    {
        float dpi_px(float v) { return v * ImGui::GetStyle().FontScaleDpi; }

        ImU32 u32(const ImVec4& c) { return ImGui::GetColorU32(c); }

        // Small filled square in front of labels (drawn: fonts may lack the glyph).
        void square_bullet(ImDrawList* dl, ImVec2 centre, float size, ImU32 col)
        {
            const float h = std::floor(size * 0.5f);
            dl->AddRectFilled(ImVec2(std::floor(centre.x) - h, std::floor(centre.y) - h), ImVec2(std::floor(centre.x) + h, std::floor(centre.y) + h), col);
        }

        // Up to three letters/digits of the name, upper case ("Roku (player)" -> "ROK").
        std::string monogram(const std::string& name)
        {
            std::string out;
            for (const char ch : name)
            {
                if (ch == '(')
                {
                    break;
                }
                if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9'))
                {
                    out.push_back(ch >= 'a' && ch <= 'z' ? static_cast<char>(ch - 'a' + 'A') : ch);
                    if (out.size() == 3)
                    {
                        break;
                    }
                }
            }
            return out.empty() ? std::string("?") : out;
        }

        // Text cut to `max_w` with "..." (byte-wise; fine for the names shown here).
        std::string fit_text(const std::string& text, float max_w)
        {
            if (ImGui::CalcTextSize(text.c_str()).x <= max_w)
            {
                return text;
            }
            const float dots = ImGui::CalcTextSize("...").x;
            std::string out = text;
            while (!out.empty() && ImGui::CalcTextSize(out.c_str()).x + dots > max_w)
            {
                out.pop_back();
                while (!out.empty() && (static_cast<unsigned char>(out.back()) & 0xC0) == 0x80)
                {
                    out.pop_back(); // whole UTF-8 characters only
                }
                if (!out.empty() && (static_cast<unsigned char>(out.back()) & 0xC0) == 0xC0)
                {
                    out.pop_back();
                }
            }
            return out + "...";
        }

        // Monogram frame (portrait stand-in): dark square, 1px border, letters centred.
        void monogram_frame(ImDrawList* dl, ImVec2 a, ImVec2 b, const std::string& letters, float font_size, ImU32 fill, ImU32 border, ImU32 text)
        {
            dl->AddRectFilled(a, b, fill);
            dl->AddRect(a, b, border, 0.0f, 0, 1.0f);
            ImFont* font = ImGui::GetFont();
            const ImVec2 ts = font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, letters.c_str());
            dl->AddText(font, font_size, ImVec2(std::floor((a.x + b.x - ts.x) * 0.5f), std::floor((a.y + b.y - ts.y) * 0.5f)), text, letters.c_str());
        }
    } // namespace

    void PanelView::draw()
    {
        sync();
        track_history();
        Skin& sk = skin();
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos);
        ImGui::SetNextWindowSize(vp->WorkSize);
        const ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
        if (sk.font())
        {
            ImGui::PushFont(sk.font(), 0.0f);
        }
        ImGui::Begin("##uuepbs", nullptr, flags);
        sk.draw_window_background();

        draw_header();

        // Flush rectangular tabs on one 1px line; the selected tab is filled and gets a top accent line.
        const ImGuiStyle& style = ImGui::GetStyle();
        const ImVec2 tab_pad(dpi_px(22.0f), dpi_px(9.0f));
        const float tabs_y = ImGui::GetCursorScreenPos().y;
        const float tab_h = ImGui::GetFontSize() + tab_pad.y * 2.0f;
        {
            const float x0 = ImGui::GetWindowPos().x, x1 = x0 + ImGui::GetWindowWidth();
            ImGui::GetWindowDrawList()->AddLine(ImVec2(x0, tabs_y + tab_h - 0.5f), ImVec2(x1, tabs_y + tab_h - 0.5f), u32(sk.color(Role::Border)), 1.0f);
        }
        ImGui::PushStyleVar(ImGuiStyleVar_TabBarBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_TabBarOverlineSize, dpi_px(2.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(0.0f, style.ItemInnerSpacing.y));
        const bool tabs = ImGui::BeginTabBar("##tabs", ImGuiTabBarFlags_DrawSelectedOverline);
        ImGui::PopStyleVar(3);
        if (tabs)
        {
            auto tab = [&](const char* label, const char* icon, Tab t) {
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, tab_pad);
                (void)icon; // text-only tabs since 2.7 (skins may still define the icon keys; they are ignored here)
                const bool open = sk.begin_tab_item(label, {}, m_focus_tab == static_cast<int>(t) ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None);
                ImGui::PopStyleVar();
                if (open)
                {
                    ImGui::Dummy(ImVec2(0.0f, dpi_px(6.0f)));
                }
                return open;
            };
            // The ### ids (and icon keys) stay as before so skins and saved focus keep working.
            if (tab("Body###simplified", "simplified", Tab::Simplified))
            {
                draw_simplified();
                ImGui::EndTabItem();
            }
            if (tab("Character Scale###scale", "scale", Tab::Scale))
            {
                draw_scale();
                ImGui::EndTabItem();
            }
            if (tab("Looks###looks", "looks", Tab::Looks))
            {
                draw_looks();
                ImGui::EndTabItem();
            }
            if (tab("Presets###presets", "presets", Tab::Presets))
            {
                draw_presets();
                ImGui::EndTabItem();
            }
            if (tab("Advanced###detailed", "detailed", Tab::Detailed))
            {
                draw_detailed();
                ImGui::EndTabItem();
            }
            if (tab("Status###status", "status", Tab::Status))
            {
                draw_status();
                ImGui::EndTabItem();
            }
            m_focus_tab = -1;
            ImGui::EndTabBar();
        }
        ImGui::End();
        if (sk.font())
        {
            ImGui::PopFont();
        }
    }

    // Title row: "UUEPBS" + version line on the left, Enabled on the right; then the character banner.
    void PanelView::draw_header()
    {
        Skin& sk = skin();
        const ImGuiStyle& style = ImGui::GetStyle();
        const float base = style.FontSizeBase;
        const ImVec2 top = ImGui::GetCursorScreenPos();
        sk.draw_header_strip(base * style.FontScaleDpi * 2.4f);
        ImGui::BeginGroup();
        if (!sk.logo(ImGui::GetFontSize() * 1.25f))
        {
            ImGui::PushFont(nullptr, base * 1.9f);
            ImGui::PushStyleColor(ImGuiCol_Text, sk.color(Role::Title));
            ImGui::TextUnformatted("UUEPBS");
            ImGui::PopStyleColor();
            ImGui::PopFont();
        }
        ImGui::PushFont(nullptr, base * 0.72f);
        std::string sub = m_version + "   \xC2\xB7   BY XTGMODS";
        for (char& c : sub)
        {
            c = (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
        }
        ImGui::TextColored(muted(), "%s", sub.c_str());
        ImGui::PopFont();
        ImGui::EndGroup();
        const ImVec2 below = ImGui::GetCursorScreenPos();
        const float title_h = below.y - top.y;

        const float box = ImGui::GetFrameHeight() + ImGui::CalcTextSize("Enabled").x + style.ItemInnerSpacing.x;
        ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x - box, top.y + (title_h - ImGui::GetFrameHeight()) * 0.5f));
        bool on = Registry::instance().enabled();
        if (sk.checkbox("Enabled", &on))
        {
            Registry::instance().set_enabled(on);
        }
        tooltip("Turn every slider on or off at once (nothing is lost).");
        ImGui::SetCursorScreenPos(below);
        ImGui::Dummy(ImVec2(0.0f, dpi_px(4.0f)));

        draw_target_picker();
    }

    bool PanelView::compact_button(const char* label, const char* icon_key)
    {
        Skin& sk = skin();
        if (sk.has_icon(icon_key))
        {
            const std::string id = std::string("##") + label;
            return sk.button(id.c_str(), icon_key);
        }
        return sk.button(label, icon_key);
    }

    float PanelView::compact_button_width(const char* label, const char* icon_key)
    {
        Skin& sk = skin();
        if (sk.has_icon(icon_key))
        {
            return sk.button_width("##", icon_key);
        }
        return sk.button_width(label, icon_key);
    }

    // The character banner replaces the old drop-down: who is being edited, and the way to change it.
    void PanelView::draw_target_picker()
    {
        draw_character_banner();
        draw_character_chooser();
        draw_rename_popup();
    }

    void PanelView::draw_character_banner()
    {
        Skin& sk = skin();
        const ImGuiStyle& style = ImGui::GetStyle();
        const float base = style.FontSizeBase;
        const float portrait = std::floor(dpi_px(64.0f));
        const float pad = dpi_px(14.0f);
        float text_h = 0.0f;
        {
            ImGui::PushFont(nullptr, base * 1.4f);
            text_h += ImGui::GetTextLineHeight();
            ImGui::PopFont();
            ImGui::PushFont(nullptr, base * 0.86f);
            text_h += ImGui::GetTextLineHeight() * 2.0f;
            ImGui::PopFont();
            text_h += style.ItemSpacing.y * 2.0f;
        }
        const float height = std::floor(std::max(portrait, text_h) + pad * 2.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pad, pad));
        sk.begin_child("##banner", ImVec2(0, height), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding,
                       ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PopStyleVar();
        ImDrawList* dl = ImGui::GetWindowDrawList();

        const std::string identity = identity_of(m_target.key);
        const std::string name = m_target.label.empty() ? std::string("No character yet")
                                                        : CharacterNames::instance().shown(m_target.label, m_target.key, identity);
        // Portrait frame: the character's image when there is one, else its monogram.
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImTextureData* face =
            m_target.label.empty() ? nullptr : portrait_or_request(m_target.id, m_target.key, identity, m_target.label, m_target.engine);
        monogram_frame(dl, p, ImVec2(p.x + portrait, p.y + portrait), face || m_target.label.empty() ? std::string(face ? "" : "?") : monogram(name),
                       base * style.FontScaleDpi * 1.35f, u32(ImGui::GetStyleColorVec4(ImGuiCol_FrameBg)), u32(sk.color(Role::Border)),
                       u32(sk.color(Role::Title)));
        if (face)
        {
            Portraits::draw(dl, face, ImVec2(p.x + 1.0f, p.y + 1.0f), ImVec2(p.x + portrait - 1.0f, p.y + portrait - 1.0f));
        }
        ImGui::Dummy(ImVec2(portrait, portrait));
        ImGui::SameLine(0.0f, dpi_px(16.0f));

        // Right column first (its width limits the text column): Change character, then note + Rename.
        const float change_w = sk.button_width("Change character    ");
        const float right_x = ImGui::GetCursorPos().x + ImGui::GetContentRegionAvail().x; // GetWindowContentRegionMax() is gone under IMGUI_DISABLE_OBSOLETE_FUNCTIONS
        std::string note;
        std::string others;
        size_t n_others = 0;
        for (const ActorSummary& a : m_edited)
        {
            if (!a.active)
            {
                others += (others.empty() ? "" : ", ") + CharacterNames::instance().shown(a.label, a.key, a.identity);
                ++n_others;
            }
        }
        {
            std::lock_guard guard(m_message_lock);
            if (Clock::now() - m_message_time < std::chrono::seconds(6))
            {
                note = m_message;
            }
        }
        const bool is_message = !note.empty();
        if (!is_message)
        {
            note = n_others == 0 ? "no other characters edited"
                                 : std::to_string(n_others) + (n_others == 1 ? " other character edited" : " other characters edited");
        }
        ImGui::PushFont(nullptr, base * 0.86f);
        const float rename_w = ImGui::CalcTextSize("Rename").x + style.FramePadding.x * 2.0f;
        const float history_w = undo_redo_width() + style.ItemSpacing.x; // Undo / Redo sit left of Rename
        const std::string shown_note = fit_text(note, std::max(dpi_px(60.0f), ImGui::GetContentRegionAvail().x * 0.30f - rename_w - history_w));
        const float note_w = ImGui::CalcTextSize(shown_note.c_str()).x;
        ImGui::PopFont();
        const float compare_w = compare_switch_width();
        const float right_w = std::max(change_w + style.ItemSpacing.x + compare_w, note_w + history_w + rename_w + style.ItemSpacing.x);

        ImGui::BeginGroup();
        const float text_w = std::max(dpi_px(80.0f), right_x - ImGui::GetCursorPosX() - right_w - style.ItemSpacing.x * 2.0f);
        ImGui::PushFont(nullptr, base * 1.4f);
        ImGui::TextUnformatted(fit_text(name, text_w).c_str());
        ImGui::PopFont();
        if (name != m_target.label && !m_target.label.empty())
        {
            const std::string tip = "Game name: " + m_target.label;
            tooltip(tip.c_str());
        }

        size_t live = 0;
        for (const auto& r : m_rigs)
        {
            live += (!r.stale && r.frames > 0) ? 1 : 0;
        }
        ImGui::PushFont(nullptr, base * 0.86f);
        {
            std::string meta = !m_target.engine.empty() ? m_target.engine : !identity.empty() ? identity : m_skeleton.owner;
            meta += meta.empty() ? "" : "   \xC2\xB7   ";
            meta += m_target.id == "auto" || m_target.id.empty() ? "automatic (the character you control)" : "picked";
            ImGui::TextColored(muted(), "%s", fit_text(meta, text_w).c_str());
        }
        {
            // Live line: dot + state.
            ImVec4 col = good();
            std::string line;
            char buf[160];
            if (m_skeleton.names.empty())
            {
                col = warn();
                line = "No character mesh found yet - press Change character > Refresh";
            }
            else if (!m_host->hook_active())
            {
                col = warn();
                line = "Waiting for the pose hook (see Status)";
            }
            else if (live == 0)
            {
                col = warn();
                std::snprintf(buf, sizeof(buf), "Tracking %zu mesh%s - waiting for an animated frame", m_rigs.size(), m_rigs.size() == 1 ? "" : "es");
                line = buf;
            }
            else if (Registry::instance().comparing())
            {
                col = accent();
                line = "Before: the game's own body   \xC2\xB7   your edits are kept";
            }
            else
            {
                std::snprintf(buf, sizeof(buf), "%s   \xC2\xB7   live on %zu mesh%s   \xC2\xB7   %zu bones%s", Registry::instance().enabled() ? "Edits on" : "Edits off",
                              live, live == 1 ? "" : "es", m_skeleton.names.size(),
                              m_morph_names.empty() ? "" : ("   \xC2\xB7   " + std::to_string(m_morph_names.size()) + " morphs").c_str());
                line = buf;
            }
            const ImVec2 q = ImGui::GetCursorScreenPos();
            const float r = std::max(2.0f, ImGui::GetFontSize() * 0.18f);
            dl->AddCircleFilled(ImVec2(q.x + r, q.y + ImGui::GetTextLineHeight() * 0.5f + 1.0f), r, u32(col));
            ImGui::SetCursorScreenPos(ImVec2(q.x + r * 2.0f + style.ItemInnerSpacing.x * 1.5f, q.y));
            ImGui::TextColored(muted(), "%s", fit_text(line, text_w - r * 4.0f).c_str());
        }
        ImGui::PopFont();
        ImGui::EndGroup();

        // Right: Before / After and Change character (opens the grid), then Rename and a short note.
        ImGui::SetCursorPos(ImVec2(right_x - change_w - style.ItemSpacing.x - compare_w, pad));
        draw_compare_switch();
        ImGui::SetCursorPos(ImVec2(right_x - change_w, pad));
        ImGui::BeginDisabled(!m_targets);
        if (sk.button("Change character    ", {}, ImVec2(change_w, 0.0f)))
        {
            m_targets->relist_targets(); // sorted by the current distances (cheap; Refresh searches the world)
            m_target_combo_since = Clock::now();
            m_choose_open = true; // opened outside this child, where the modal is drawn
        }
        ImGui::EndDisabled();
        {
            // Down arrow inside the button's right padding.
            const ImVec2 b = ImGui::GetItemRectMax();
            const float s = ImGui::GetFontSize() * 0.32f;
            const ImVec2 c(b.x - style.FramePadding.x - s, (ImGui::GetItemRectMin().y + b.y) * 0.5f);
            dl->AddTriangleFilled(ImVec2(c.x - s, c.y - s * 0.5f), ImVec2(c.x + s, c.y - s * 0.5f), ImVec2(c.x, c.y + s * 0.6f), u32(ImGui::GetStyleColorVec4(ImGuiCol_Text)));
        }
        tooltip("Whose body the sliders edit: every character loaded right now, nearest first.\n"
                "Characters you have edited keep their sliders while you edit someone else.");

        ImGui::PushFont(nullptr, base * 0.86f);
        const float row_y = pad + ImGui::GetFrameHeight() + style.ItemSpacing.y * 0.75f;
        ImGui::SetCursorPos(ImVec2(right_x - rename_w - history_w - style.ItemSpacing.x - note_w, row_y + (ImGui::GetFrameHeight() - ImGui::GetTextLineHeight()) * 0.5f));
        ImGui::TextColored(is_message ? accent() : muted(), "%s", shown_note.c_str());
        if (is_message)
        {
            tooltip(note.c_str());
        }
        else if (n_others > 0)
        {
            const std::string tip = "Also keeping sliders on: " + others;
            tooltip(tip.c_str());
        }
        ImGui::SetCursorPos(ImVec2(right_x - rename_w - history_w, row_y));
        draw_undo_redo();
        ImGui::SetCursorPos(ImVec2(right_x - rename_w, row_y));
        ImGui::BeginDisabled(m_target.label.empty());
        if (sk.button("Rename", {}, ImVec2(rename_w, 0.0f)))
        {
            m_rename_key = CharacterNames::key_for(m_target.key, identity, &m_rename_stable);
            m_rename_label = m_target.label;
            std::snprintf(m_rename_buf, sizeof(m_rename_buf), "%s", CharacterNames::instance().get(m_rename_key).c_str());
            m_rename_open = true;
        }
        ImGui::EndDisabled();
        tooltip("Give this character your own name. It is only shown in this window and saved next to your presets.");
        ImGui::PopFont();
        sk.end_child();
        if (m_rename_open)
        {
            ImGui::OpenPopup("Rename character");
            m_rename_open = false;
        }
        if (m_choose_open)
        {
            ImGui::OpenPopup("##choose");
            m_choose_open = false;
        }
        ImGui::Dummy(ImVec2(0.0f, dpi_px(4.0f)));
    }

    float PanelView::compare_switch_width()
    {
        const ImGuiStyle& style = ImGui::GetStyle();
        return std::ceil(std::max(ImGui::CalcTextSize("Before").x, ImGui::CalcTextSize("After").x) + style.FramePadding.x * 2.0f) * 2.0f;
    }

    // Before | After: two flush cells; the one showing is filled with the accent and gets an accent top
    // line (the line also shows on skins that draw buttons with images). "Before" shows the game's own
    // body for every character, "After" the sliders. Moving a slider or closing the window goes back to After.
    void PanelView::draw_compare_switch()
    {
        Skin& sk = skin();
        Registry& reg = Registry::instance();
        const ImGuiStyle& style = ImGui::GetStyle();
        const float cell = compare_switch_width() * 0.5f;
        const bool before = reg.comparing();
        const bool held = reg.compare().held();
        std::string tip = "Before shows the game's own body, After shows your sliders. Nothing is lost.\n"
                          "Moving a slider or closing the window goes back to After.";
        const std::string key = m_targets ? m_targets->compare_hotkey_text() : std::string();
        tip += key.empty() ? std::string("\nSet CompareKey in config.lua to compare by holding a key in game.")
                           : "\nOr hold " + key + " in game to see the original while the key is down.";
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, style.ItemSpacing.y));
        for (int i = 0; i < 2; ++i)
        {
            const bool is_before = i == 0;
            const bool on = is_before == before;
            if (!is_before)
            {
                ImGui::SameLine();
            }
            if (on)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, accent_dim());
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, accent_dim());
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, accent_dim());
            }
            if (sk.button(is_before ? "Before##compare" : "After##compare", {}, ImVec2(cell, 0.0f)))
            {
                reg.set_compare(is_before);
            }
            if (on)
            {
                ImGui::PopStyleColor(3);
                const ImVec2 a = ImGui::GetItemRectMin();
                const ImVec2 b = ImGui::GetItemRectMax();
                dl->AddRectFilled(a, ImVec2(b.x, a.y + std::max(1.0f, std::floor(dpi_px(2.0f)))), u32(accent()));
            }
            tooltip(held && !is_before ? (tip + "\n(" + key + " is held down right now.)").c_str() : tip.c_str());
        }
        ImGui::PopStyleVar();
    }

    void PanelView::draw_character_chooser()
    {
        if (!m_targets)
        {
            return;
        }
        Skin& sk = skin();
        const ImGuiStyle& style = ImGui::GetStyle();
        const float base = style.FontSizeBase;
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x - dpi_px(40.0f), vp->WorkSize.y - dpi_px(56.0f)), ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(dpi_px(20.0f), dpi_px(18.0f)));
        const bool open = ImGui::BeginPopupModal("##choose", nullptr,
                                                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
        ImGui::PopStyleVar();
        m_target_combo_open = open; // keeps the list from reordering under the mouse
        if (!open)
        {
            m_choose_close = false;
            return;
        }
        // Header: title + subtitle, Refresh and X on the right.
        const float close_w = sk.button_width("X");
        const float refresh_w = sk.button_width("Refresh");
        const ImVec2 top = ImGui::GetCursorPos();
        ImGui::PushFont(nullptr, base * 1.4f);
        ImGui::TextUnformatted("Choose character");
        ImGui::PopFont();
        ImGui::PushFont(nullptr, base * 0.86f);
        ImGui::TextColored(muted(), "Every character loaded right now, nearest first. Each one keeps its own sliders.");
        ImGui::PopFont();
        const ImVec2 after = ImGui::GetCursorPos();
        const float right_x = ImGui::GetCursorPos().x + ImGui::GetContentRegionAvail().x; // GetWindowContentRegionMax() is gone under IMGUI_DISABLE_OBSOLETE_FUNCTIONS
        ImGui::SetCursorPos(ImVec2(right_x - close_w - refresh_w - style.ItemSpacing.x, top.y));
        if (sk.button("Refresh"))
        {
            m_targets->refresh_targets();
            m_targets->request_rescan();
            m_target_combo_since = Clock::now(); // take the fresh list
            set_message("Looking for characters...");
        }
        {
            const std::string key = m_targets->refresh_hotkey_text();
            const std::string tip = "Search the whole level again, rescan the meshes and retry the pose hook." +
                                    (key.empty() ? std::string() : "\nHotkey: " + key + " (works in game too).");
            tooltip(tip.c_str());
        }
        ImGui::SameLine();
        if (sk.button("X") || ImGui::IsKeyPressed(ImGuiKey_Escape) || m_choose_close)
        {
            ImGui::CloseCurrentPopup();
        }
        m_choose_close = false;
        ImGui::SetCursorPos(after);
        ImGui::Dummy(ImVec2(0.0f, dpi_px(4.0f)));
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0.0f, dpi_px(8.0f)));

        const float footer = ImGui::GetTextLineHeightWithSpacing() * 3.0f;
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
        ImGui::BeginChild("##grid", ImVec2(0, -footer), ImGuiChildFlags_None);
        ImGui::PopStyleColor();
        const float gap = dpi_px(10.0f);
        const float avail = ImGui::GetContentRegionAvail().x;
        const int cols = std::max(2, static_cast<int>((avail + gap) / (dpi_px(124.0f) + gap)));
        const float tile_w = std::floor((avail - gap * static_cast<float>(cols - 1)) / static_cast<float>(cols));
        const float small = base * style.FontScaleDpi * 0.8f;
        const float foot_h = std::floor(ImGui::GetFontSize() + small + dpi_px(14.0f));
        const float tile_h = tile_w + foot_h;

        int index = 0;
        std::string picked_id;
        std::string picked_name;
        auto tile = [&](const std::string& id, const std::string& name, const std::string& sub, const std::string& letters, bool selected, bool edited,
                        ImTextureData* face) {
            ImGui::PushID(id.c_str());
            if (index % cols != 0)
            {
                ImGui::SameLine(0.0f, gap);
            }
            const ImVec2 a = ImGui::GetCursorScreenPos();
            if (ImGui::InvisibleButton("##tile", ImVec2(tile_w, tile_h)))
            {
                picked_id = id;
                picked_name = name;
            }
            const bool hovered = ImGui::IsItemHovered();
            if (!sub.empty() || name.size() > 18)
            {
                const std::string tip = sub.empty() ? name : name + "\n" + sub;
                tooltip(tip.c_str());
            }
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 b(a.x + tile_w, a.y + tile_h);
            const ImVec2 img_b(b.x, a.y + tile_w);
            const ImU32 border = u32(selected ? accent() : hovered ? accent_dim() : sk.color(Role::Border));
            monogram_frame(dl, a, img_b, letters, base * style.FontScaleDpi * 1.6f,
                           u32(hovered ? ImGui::GetStyleColorVec4(ImGuiCol_FrameBgHovered) : sk.color(Role::Section)), border,
                           u32(ImVec4(sk.color(Role::Title).x, sk.color(Role::Title).y, sk.color(Role::Title).z, 0.85f)));
            if (face)
            {
                Portraits::draw(dl, face, ImVec2(a.x + 1.0f, a.y + 1.0f), ImVec2(img_b.x - 1.0f, img_b.y));
                dl->AddRect(a, img_b, border, 0.0f, 0, 1.0f);
            }
            dl->AddRectFilled(ImVec2(a.x, img_b.y), b, u32(selected ? accent_dim() : ImGui::GetStyleColorVec4(ImGuiCol_FrameBg)));
            dl->AddRect(a, b, border, 0.0f, 0, 1.0f);
            // Footer: (bullet) name, then the engine name / id.
            const float tx = a.x + dpi_px(8.0f);
            float nx = tx;
            const float ny = img_b.y + dpi_px(6.0f);
            if (edited)
            {
                const float bs = std::max(3.0f, std::floor(ImGui::GetFontSize() * 0.28f));
                square_bullet(dl, ImVec2(tx + bs * 0.5f, ny + ImGui::GetFontSize() * 0.55f), bs, u32(ImGui::GetStyleColorVec4(ImGuiCol_Text)));
                nx += bs + dpi_px(5.0f);
            }
            dl->PushClipRect(ImVec2(a.x, img_b.y), ImVec2(b.x - dpi_px(4.0f), b.y), true);
            dl->AddText(ImVec2(nx, ny), u32(ImGui::GetStyleColorVec4(ImGuiCol_Text)), name.c_str());
            dl->AddText(ImGui::GetFont(), small, ImVec2(tx, ny + ImGui::GetFontSize() + dpi_px(1.0f)), u32(muted()), sub.c_str());
            dl->PopClipRect();
            ImGui::PopID();
            ++index;
        };

        tile("auto", "Automatic", "the character you control", "AUTO", m_target.id == "auto", is_edited(kPlayerActor), nullptr);
        for (const TargetChoice& t : m_target_list)
        {
            const std::string identity = identity_of(t.key);
            const std::string name = CharacterNames::instance().shown(t.label, t.key, identity);
            const std::string sub = !t.engine.empty() ? t.engine : !identity.empty() ? identity : std::string();
            ImTextureData* face = portrait_or_request(t.id, t.key, identity, t.label, t.engine);
            tile(t.id, name, sub, face ? std::string() : monogram(name), t.id == m_target.id, is_edited(t.key), face);
        }
        ImGui::EndChild();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::PushFont(nullptr, base * 0.86f);
        ImGui::TextColored(muted(), "A square marks a character with edited sliders. Characters without a portrait get a name tile; "
                                    "put PNG/JPG portraits named after the character in the presets folder's _portraits folder. "
                                    "Refresh searches the whole level.");
        ImGui::PopFont();
        ImGui::PopTextWrapPos();
        if (!picked_id.empty())
        {
            m_targets->pick_target(picked_id);
            set_message(picked_id == "auto" ? std::string("Switching to the default character") : "Switching to " + picked_name);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    void PanelView::draw_rename_popup()
    {
        Skin& sk = skin();
        if (!ImGui::BeginPopupModal("Rename character", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            return;
        }
        ImGui::TextColored(muted(), "Game name: %s", m_rename_label.c_str());
        if (!m_target.engine.empty())
        {
            ImGui::TextColored(muted(), "Engine name: %s", m_target.engine.c_str());
        }
        if (ImGui::IsWindowAppearing())
        {
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 18.0f);
        const bool enter = ImGui::InputTextWithHint("##rename", "Your name for this character", m_rename_buf, sizeof(m_rename_buf),
                                                    ImGuiInputTextFlags_EnterReturnsTrue);
        if (!m_rename_stable)
        {
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 18.0f);
            ImGui::TextColored(warn(), "This character has no stable name to recognise it by after a reload, so this name lasts for this visit only.");
            ImGui::PopTextWrapPos();
        }
        auto apply = [&](const std::string& name) {
            const std::string clean = CharacterNames::clean(name);
            const bool saved = CharacterNames::instance().set(m_rename_key, clean, m_rename_stable);
            if (!saved)
            {
                set_message("Could not save the name (kept for this session)");
            }
            else
            {
                set_message(clean.empty() ? "Using the game's name again" : "Renamed to " + clean);
            }
            ImGui::CloseCurrentPopup();
        };
        if (sk.button("Save", "save") || enter)
        {
            apply(m_rename_buf);
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(CharacterNames::instance().get(m_rename_key).empty());
        if (sk.button("Use game name", "reset"))
        {
            apply("");
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!m_targets);
        if (sk.button("Retake portrait"))
        {
            const std::string identity = identity_of(m_target.key);
            const std::string stem = portrait_stem(m_target.key, identity, m_target.label, m_target.engine);
            if (!stem.empty())
            {
                m_targets->request_portrait(m_target.id, stem, true);
                set_message("Taking a new portrait...");
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        tooltip("Takes a new picture of this character in the game (saved in the presets folder's _portraits folder).\n"
                "A PNG/JPG of your own named after the character always wins over taken pictures.");
        ImGui::SameLine();
        if (sk.button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    bool PanelView::is_edited(const std::string& key) const
    {
        return std::any_of(m_edited.begin(), m_edited.end(), [&](const ActorSummary& a) { return a.key == key; });
    }

    // Status tab: every character with sliders, with Select / Release.
    void PanelView::draw_edited_characters()
    {
        Skin& sk = skin();
        section_bar("Edited characters");
        if (!m_remembered.empty())
        {
            std::string list;
            for (const std::string& id : m_remembered)
            {
                list += (list.empty() ? "" : ", ") + id;
            }
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(muted(), "Remembered NPCs (%zu): %s", m_remembered.size(), list.c_str());
            ImGui::PopTextWrapPos();
            tooltip("Their sliders come back automatically when they are loaded within CharacterRange of the player.\n"
                    "Files: _characters folder inside the presets folder.");
        }
        if (m_edited.empty())
        {
            ImGui::TextColored(muted(), "None yet. A character gets its own sliders once you pick it and move one.");
            return;
        }
        std::string release_key;
        if (ImGui::BeginTable("##edited", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableSetupColumn("Character", ImGuiTableColumnFlags_WidthStretch, 3.0f);
            ImGui::TableSetupColumn("Bones", ImGuiTableColumnFlags_WidthStretch, 0.8f);
            ImGui::TableSetupColumn("Morphs", ImGuiTableColumnFlags_WidthStretch, 0.8f);
            ImGui::TableSetupColumn("##actions", ImGuiTableColumnFlags_WidthFixed,
                                    sk.button_width("Select") + sk.button_width("Release", "reset") + ImGui::GetStyle().ItemSpacing.x);
            ImGui::TableHeadersRow();
            for (const ActorSummary& a : m_edited)
            {
                ImGui::PushID(a.key.c_str());
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                const std::string name = CharacterNames::instance().shown(a.label, a.key, a.identity);
                if (a.active)
                {
                    ImGui::TextColored(accent(), "%s (selected)", name.c_str());
                }
                else
                {
                    ImGui::TextUnformatted(name.c_str());
                }
                if (name != a.label)
                {
                    const std::string tip = "Game name: " + a.label;
                    tooltip(tip.c_str());
                }
                if (a.key == kPlayerActor)
                {
                    tooltip("The player's sliders are saved with the session (_last_session).");
                }
                else if (!a.identity.empty())
                {
                    ImGui::SameLine();
                    ImGui::TextColored(muted(), "- remembered");
                    const std::string tip = "Remembered as \"" + a.identity + "\": these sliders come back by themselves whenever this character\n"
                                            "is loaded near the player (after a reload, in a new area...). Release forgets it.";
                    tooltip(tip.c_str());
                }
                else
                {
                    ImGui::SameLine();
                    ImGui::TextColored(warn(), "- this visit only");
                    tooltip("This character has no stable name to recognise it by after a reload, so its sliders last until it despawns.\n"
                            "Save a preset to reuse them.");
                }
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::Text("%zu", a.bones);
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::Text("%zu", a.morphs);
                ImGui::TableNextColumn();
                ImGui::BeginDisabled(a.active || !m_targets);
                if (sk.button("Select"))
                {
                    m_targets->pick_target(a.pick_id);
                    set_message("Switching to " + name);
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                if (sk.button("Release", "reset"))
                {
                    release_key = a.key;
                }
                tooltip("Drop this character's sliders. Its body goes back to normal, and it is left alone from now on\n"
                        "(unless you pick it and edit it again).");
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        if (!release_key.empty())
        {
            Registry::instance().forget_actor(release_key);
            m_edited = Registry::instance().edited_actors();
            set_message("Sliders released");
        }
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(muted(), "The player's sliders are saved with the session. NPCs are remembered by name and get their sliders "
                                    "back whenever they are loaded near the player; Release or Reset all forgets them.");
        ImGui::PopTextWrapPos();
    }

    std::vector<PanelView::Category> PanelView::categories() const
    {
        std::vector<Category> out;
        auto slot = [&out](const std::string& name) -> Category& {
            for (Category& c : out)
            {
                if (c.name == name)
                {
                    return c;
                }
            }
            out.push_back(Category{name});
            return out.back();
        };
        for (const GroupBones& gb : m_body.groups)
        {
            if (group_hidden(gb))
            {
                continue; // a named morph target does this part's job (draw_category)
            }
            Category& c = slot(gb.group.section);
            c.parts++;
            c.changed += scale_of(gb.bones.front(), gb.group.spread).has_scale() ? 1 : 0;
        }
        for (const MorphGroupMorphs& mg : m_morph_groups)
        {
            Category& c = slot(mg.group.section.empty() ? std::string("Morphs") : mg.group.section);
            c.parts++;
            const bool set = std::any_of(mg.morphs.begin(), mg.morphs.end(), [this](const std::string& m) { return morph_of(m).has_value(); });
            c.changed += set ? 1 : 0;
        }
        for (const ScaleSlider* sl : m_body_sliders)
        {
            if (slider_hidden(*sl))
            {
                continue;
            }
            Category& c = slot(sl->section);
            c.parts++;
            c.changed += std::fabs(scale_value(m_book, sl->id) - sl->neutral()) >= kScaleNeutralEps ? 1 : 0;
        }
        // Head & Neck, Face, Torso, Chest, Hips & Legs, Arms, ... Groin, Other (morph_sort.hpp).
        std::stable_sort(out.begin(), out.end(), [](const Category& a, const Category& b) { return section_rank(a.name) < section_rank(b.name); });
        return out;
    }

    bool PanelView::section_bar(const char* title, const char* button, bool button_enabled, const char* icon_key)
    {
        Skin& sk = skin();
        const ImGuiStyle& style = ImGui::GetStyle();
        const float pad_x = dpi_px(12.0f);
        const float h = ImGui::GetFrameHeight() + dpi_px(12.0f);
        const float w = ImGui::GetContentRegionAvail().x;
        const ImVec2 a = ImGui::GetCursorScreenPos();
        const ImVec2 b(a.x + w, a.y + h);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(a, b, u32(sk.color(Role::Section)));
        dl->AddRect(a, b, u32(sk.color(Role::Border)), 0.0f, 0, 1.0f);
        const float size = style.FontSizeBase * style.FontScaleDpi * 1.22f;
        const ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0.0f, title);
        dl->AddText(ImGui::GetFont(), size, ImVec2(a.x + pad_x, std::floor(a.y + (h - ts.y) * 0.5f)), u32(ImGui::GetStyleColorVec4(ImGuiCol_Text)), title);
        ImGui::Dummy(ImVec2(w, h));
        const ImVec2 after = ImGui::GetCursorScreenPos();
        bool pressed = false;
        if (button)
        {
            const float bw = sk.button_width(button, icon_key);
            ImGui::SetCursorScreenPos(ImVec2(b.x - bw - dpi_px(6.0f), a.y + (h - ImGui::GetFrameHeight()) * 0.5f));
            ImGui::PushID(title);
            ImGui::BeginDisabled(!button_enabled);
            pressed = sk.button(button, icon_key);
            ImGui::EndDisabled();
            ImGui::PopID();
            ImGui::SetCursorScreenPos(after);
        }
        return pressed;
    }

    void PanelView::draw_category_rail(const std::vector<Category>& cats, float width)
    {
        draw_rail("##categories", "BODY", cats, width, m_category,
                  "Sliders belong to the character: the player's are saved with the session, NPCs are remembered by name. "
                  "Save a preset to reuse a shape.");
    }

    void PanelView::draw_rail(const char* id, const char* heading, const std::vector<Category>& cats, float width, std::string& picked_name,
                              const char* note)
    {
        Skin& sk = skin();
        const ImGuiStyle& style = ImGui::GetStyle();
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
        ImGui::BeginChild(id, ImVec2(width, 0), ImGuiChildFlags_None);
        ImGui::PopStyleColor();
        ImGui::PushFont(nullptr, style.FontSizeBase * 0.78f);
        ImGui::TextColored(muted(), "%s", heading);
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(0.0f, dpi_px(2.0f)));
        const float row_h = std::floor(ImGui::GetFrameHeight() * 1.25f);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(style.ItemSpacing.x, dpi_px(3.0f)));
        const float small = style.FontSizeBase * style.FontScaleDpi * 0.8f;
        for (const Category& c : cats)
        {
            ImGui::PushID(c.name.c_str());
            const bool picked = c.name == picked_name;
            const ImVec2 a = ImGui::GetCursorScreenPos();
            const float w = ImGui::GetContentRegionAvail().x;
            if (ImGui::InvisibleButton("##cat", ImVec2(w, row_h)))
            {
                picked_name = c.name;
            }
            const bool hovered = ImGui::IsItemHovered();
            const ImVec2 b(a.x + w, a.y + row_h);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            if (picked)
            {
                dl->AddRectFilled(a, b, u32(accent_dim()));
                dl->AddRect(a, b, u32(accent()), 0.0f, 0, 1.0f);
            }
            else if (hovered)
            {
                dl->AddRectFilled(a, b, u32(ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered)));
            }
            float x = a.x + dpi_px(12.0f);
            const float cy = a.y + row_h * 0.5f;
            if (sk.has_icon("section." + c.name))
            {
                const float ic = ImGui::GetFontSize();
                sk.draw_image(dl, "icon." + fold_case("section." + c.name), ImVec2(x, cy - ic * 0.5f), ImVec2(x + ic, cy + ic * 0.5f));
                x += ic + dpi_px(8.0f);
            }
            else
            {
                const float bs = std::max(3.0f, std::floor(ImGui::GetFontSize() * 0.26f));
                square_bullet(dl, ImVec2(x + bs * 0.5f, cy), bs, u32(picked ? ImGui::GetStyleColorVec4(ImGuiCol_Text) : c.changed ? accent() : muted()));
                x += bs + dpi_px(9.0f);
            }
            dl->AddText(ImVec2(x, std::floor(cy - ImGui::GetFontSize() * 0.5f)), u32(ImGui::GetStyleColorVec4(ImGuiCol_Text)), c.name.c_str());
            if (c.changed > 0)
            {
                char n[32];
                std::snprintf(n, sizeof(n), "%zu changed", c.changed);
                const ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(small, FLT_MAX, 0.0f, n);
                dl->AddText(ImGui::GetFont(), small, ImVec2(b.x - ts.x - dpi_px(10.0f), std::floor(cy - ts.y * 0.5f)),
                            u32(picked ? ImGui::GetStyleColorVec4(ImGuiCol_Text) : muted()), n);
            }
            std::string tip = std::to_string(c.parts) + (c.parts == 1 ? " part" : " parts");
            tooltip(tip.c_str());
            ImGui::PopID();
        }
        ImGui::PopStyleVar();
        ImGui::Dummy(ImVec2(0.0f, dpi_px(10.0f)));
        ImGui::PushFont(nullptr, style.FontSizeBase * 0.86f);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(muted(), "%s", note);
        ImGui::PopTextWrapPos();
        ImGui::PopFont();
        ImGui::EndChild();
        // Divider between the rail and the sliders.
        const ImVec2 r = ImGui::GetItemRectMax();
        const float x = r.x + ImGui::GetStyle().ItemSpacing.x;
        ImGui::GetWindowDrawList()->AddLine(ImVec2(x, ImGui::GetItemRectMin().y), ImVec2(x, r.y), u32(sk.color(Role::Border)), 1.0f);
    }

    bool PanelView::hairline_slider(const char* id, float* v, float lo, float hi, bool log, float neutral)
    {
        Skin& sk = skin();
        if (sk.has_image("slider_track") || sk.has_image("slider_fill") || sk.has_image("slider_grab"))
        {
            return sk.slider_float(id, v, lo, hi, "", log ? ImGuiSliderFlags_Logarithmic : ImGuiSliderFlags_None); // the skin's own slider
        }
        const float w = std::max(dpi_px(40.0f), ImGui::CalcItemWidth());
        const float h = ImGui::GetFrameHeight();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton(id, ImVec2(w, h));
        const bool hovered = ImGui::IsItemHovered();
        const bool active = ImGui::IsItemActive();
        const float tw = std::max(3.0f, std::floor(dpi_px(6.0f)));
        const float th = std::floor(std::min(h, dpi_px(16.0f)));
        const float x0 = p.x + tw * 0.5f, x1 = p.x + w - tw * 0.5f;
        auto to_t = [&](float val) {
            val = std::clamp(val, lo, hi);
            if (log && lo > 0.0f)
            {
                return std::log(val / lo) / std::log(hi / lo);
            }
            return hi != lo ? (val - lo) / (hi - lo) : 0.0f;
        };
        bool changed = false;
        if (active && ImGui::IsMouseDown(ImGuiMouseButton_Left) && x1 > x0)
        {
            const float t = std::clamp((ImGui::GetIO().MousePos.x - x0) / (x1 - x0), 0.0f, 1.0f);
            const float nv = (log && lo > 0.0f) ? lo * std::pow(hi / lo, t) : lo + (hi - lo) * t;
            if (nv != *v)
            {
                *v = nv;
                changed = true;
            }
        }
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float cy = std::floor(p.y + h * 0.5f);
        const float line = std::max(1.0f, std::floor(dpi_px(2.0f)));
        dl->AddRectFilled(ImVec2(x0, cy - std::floor(line * 0.5f)), ImVec2(x1, cy - std::floor(line * 0.5f) + line), u32(sk.color(Role::Track)));
        const float nx = std::floor(x0 + (x1 - x0) * to_t(neutral));
        const float tick = std::floor(dpi_px(5.0f));
        dl->AddRectFilled(ImVec2(nx, cy - tick), ImVec2(nx + std::max(1.0f, std::floor(dpi_px(1.0f))), cy + tick), u32(muted()));
        const float tx = std::floor(x0 + (x1 - x0) * to_t(*v));
        const ImGuiCol grab = active || hovered ? ImGuiCol_SliderGrabActive : ImGuiCol_SliderGrab;
        dl->AddRectFilled(ImVec2(tx - std::floor(tw * 0.5f), cy - std::floor(th * 0.5f)), ImVec2(tx - std::floor(tw * 0.5f) + tw, cy - std::floor(th * 0.5f) + th),
                          ImGui::GetColorU32(grab));
        return changed;
    }

    PanelView::RowResult PanelView::part_row(const RowSpec& spec, float& v)
    {
        Skin& sk = skin();
        const ImGuiStyle& style = ImGui::GetStyle();
        RowResult r;
        ImGui::PushID(spec.id);
        // Label with a square bullet (accent when the part is changed).
        ImGui::AlignTextToFramePadding();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float bs = std::max(3.0f, std::floor(ImGui::GetFontSize() * 0.26f));
        square_bullet(ImGui::GetWindowDrawList(), ImVec2(p.x + bs * 0.5f, p.y + ImGui::GetFrameHeight() * 0.5f), bs,
                      u32(spec.edited ? accent() : ImGui::GetStyleColorVec4(ImGuiCol_Text)));
        ImGui::SetCursorScreenPos(ImVec2(p.x + bs + dpi_px(9.0f), p.y));
        ImGui::AlignTextToFramePadding();
        // Long names (two morphs with the same label get their name added) are cut with "..."
        // so they never run into the track; the tooltip has the whole name.
        std::string shown = spec.title;
        const float room = spec.label_w - bs - dpi_px(9.0f) - style.ItemSpacing.x;
        if (spec.label_w > 0.0f && ImGui::CalcTextSize(shown.c_str()).x > room)
        {
            while (!shown.empty() && ImGui::CalcTextSize((shown + "...").c_str()).x > room)
            {
                shown.pop_back();
            }
            shown += "...";
        }
        ImGui::TextColored(spec.edited ? accent() : ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s", shown.c_str());
        tooltip(shown == spec.title ? spec.tip.c_str() : (spec.title + "\n" + spec.tip).c_str());

        const float box_w = std::floor(ImGui::CalcTextSize("0.000").x + style.FramePadding.x * 2.0f);
        const float xyz_w = spec.has_axes || spec.reserve_axes ? compact_button_width("XYZ", "xyz") + style.ItemSpacing.x : 0.0f;
        const float reset_w = compact_button_width("Reset", "reset");
        ImGui::SameLine(p.x - ImGui::GetWindowPos().x + ImGui::GetScrollX() + spec.label_w);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - box_w - xyz_w - reset_w - style.ItemSpacing.x * 2.0f - dpi_px(6.0f));
        if (hairline_slider("##track", &v, spec.lo, spec.hi, spec.log, spec.neutral))
        {
            r.changed = true;
        }
        if (wheel_nudge(v, spec.log ? 0.01f : 0.05f, spec.log ? 0.001f : 0.01f, spec.lo, spec.hi))
        {
            r.changed = true;
        }
        tooltip("Drag to change. Ctrl+wheel nudges (add Shift for finer steps).");

        // Number box: click and type.
        ImGui::SameLine(0.0f, style.ItemSpacing.x + dpi_px(6.0f));
        ImGui::SetNextItemWidth(box_w);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Border, sk.color(Role::Border));
        if (spec.unset && !ImGui::IsItemActive())
        {
            char buf[32] = "";
            if (ImGui::InputTextWithHint("##box", spec.unset_hint, buf, sizeof(buf), ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_AutoSelectAll))
            {
                char* end = nullptr;
                const float typed = std::strtof(buf, &end);
                if (end != buf)
                {
                    v = typed;
                    r.changed = true;
                }
            }
        }
        else
        {
            float typed = v;
            if (ImGui::InputFloat("##box", &typed, 0.0f, 0.0f, spec.format, ImGuiInputTextFlags_AutoSelectAll) && std::isfinite(typed))
            {
                v = spec.log ? std::max(typed, spec.lo * 0.5f) : typed;
                r.changed = true;
            }
        }
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        tooltip("Click to type a value.");
        if (spec.has_axes)
        {
            ImGui::SameLine();
            if (spec.axes_open)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, accent_dim());
            }
            r.axes = compact_button("XYZ", "xyz");
            if (spec.axes_open)
            {
                ImGui::PopStyleColor();
            }
            tooltip("Edit length / width / depth separately");
        }
        else if (spec.reserve_axes)
        {
            // Keep every track in a category the same length, with or without an XYZ button.
            ImGui::SameLine();
            ImGui::Dummy(ImVec2(compact_button_width("XYZ", "xyz"), ImGui::GetFrameHeight()));
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!spec.can_reset);
        r.reset = compact_button("Reset", "reset");
        ImGui::EndDisabled();
        tooltip("Back to the game's own shape");
        ImGui::PopID();
        return r;
    }

    namespace
    {
        // Size slider semantics: a uniform part stays uniform, a stretched one keeps its proportions.
        void set_size(BoneEdit& s, double v)
        {
            const double avg = (s.axis[0] + s.axis[1] + s.axis[2]) / 3.0;
            v = std::clamp(v, 0.01, 100.0);
            const bool uniform = std::fabs(s.axis[0] - s.axis[1]) < 1e-6 && std::fabs(s.axis[1] - s.axis[2]) < 1e-6;
            if (uniform || avg <= 1e-6)
            {
                s.axis[0] = s.axis[1] = s.axis[2] = v;
            }
            else
            {
                for (double& a : s.axis)
                {
                    a *= v / avg;
                }
            }
        }
    } // namespace

    namespace
    {
        // Body tab label of a bone group: "Thighs" reads as "Thigh size", like a character creator.
        // Titles from game profiles (and anything not listed) are shown as they are.
        std::string friendly_group_title(const std::string& title)
        {
            static const std::map<std::string, const char*> names = {
                {"head", "Head size"},       {"horns", "Horn size"},       {"ears", "Ear size"},         {"eyes", "Eye size"},
                {"neck", "Neck size"},       {"rib cage", "Rib cage size"}, {"waist", "Waist size"},     {"belly", "Belly size"},
                {"hips", "Hip size"},        {"shoulders", "Shoulder size"}, {"breasts", "Breast size"}, {"nipples", "Nipple size"},
                {"butt", "Butt size"},       {"thighs", "Thigh size"},     {"calves", "Calf size"},      {"feet", "Foot size"},
                {"upper arms", "Upper arm size"}, {"forearms", "Forearm size"}, {"hands", "Hand size"},
            };
            const auto it = names.find(fold_case(title));
            return it != names.end() ? std::string(it->second) : title;
        }

        // Small muted caption between the size and the shape rows of a category.
        void row_caption(const char* text)
        {
            ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 0.80f);
            ImGui::TextColored(muted(), "%s", text);
            ImGui::PopFont();
        }
    } // namespace

    bool PanelView::group_hidden(const GroupBones& gb) const
    {
        // Replaced by a named morph target, unless the bones were already edited (a preset, or
        // before the morph showed up): then the row stays so the edit can be seen and reset.
        return m_covered_groups.count(gb.group.title) > 0 && !group_changed(gb, m_book);
    }

    bool PanelView::slider_hidden(const ScaleSlider& sl) const
    {
        return m_covered_sliders.count(sl.id) > 0 && std::fabs(scale_value(m_book, sl.id) - sl.neutral()) < kScaleNeutralEps;
    }

    void PanelView::refresh_body_sliders()
    {
        // Per slider: a clearly named morph target first, then the bone-built slider; morphs with
        // names nothing recognises stay in Other (last).
        m_covered_groups.clear();
        for (const GroupBones& gb : m_body.groups)
        {
            if (!gb.custom && group_covered(gb.group.title, m_morph_names))
            {
                m_covered_groups.insert(gb.group.title);
            }
        }
        m_body_sliders.clear();
        m_covered_sliders.clear();
        for (const ScaleSlider& sl : scale_sliders())
        {
            if (sl.body && !scale_slider_bones(sl, m_body).empty())
            {
                m_body_sliders.push_back(&sl);
                if (scale_slider_covered(sl, m_morph_names))
                {
                    m_covered_sliders.insert(sl.id);
                }
            }
        }
    }

    void PanelView::draw_category(const Category& cat)
    {
        Skin& sk = skin();
        const ImGuiStyle& style = ImGui::GetStyle();
        const bool reset_all = section_bar(cat.name.c_str(), "Reset", cat.changed > 0);
        ImGui::PushFont(nullptr, style.FontSizeBase * 0.86f);
        draw_changed_only_filter();
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(muted(), cat.changed ? "%zu of %zu sliders changed. Click a number to type it."
                           : cat.parts == 1 ? "%zu slider. Click a number to type it." : "%zu sliders. Click a number to type it.",
                           cat.changed ? cat.changed : cat.parts, cat.parts);
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(0.0f, dpi_px(4.0f)));

        auto morph_section = [](const MorphGroupMorphs& mg) { return mg.group.section.empty() ? std::string("Morphs") : mg.group.section; };

        // What this category holds: bone sizes, bone-built shapes, morph shapes.
        size_t n_groups = 0, n_shapes = 0;
        float label_w = ImGui::CalcTextSize("Depth").x;
        for (const GroupBones& gb : m_body.groups)
        {
            if (gb.group.section == cat.name && !group_hidden(gb))
            {
                ++n_groups;
                label_w = std::max(label_w, ImGui::CalcTextSize(friendly_group_title(gb.group.title).c_str()).x);
            }
        }
        for (const ScaleSlider* sl : m_body_sliders)
        {
            if (cat.name == sl->section && !slider_hidden(*sl))
            {
                ++n_shapes;
                label_w = std::max(label_w, ImGui::CalcTextSize(sl->title).x);
            }
        }
        for (const MorphGroupMorphs& mg : m_morph_groups)
        {
            if (morph_section(mg) == cat.name)
            {
                ++n_shapes;
                label_w = std::max(label_w, ImGui::CalcTextSize(mg.group.title.c_str()).x);
            }
        }
        label_w = std::min(label_w + ImGui::GetFontSize() * 1.6f, ImGui::GetContentRegionAvail().x * 0.34f);
        const bool captions = n_groups > 0 && n_shapes > 0 && !m_simplified_changed_only;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(dpi_px(16.0f), dpi_px(14.0f)));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(style.ItemSpacing.x, dpi_px(12.0f)));
        sk.begin_child("##group", ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
        ImGui::PopStyleVar();
        size_t shown = 0; // rows left after the "Changed only" filter

        if (captions)
        {
            row_caption("SIZE");
        }
        for (const GroupBones& gb : m_body.groups)
        {
            const BodyGroup& g = gb.group;
            if (g.section != cat.name || group_hidden(gb) || (m_simplified_changed_only && !group_changed(gb, m_book)))
            {
                continue;
            }
            ++shown;
            ImGui::PushID(g.title.c_str());
            BoneEdit s = scale_of(gb.bones.front(), g.spread);
            const double one[3] = {1.0, 1.0, 1.0};
            if (reset_all && s.has_scale())
            {
                write_group(gb, one);
                s = scale_of(gb.bones.front(), g.spread);
            }
            // Friendly tooltip; the bones themselves are listed under Advanced > Bones.
            std::string tip = g.hint;
            tip += tip.empty() ? "" : "\n";
            tip += gb.bones.size() == 1 ? "Drives 1 bone (see Advanced > Bones)." : "Drives " + std::to_string(gb.bones.size()) + " bones (see Advanced > Bones).";
            bool& open = m_group_open[g.title];
            RowSpec spec;
            spec.id = "size";
            spec.title = friendly_group_title(g.title);
            spec.tip = tip;
            spec.edited = s.has_scale();
            spec.lo = kScaleMin;
            spec.hi = kScaleMax;
            spec.log = true;
            spec.neutral = 1.0f;
            spec.has_axes = true;
            spec.axes_open = open;
            spec.can_reset = s.has_scale();
            spec.label_w = label_w;
            float v = static_cast<float>((s.axis[0] + s.axis[1] + s.axis[2]) / 3.0);
            const RowResult r = part_row(spec, v);
            if (r.changed)
            {
                set_size(s, v);
                write_group(gb, s.axis);
            }
            if (r.axes)
            {
                open = !open;
            }
            if (r.reset)
            {
                write_group(gb, one);
                s = scale_of(gb.bones.front(), g.spread);
            }
            if (open)
            {
                static const char* const names[3] = {"Length", "Width", "Depth"};
                ImGui::Indent(ImGui::GetFontSize());
                for (int a = 0; a < 3; ++a)
                {
                    RowSpec sub = spec;
                    sub.id = names[a];
                    sub.title = names[a];
                    sub.tip = a == 0 ? "Along the bone (X)" : a == 1 ? "Sideways (Y)" : "Front to back (Z)";
                    sub.edited = std::fabs(s.axis[a] - 1.0) > 1e-6;
                    sub.has_axes = false;
                    sub.reserve_axes = true;
                    sub.can_reset = sub.edited;
                    sub.label_w = label_w - ImGui::GetFontSize();
                    float av = static_cast<float>(s.axis[a]);
                    const RowResult ar = part_row(sub, av);
                    if (ar.changed || ar.reset)
                    {
                        s.axis[a] = ar.reset ? 1.0 : std::clamp(static_cast<double>(av), 0.01, 100.0);
                        write_group(gb, s.axis);
                    }
                }
                ImGui::Unindent(ImGui::GetFontSize());
            }
            ImGui::PopID();
        }

        if (captions)
        {
            row_caption("SHAPE");
        }
        // Bone-built sliders (Penis length, Breast cleavage...): only on meshes with the bones and
        // no named morph target for it (refresh_body_sliders, slider_hidden).
        for (const ScaleSlider* sl : m_body_sliders)
        {
            const double value = scale_value(m_book, sl->id);
            const bool edited = std::fabs(value - sl->neutral()) >= kScaleNeutralEps;
            if (cat.name != sl->section || slider_hidden(*sl) || (m_simplified_changed_only && !edited))
            {
                continue;
            }
            ++shown;
            const double neutral = sl->neutral();
            if (reset_all && edited)
            {
                write_scale(*sl, neutral);
            }
            const size_t nb = scale_slider_bones(*sl, m_body).size();
            RowSpec spec;
            spec.id = sl->id;
            spec.title = sl->title;
            spec.tip = std::string(sl->hint) + "\n" +
                       (nb == 1 ? std::string("Drives 1 bone (see Advanced > Bones).") : "Drives " + std::to_string(nb) + " bones (see Advanced > Bones).");
            spec.edited = edited && !reset_all;
            spec.lo = static_cast<float>(sl->lo);
            spec.hi = static_cast<float>(sl->hi);
            spec.log = !sl->offset();
            spec.neutral = static_cast<float>(neutral);
            spec.can_reset = edited && !reset_all;
            spec.reserve_axes = n_groups > 0;
            spec.label_w = label_w;
            float v = reset_all ? static_cast<float>(neutral) : static_cast<float>(value);
            const RowResult r = part_row(spec, v);
            if (r.reset)
            {
                write_scale(*sl, neutral);
            }
            else if (r.changed)
            {
                write_scale(*sl, v);
            }
        }

        for (const MorphGroupMorphs& mg : m_morph_groups)
        {
            if (morph_section(mg) != cat.name || (m_simplified_changed_only && !morph_group_changed(mg, m_morphs)))
            {
                continue;
            }
            ++shown;
            ImGui::PushID(("morphgroup:" + mg.group.title).c_str());
            std::optional<double> current;
            for (const std::string& m : mg.morphs)
            {
                if (const auto w = morph_of(m))
                {
                    current = w;
                    break;
                }
            }
            bool reset = reset_all && current.has_value();
            std::string tip = mg.group.hint;
            tip += tip.empty() ? "" : "\n";
            tip += mg.morphs.size() == 1 ? "A morph target of this mesh (see Advanced > Morphs)."
                                         : "Drives " + std::to_string(mg.morphs.size()) + " morph targets (see Advanced > Morphs).";
            RowSpec spec;
            spec.id = "morph";
            spec.title = mg.group.title;
            spec.tip = tip + "\nEmpty box = the game's own value.";
            spec.edited = current.has_value();
            spec.lo = kMorphSliderMin;
            spec.hi = kMorphSliderMax;
            spec.neutral = 0.0f;
            spec.unset = !current.has_value();
            spec.can_reset = current.has_value();
            spec.reserve_axes = n_groups > 0;
            spec.label_w = label_w;
            float v = static_cast<float>(current.value_or(0.0));
            const RowResult r = part_row(spec, v);
            reset = reset || r.reset;
            if (r.changed && !reset)
            {
                for (const std::string& m : mg.morphs)
                {
                    write_morph(m, v); // clamped to kMorphMin..kMorphMax
                }
            }
            if (reset)
            {
                for (const std::string& m : mg.morphs)
                {
                    write_morph(m, std::nullopt);
                }
            }
            ImGui::PopID();
        }
        if (shown == 0)
        {
            ImGui::TextColored(muted(), "Nothing in %s is changed yet. Untick \"Changed only\" to see every slider.", cat.name.c_str());
        }
        sk.end_child();
        ImGui::PopStyleVar();
    }

    void PanelView::draw_simplified()
    {
        if (m_skeleton.names.empty())
        {
            ImGui::TextWrapped("The sliders appear once a character's skeletal mesh has been found. Load a save or enter a level, or pick a character above.");
            return;
        }
        const std::vector<Category> cats = categories();
        if (cats.empty())
        {
            ImGui::TextWrapped("This skeleton uses bone names the Body tab does not recognise. Every bone can still be edited under Advanced.");
            return;
        }
        if (std::none_of(cats.begin(), cats.end(), [this](const Category& c) { return c.name == m_category; }))
        {
            m_category = cats.front().name;
        }
        float rail_w = ImGui::CalcTextSize("Hips & Legs").x;
        for (const Category& c : cats)
        {
            rail_w = std::max(rail_w, ImGui::CalcTextSize(c.name.c_str()).x);
        }
        rail_w = std::min(rail_w + ImGui::GetFontSize() * 5.0f, ImGui::GetContentRegionAvail().x * 0.29f);

        draw_symmetry_tools(true);
        draw_category_rail(cats, rail_w);
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x * 2.0f + 1.0f);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
        ImGui::BeginChild("##parts", ImVec2(0, 0), ImGuiChildFlags_None);
        ImGui::PopStyleColor();
        for (const Category& c : cats)
        {
            if (c.name == m_category)
            {
                draw_category(c);
            }
        }
        if (m_morphs_advanced_only > 0 && (m_category == cats.back().name || m_morph_groups.empty()))
        {
            // Under the last category (usually "Other"): what the Body tab leaves out, and why.
            ImGui::Spacing();
            ImGui::PushTextWrapPos(0.0f);
            if (!m_morph_profile.auto_groups)
            {
                ImGui::TextColored(muted(), "%zu more morph target%s under Advanced > Morphs (\"AutoMorphGroups\" is off in the game profile).",
                                   m_morphs_advanced_only, m_morphs_advanced_only == 1 ? "" : "s");
            }
            else
            {
                ImGui::TextColored(muted(),
                                   "%zu more morph target%s under Advanced > Morphs: correctives and clothing helpers the game drives itself. "
                                   "\"MorphGroups\" in the game profile can bring them here.",
                                   m_morphs_advanced_only, m_morphs_advanced_only == 1 ? "" : "s");
            }
            ImGui::PopTextWrapPos();
        }
        ImGui::EndChild();
    }

    // ---- Character Scale tab ---------------------------------------------------------------
    // Height scales the whole pose from the mesh origin; the proportion sliders multiply the
    // bones of Body tab groups (proportions.hpp). Both sit on top of the Body and Advanced edits.

    bool PanelView::scale_available(const ScaleSlider& sl) const
    {
        if (sl.body)
        {
            return false; // a Body tab slider (universal morph)
        }
        if (sl.groups.empty())
        {
            return true;
        }
        for (const char* title : sl.groups)
        {
            const std::string want = fold_case(title);
            for (const GroupBones& gb : m_body.groups)
            {
                if (fold_case(gb.group.title) == want && !gb.bones.empty())
                {
                    return true;
                }
            }
        }
        return false;
    }

    void PanelView::write_scale(const ScaleSlider& sl, double value)
    {
        const EditEntry e = scale_entry(sl.id, value);
        write(e.bone, e.edit); // 1.0 is neutral and clears it
    }

    std::vector<PanelView::Category> PanelView::scale_categories() const
    {
        std::vector<Category> out;
        for (const ScaleSlider& sl : scale_sliders())
        {
            if (!scale_available(sl))
            {
                continue;
            }
            auto it = std::find_if(out.begin(), out.end(), [&](const Category& c) { return c.name == sl.section; });
            if (it == out.end())
            {
                out.push_back(Category{sl.section});
                it = out.end() - 1;
            }
            it->parts++;
            it->changed += std::fabs(scale_value(m_book, sl.id) - sl.neutral()) >= kScaleNeutralEps ? 1 : 0;
        }
        return out;
    }

    void PanelView::draw_scale()
    {
        if (m_skeleton.names.empty())
        {
            ImGui::TextWrapped("The sliders appear once a character's skeletal mesh has been found. Load a save or enter a level, or pick a character above.");
            return;
        }
        const std::vector<Category> cats = scale_categories();
        if (std::none_of(cats.begin(), cats.end(), [this](const Category& c) { return c.name == m_scale_section; }))
        {
            m_scale_section = cats.empty() ? std::string() : cats.front().name;
        }
        float rail_w = ImGui::CalcTextSize("Proportions").x;
        rail_w = std::min(rail_w + ImGui::GetFontSize() * 6.0f, ImGui::GetContentRegionAvail().x * 0.29f);
        draw_rail("##scale_sections", "CHARACTER SCALE", cats, rail_w, m_scale_section,
                  "Applied on top of the Body and Advanced sliders, per character: saved with the session, in presets and in the "
                  "character's memory. Height scales from the feet, so the character stays on the ground.");
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x * 2.0f + 1.0f);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
        ImGui::BeginChild("##scale_parts", ImVec2(0, 0), ImGuiChildFlags_None);
        ImGui::PopStyleColor();

        Skin& sk = skin();
        const ImGuiStyle& style = ImGui::GetStyle();
        const auto cat = std::find_if(cats.begin(), cats.end(), [this](const Category& c) { return c.name == m_scale_section; });
        if (cat != cats.end())
        {
            const bool reset_all = section_bar(cat->name.c_str(), "Reset", cat->changed > 0);
            ImGui::PushFont(nullptr, style.FontSizeBase * 0.86f);
            ImGui::PushTextWrapPos(0.0f);
            if (cat->name == "Overall")
            {
                ImGui::TextColored(muted(), "The whole character and every mesh it wears, scaled from the feet. Collision stays as it is; "
                                            "the Camera section moves the third-person camera with it.");
            }
            else if (cat->name == "Camera")
            {
                ImGui::TextColored(muted(), "The game's third-person camera while you play this character: its camera boom (spring arm) "
                                            "when it has one, else its camera or eye height. Saved per character like the other sliders.");
            }
            else if (cat->name == "Muscle")
            {
                ImGui::TextColored(muted(), "Thickness only: arms, legs, neck and chest get bulkier without getting longer, and the hands, feet and "
                                            "head keep their size. Muscle helper bones (biceps, quads...) grow along when the rig has them.");
            }
            else
            {
                ImGui::TextColored(muted(), "Lengths keep the hands, feet and head their own size. Hover a name to see its bones.");
            }
            ImGui::PopTextWrapPos();
            ImGui::PopFont();
            ImGui::Dummy(ImVec2(0.0f, dpi_px(4.0f)));

            float label_w = 0.0f;
            for (const ScaleSlider& sl : scale_sliders())
            {
                if (!sl.body)
                {
                    label_w = std::max(label_w, ImGui::CalcTextSize(sl.title).x);
                }
            }
            label_w = std::min(label_w + ImGui::GetFontSize() * 1.6f, ImGui::GetContentRegionAvail().x * 0.42f);

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(dpi_px(16.0f), dpi_px(14.0f)));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(style.ItemSpacing.x, dpi_px(12.0f)));
            sk.begin_child("##scale_group", ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
            ImGui::PopStyleVar();
            for (const ScaleSlider& sl : scale_sliders())
            {
                if (cat->name != sl.section || !scale_available(sl))
                {
                    continue;
                }
                const double value = scale_value(m_book, sl.id);
                const double neutral = sl.neutral();
                const bool edited = std::fabs(value - neutral) >= kScaleNeutralEps;
                if (reset_all && edited)
                {
                    write_scale(sl, neutral);
                }
                if (sl.axes == ScaleAxes::CameraFollow)
                {
                    bool on = !reset_all && value >= 0.5;
                    if (sk.checkbox(sl.title, &on))
                    {
                        write_scale(sl, on ? 1.0 : 0.0);
                    }
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip("%s\nHeight is %.0f%% now.", sl.hint, scale_value(m_book, "height") * 100.0);
                    }
                    continue;
                }
                std::string tip = sl.hint;
                if (!sl.groups.empty())
                {
                    tip += "\nBones:";
                    for (const std::string& b : scale_slider_bones(sl, m_body))
                    {
                        tip += " " + b;
                    }
                }
                RowSpec spec;
                spec.id = sl.id;
                spec.title = sl.title;
                spec.tip = tip;
                spec.edited = edited && !reset_all;
                spec.lo = static_cast<float>(sl.lo);
                spec.hi = static_cast<float>(sl.hi);
                spec.log = !sl.offset();
                spec.neutral = static_cast<float>(neutral);
                if (sl.axes == ScaleAxes::CameraLift)
                {
                    spec.format = "%+.0f cm";
                }
                spec.can_reset = edited && !reset_all;
                spec.label_w = label_w;
                float v = reset_all ? static_cast<float>(neutral) : static_cast<float>(value);
                const RowResult r = part_row(spec, v);
                if (r.reset)
                {
                    write_scale(sl, neutral);
                }
                else if (r.changed)
                {
                    write_scale(sl, v);
                }
            }
            sk.end_child();
            ImGui::PopStyleVar();
        }

        // Summary under the group, like the other tabs' footers.
        size_t hidden = 0;
        for (const ScaleSlider& sl : scale_sliders())
        {
            hidden += sl.body || scale_available(sl) ? 0 : 1;
        }
        ImGui::Dummy(ImVec2(0.0f, dpi_px(8.0f)));
        {
            const ImVec2 a = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddLine(a, ImVec2(a.x + ImGui::GetContentRegionAvail().x, a.y), u32(sk.color(Role::Border)), 1.0f);
            ImGui::Dummy(ImVec2(0.0f, dpi_px(8.0f)));
        }
        ImGui::PushFont(nullptr, style.FontSizeBase * 0.86f);
        ImGui::PushTextWrapPos(0.0f);
        const double height = scale_value(m_book, "height");
        const auto props = std::find_if(cats.begin(), cats.end(), [](const Category& c) { return c.name == "Proportions"; });
        const auto muscle = std::find_if(cats.begin(), cats.end(), [](const Category& c) { return c.name == "Muscle"; });
        ImGui::TextColored(muted(), "Height %.0f%%. %zu of %zu proportion and %zu of %zu muscle sliders changed.", height * 100.0,
                           props != cats.end() ? props->changed : 0, props != cats.end() ? props->parts : 0,
                           muscle != cats.end() ? muscle->changed : 0, muscle != cats.end() ? muscle->parts : 0);
        {
            const CameraWish cam = camera_wish(m_book);
            if (!cam.active())
            {
                ImGui::TextColored(muted(), "Camera: the game's own.");
            }
            else
            {
                ImGui::TextColored(muted(), "Camera: %s%+.0f cm, distance %.2fx.", cam.follow ? "follows Height, " : "", cam.lift, cam.distance);
            }
        }
        if (hidden > 0)
        {
            ImGui::TextColored(muted(), "%zu slider%s hidden: this skeleton has no matching bones (see the Body tab's groups).", hidden,
                               hidden == 1 ? "" : "s");
        }
        ImGui::PopTextWrapPos();
        ImGui::PopFont();
        ImGui::EndChild();
    }

    void PanelView::draw_detailed()
    {
        if (!m_skeleton.names.empty() || !m_morph_names.empty())
        {
            draw_symmetry_tools(false);
        }
        if (m_morph_names.empty())
        {
            draw_detailed_bones();
            return;
        }
        Skin& sk = skin();
        if (ImGui::BeginTabBar("##detail"))
        {
            auto detail_flags = [this](Detail d) {
                return m_focus_detail == static_cast<int>(d) ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
            };
            char label[64];
            std::snprintf(label, sizeof(label), "Bones (%zu)###bones", m_skeleton.names.size());
            if (sk.begin_tab_item(label, "bones", detail_flags(Detail::Bones)))
            {
                draw_detailed_bones();
                ImGui::EndTabItem();
            }
            std::snprintf(label, sizeof(label), "Morphs (%zu)###morphs", m_morph_names.size());
            if (sk.begin_tab_item(label, "morphs", detail_flags(Detail::Morphs)))
            {
                draw_detailed_morphs();
                ImGui::EndTabItem();
            }
            m_focus_detail = -1;
            ImGui::EndTabBar();
        }
    }

    void PanelView::draw_detailed_bones()
    {
        Skin& sk = skin();
        if (m_skeleton.names.empty())
        {
            ImGui::TextWrapped("No bones yet - no character mesh has been found.");
            return;
        }
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("Edited only").x - ImGui::GetFrameHeight() -
                                ImGui::GetStyle().ItemSpacing.x * 2);
        ImGui::InputTextWithHint("##find", "Search bones...", m_filter, sizeof(m_filter));
        ImGui::SameLine();
        sk.checkbox("Edited only", &m_only_edited);

        const std::string needle = fold_case(m_filter);
        const float list_h = std::max(140.0f, ImGui::GetContentRegionAvail().y * 0.42f);
        sk.begin_child("##bonelist", ImVec2(0, list_h), ImGuiChildFlags_Borders);
        const float indent = ImGui::GetFontSize() * 0.75f;
        for (size_t i = 0; i < m_skeleton.names.size(); ++i)
        {
            const std::string& name = m_skeleton.names[i];
            const auto it = m_book.find(fold_case(name));
            const bool edited = it != m_book.end() && !it->second.edit.is_neutral();
            if (m_only_edited && !edited)
            {
                continue;
            }
            if (!needle.empty() && fold_case(name).find(needle) == std::string::npos)
            {
                continue;
            }
            if (needle.empty() && !m_only_edited)
            {
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + indent * static_cast<float>(m_skeleton.depth[i]));
            }
            char label[256];
            const auto grp = m_bone_group.find(fold_case(name));
            const std::string part = grp != m_bone_group.end() ? "   [" + grp->second + "]" : std::string();
            if (edited)
            {
                const BoneEdit& e = it->second.edit;
                std::string tags;
                if (e.has_scale())
                {
                    char buf[48];
                    std::snprintf(buf, sizeof(buf), "  %.2f/%.2f/%.2f", e.axis[0], e.axis[1], e.axis[2]);
                    tags += buf;
                }
                if (e.has_turn())
                {
                    tags += "  rot";
                }
                if (e.has_shift())
                {
                    tags += "  move";
                }
                std::snprintf(label, sizeof(label), "%s%s %s##%zu", name.c_str(), part.c_str(), tags.c_str(), i);
                ImGui::PushStyleColor(ImGuiCol_Text, accent());
            }
            else
            {
                std::snprintf(label, sizeof(label), "%s%s##%zu", name.c_str(), part.c_str(), i);
            }
            if (ImGui::Selectable(label, fold_case(m_selected) == fold_case(name)))
            {
                m_selected = name;
            }
            if (edited)
            {
                ImGui::PopStyleColor();
            }
        }
        sk.end_child();

        ImGui::Spacing();
        if (m_selected.empty() || !has_bone(m_selected))
        {
            ImGui::TextColored(muted(), "Pick a bone above to edit it.");
            return;
        }

        const std::string mirror = mirror_bone_name(m_selected);
        const bool can_mirror = !mirror.empty() && has_bone(mirror);
        ImGui::TextColored(accent(), "%s", m_selected.c_str());
        if (can_mirror && m_mirror)
        {
            ImGui::SameLine();
            ImGui::TextColored(muted(), "+ %s (Symmetry lock)", mirror.c_str());
        }

        BoneEdit s = scale_of(m_selected, Spread::Chain);
        bool changed = false;
        sk.begin_child("##editor", ImVec2(0, 0), ImGuiChildFlags_None);
        // Same rows as the Body tab: label | hairline track | number box | Reset.
        const float label_w = ImGui::CalcTextSize("Length (X)").x + ImGui::GetFontSize() * 1.6f;
        auto row = [&](const char* label, double& value, float lo, float hi, bool log, float neutral, const char* format, const char* tip) {
            RowSpec spec;
            spec.id = label;
            spec.title = label;
            spec.tip = tip;
            spec.edited = std::fabs(value - neutral) > 1e-6;
            spec.lo = lo;
            spec.hi = hi;
            spec.log = log;
            spec.neutral = neutral;
            spec.format = format;
            spec.can_reset = spec.edited;
            spec.label_w = label_w;
            float v = static_cast<float>(value);
            const RowResult r = part_row(spec, v);
            if (r.reset)
            {
                value = neutral;
                return true;
            }
            if (r.changed)
            {
                value = log ? std::clamp(static_cast<double>(v), 0.01, 100.0) : v;
                return true;
            }
            return false;
        };
        ImGui::PushStyleColor(ImGuiCol_Header, sk.color(Role::Section));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImGui::GetStyleColorVec4(ImGuiCol_FrameBgHovered));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImGui::GetStyleColorVec4(ImGuiCol_FrameBgActive));
        const bool scale_open = ImGui::CollapsingHeader(s.has_scale() ? "Scale  *###scale" : "Scale###scale", ImGuiTreeNodeFlags_DefaultOpen);
        ImGui::PopStyleColor(3);
        if (scale_open)
        {
            double size = (s.axis[0] + s.axis[1] + s.axis[2]) / 3.0;
            if (row("Size", size, kScaleMin, kScaleMax, true, 1.0f, "%.3f", "All three axes together"))
            {
                set_size(s, size);
                changed = true;
            }
            changed |= row("Length (X)", s.axis[0], kScaleMin, kScaleMax, true, 1.0f, "%.3f", "Along the bone");
            changed |= row("Width (Y)", s.axis[1], kScaleMin, kScaleMax, true, 1.0f, "%.3f", "Sideways");
            changed |= row("Depth (Z)", s.axis[2], kScaleMin, kScaleMax, true, 1.0f, "%.3f", "Front to back");
        }
        ImGui::PushStyleColor(ImGuiCol_Header, sk.color(Role::Section));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImGui::GetStyleColorVec4(ImGuiCol_FrameBgHovered));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImGui::GetStyleColorVec4(ImGuiCol_FrameBgActive));
        const bool turn_open = ImGui::CollapsingHeader(s.has_turn() ? "Rotate  *###rotate" : "Rotate###rotate", s.has_turn() ? ImGuiTreeNodeFlags_DefaultOpen : 0);
        ImGui::PopStyleColor(3);
        if (turn_open)
        {
            changed |= row("Roll (X)", s.turn[0], -180.0f, 180.0f, false, 0.0f, "%.1f", "Degrees");
            changed |= row("Pitch (Y)", s.turn[1], -180.0f, 180.0f, false, 0.0f, "%.1f", "Degrees");
            changed |= row("Yaw (Z)", s.turn[2], -180.0f, 180.0f, false, 0.0f, "%.1f", "Degrees");
        }
        ImGui::PushStyleColor(ImGuiCol_Header, sk.color(Role::Section));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImGui::GetStyleColorVec4(ImGuiCol_FrameBgHovered));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImGui::GetStyleColorVec4(ImGuiCol_FrameBgActive));
        const bool move_open = ImGui::CollapsingHeader(s.has_shift() ? "Move  *###move" : "Move###move", s.has_shift() ? ImGuiTreeNodeFlags_DefaultOpen : 0);
        ImGui::PopStyleColor(3);
        if (move_open)
        {
            changed |= row("Along (X)", s.shift[0], -20.0f, 20.0f, false, 0.0f, "%.2f", "Centimetres");
            changed |= row("Side (Y)", s.shift[1], -20.0f, 20.0f, false, 0.0f, "%.2f", "Centimetres");
            changed |= row("Up (Z)", s.shift[2], -20.0f, 20.0f, false, 0.0f, "%.2f", "Centimetres");
        }
        ImGui::Spacing();

        int spread = static_cast<int>(s.spread);
        const char* spreads[3] = {spread_label(Spread::Chain), spread_label(Spread::Keep), spread_label(Spread::Solo)};
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Children");
        ImGui::SameLine(label_w);
        ImGui::SetNextItemWidth(std::min(ImGui::GetContentRegionAvail().x, ImGui::GetFontSize() * 14.0f));
        if (ImGui::Combo("##children", &spread, spreads, 3))
        {
            s.spread = static_cast<Spread>(spread);
            changed = true; // remembered as a hint even while the bone is still at 1.0x
        }
        tooltip("Scale children too: children follow the bone's scale, rotation and move.\n"
                "Keep children size: children follow rotation/move and the new shape but keep their own size.\n"
                "This bone only: nothing below it changes (rotation/move included).");

        if (sk.button("Reset bone", "reset"))
        {
            BoneEdit neutral;
            neutral.spread = s.spread;
            s = neutral;
            changed = true;
        }

        ImGui::TextColored(muted(), "Click a number to type a value. Ctrl+wheel over a slider nudges it (Shift: finer).");
        sk.end_child();

        if (changed)
        {
            write(m_selected, s);
            if (can_mirror && m_mirror)
            {
                write(mirror, Registry::instance().mirror_edit(m_selected, s)); // mirror-image rotation/move, measured from the skeleton
            }
        }
    }

    void PanelView::draw_detailed_morphs()
    {
        Skin& sk = skin();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("Edited only").x - ImGui::GetFrameHeight() -
                                ImGui::GetStyle().ItemSpacing.x * 2);
        ImGui::InputTextWithHint("##findmorph", "Search morphs...", m_morph_filter, sizeof(m_morph_filter));
        ImGui::SameLine();
        sk.checkbox("Edited only", &m_morph_only_edited);

        const std::string needle = fold_case(m_morph_filter);
        std::vector<const std::string*> rows;
        rows.reserve(m_morph_names.size());
        for (const std::string& m : m_morph_names)
        {
            if (m_morph_only_edited && !morph_of(m))
            {
                continue;
            }
            if (!contains_folded(m, needle))
            {
                // also find "Breast enlarge" or "Chest" (the Body tab name of the slider)
                const auto place = m_morph_place.find(fold_case(m));
                if (needle.empty() || place == m_morph_place.end() || !contains_folded(place->second, needle))
                {
                    continue;
                }
            }
            rows.push_back(&m);
        }

        const float footer = ImGui::GetFrameHeightWithSpacing() * 2.2f;
        sk.begin_child("##morphlist", ImVec2(0, std::max(140.0f, ImGui::GetContentRegionAvail().y - footer)), ImGuiChildFlags_Borders);
        float name_w = ImGui::CalcTextSize("MMMMMMMMMMMM").x;
        for (const std::string* m : rows)
        {
            name_w = std::max(name_w, ImGui::CalcTextSize(m->c_str()).x);
        }
        const float warn_w = ImGui::CalcTextSize(" !").x;
        name_w = std::min(name_w + warn_w + ImGui::GetFontSize() * 1.2f, ImGui::GetContentRegionAvail().x * 0.40f) + ImGui::GetStyle().ItemSpacing.x;

        ImGuiListClipper clipper; // faces can have hundreds of morphs
        clipper.Begin(static_cast<int>(rows.size()), ImGui::GetFrameHeightWithSpacing());
        while (clipper.Step())
        {
            for (int r = clipper.DisplayStart; r < clipper.DisplayEnd; ++r)
            {
                const std::string& name = *rows[static_cast<size_t>(r)];
                ImGui::PushID(name.c_str());
                const std::optional<double> w = morph_of(name);
                const bool animated = m_animated.count(fold_case(name)) > 0;
                RowSpec spec;
                spec.id = "row";
                spec.title = animated ? name + " !" : name;
                spec.tip = animated ? "The game keeps setting this morph itself (animation or its own code),\nso the slider may not stick. "
                                      "UUEPBS re-applies it each time it notices."
                                    : "Empty box = the game's own value.";
                {
                    const auto place = m_morph_place.find(fold_case(name));
                    spec.tip += place != m_morph_place.end() ? "\nBody tab: " + place->second : std::string("\nNot on the Body tab (Advanced only).");
                }
                spec.edited = w.has_value();
                spec.lo = kMorphSliderMin;
                spec.hi = kMorphSliderMax;
                spec.unset = !w.has_value();
                spec.can_reset = w.has_value();
                spec.label_w = name_w;
                float v = static_cast<float>(w.value_or(0.0));
                const RowResult res = part_row(spec, v);
                const std::string other = mirror_bone_name(name);
                const bool twin = m_mirror && !other.empty() && has_morph(other);
                if (res.reset)
                {
                    write_morph(name, std::nullopt);
                    if (twin)
                    {
                        write_morph(other, std::nullopt);
                    }
                }
                else if (res.changed)
                {
                    write_morph(name, v);
                    if (twin)
                    {
                        write_morph(other, v);
                    }
                }
                ImGui::PopID();
            }
        }
        if (rows.empty())
        {
            ImGui::TextColored(muted(), m_morph_only_edited ? "No morph is set yet." : "No morph matches the search.");
        }
        sk.end_child();

        ImGui::BeginDisabled(m_morphs.size() == looks::count(m_morphs)); // look:* entries are not morphs
        if (sk.button("Reset all morphs", "reset"))
        {
            // Looks ride along in the morph book (looks.hpp); this button leaves them alone.
            m_morphs = looks::only_looks(Registry::instance().morphs());
            Registry::instance().replace_morphs(m_morphs);
            m_morph_revision = Registry::instance().morph_revision();
            set_message("All morphs given back to the game");
        }
        ImGui::EndDisabled();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(muted(), "A set morph replaces the game's value (even at 0); Reset gives it back. Sliders run from -5 to 5; click a box to type a value.");
        ImGui::PopTextWrapPos();
    }

    void PanelView::refresh_presets()
    {
        if (m_shelf)
        {
            m_presets = m_shelf->list();
        }
        m_presets_time = Clock::now();
    }

    void PanelView::load_preset(const std::string& name)
    {
        EditBook book;
        MorphBook morphs;
        std::string message;
        if (m_shelf && m_shelf->load(name, book, message, &morphs))
        {
            looks::keep_when_absent(morphs, Registry::instance().morphs());
            Registry::instance().replace_edits(std::move(book));
            Registry::instance().replace_morphs(std::move(morphs));
            std::snprintf(m_preset_name, sizeof(m_preset_name), "%s", name.c_str());
            m_picked = name; // its details stay on show
        }
        set_message(message);
    }

    void PanelView::save_preset(const std::string& name)
    {
        std::string message;
        bool saved = false;
        if (m_shelf)
        {
            const MorphBook morphs = Registry::instance().morphs();
            saved = m_shelf->save(name, Registry::instance().edits(), message, &morphs);
        }
        set_message(message);
        refresh_presets();
        if (saved)
        {
            const std::string clean = PresetShelf::clean_name(name);
            m_picked = clean;
            m_scroll_to_picked = true;
            request_preset_thumbnail(clean); // a picture of the character as it looks now
        }
    }

    void PanelView::draw_presets()
    {
        Skin& sk = skin();
        if (!m_shelf || !m_shelf->ready())
        {
            ImGui::TextColored(warn(), "Preset folder is not set up yet.");
            return;
        }
        if (Clock::now() - m_presets_time > std::chrono::seconds(3))
        {
            refresh_presets();
        }

        section_bar("Save current sliders");
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - sk.button_width("Save", "save") - ImGui::GetStyle().ItemSpacing.x);
        const bool enter =
            ImGui::InputTextWithHint("##name", "Preset name", m_preset_name, sizeof(m_preset_name), ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        const std::string clean = PresetShelf::clean_name(m_preset_name);
        ImGui::BeginDisabled(clean.empty());
        if (sk.button("Save", "save") || (enter && !clean.empty()))
        {
            if (m_shelf->exists(clean))
            {
                m_pending = clean;
                ImGui::OpenPopup("Overwrite preset?");
            }
            else
            {
                save_preset(clean);
            }
        }
        ImGui::EndDisabled();

        if (ImGui::BeginPopupModal("Overwrite preset?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("'%s' already exists. Replace it?", m_pending.c_str());
            if (sk.button("Replace"))
            {
                save_preset(m_pending);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (sk.button("Cancel"))
            {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        ImGui::Spacing();
        section_bar("Saved presets");
        const float list_h = std::max(160.0f, ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing() * 4.5f - share_codes_height());
        draw_preset_browser(list_h);

        const bool picked = !m_picked.empty() && std::find(m_presets.begin(), m_presets.end(), m_picked) != m_presets.end();
        ImGui::BeginDisabled(!picked);
        if (sk.button("Load", "load"))
        {
            load_preset(m_picked);
        }
        ImGui::SameLine();
        if (sk.button("Delete", "delete"))
        {
            m_pending = m_picked;
            ImGui::OpenPopup("Delete preset?");
        }
        ImGui::SameLine();
        if (sk.button("Retake picture"))
        {
            request_preset_thumbnail(m_picked);
        }
        tooltip("Takes a new thumbnail of the selected character as it looks right now (load the preset first).\n"
                "Your own PNG/JPG named after the preset in the presets folder's _library folder always wins.");
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (sk.button("Open folder", "folder"))
        {
            m_host->open_folder(m_shelf->folder());
        }
        ImGui::SameLine();
        if (sk.button("Reset all", "reset"))
        {
            ImGui::OpenPopup("Reset all sliders?");
        }

        if (ImGui::BeginPopupModal("Delete preset?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Delete '%s' (and its thumbnail and tags) from disk?", m_pending.c_str());
            if (sk.button("Delete"))
            {
                std::string message;
                if (m_shelf->remove(m_pending, message))
                {
                    m_library.forget(m_pending); // its tags and thumbnail
                }
                set_message(message);
                m_picked.clear();
                refresh_presets();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (sk.button("Cancel"))
            {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        if (ImGui::BeginPopupModal("Reset all sliders?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            const std::string who =
                m_target.label.empty() ? std::string("this character") : CharacterNames::instance().shown(m_target.label, m_target.key, identity_of(m_target.key));
            ImGui::Text(m_morph_names.empty() ? "Set every bone of %s back to 1.0x?" : "Set every bone of %s back to 1.0x and give every morph back to the game?",
                        who.c_str());
            if (m_edited.size() > (is_edited(m_target.key) ? 1u : 0u))
            {
                ImGui::TextColored(muted(), "Other characters keep their sliders (release them in the Status tab).");
            }
            if (sk.button("Reset"))
            {
                Registry::instance().clear_edits();
                Registry::instance().clear_morphs();
                set_message("All sliders reset");
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (sk.button("Cancel"))
            {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        draw_share_codes();

        ImGui::Spacing();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(muted(), "Folder: %s", path_to_utf8(m_shelf->folder()).c_str());
        const std::string keys = m_targets ? m_targets->preset_hotkeys_text() : std::string();
        if (!keys.empty())
        {
            ImGui::TextColored(muted(), "Hotkeys: %s (NextPresetKey / PrevPresetKey in config.lua)", keys.c_str());
        }
        ImGui::PopTextWrapPos();
    }

    float PanelView::share_codes_height() const
    {
        // section bar + button row + one line of status/hint (+ name/apply row when a code is waiting)
        float rows = (m_import || !m_import_error.empty()) ? 4.2f : 3.2f;
        if (m_targets && !m_targets->preset_hotkeys_text().empty())
        {
            rows += 1.0f;
        }
        return ImGui::GetFrameHeightWithSpacing() * rows;
    }

    void PanelView::draw_share_codes()
    {
        Skin& sk = skin();
        ImGui::Spacing();
        section_bar("Share codes");

        auto copy_code = [&](const EditBook& book, const MorphBook& morphs, const std::string& name) {
            if (book.empty() && morphs.empty())
            {
                set_message("Nothing to share: these sliders are all unchanged");
                return;
            }
            const std::string code = share::encode(book, &morphs, name);
            ImGui::SetClipboardText(code.c_str());
            set_message("Preset code copied (" + std::to_string(code.size()) + " characters): paste it anywhere to share");
        };

        if (sk.button("Copy current sliders", "save"))
        {
            const std::string who = m_target.label.empty() ? std::string() : CharacterNames::instance().shown(m_target.label, m_target.key, identity_of(m_target.key));
            copy_code(Registry::instance().edits(), Registry::instance().morphs(), PresetShelf::clean_name(m_preset_name).empty() ? who : m_preset_name);
        }
        tooltip("Copies the sliders of the character shown above as a code (starts with UUEPBS1.)");
        ImGui::SameLine();
        const bool picked = !m_picked.empty() && std::find(m_presets.begin(), m_presets.end(), m_picked) != m_presets.end();
        ImGui::BeginDisabled(!picked);
        if (sk.button("Copy picked preset", "load"))
        {
            EditBook book;
            MorphBook morphs;
            std::string message;
            if (m_shelf->load(m_picked, book, message, &morphs))
            {
                copy_code(book, morphs, m_picked);
            }
            else
            {
                set_message(message);
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (sk.button("Paste code", "load"))
        {
            const char* clip = ImGui::GetClipboardText();
            share::Decoded decoded;
            std::string message;
            m_import.reset();
            m_import_error.clear();
            if (share::decode(clip ? clip : "", decoded, message))
            {
                std::snprintf(m_import_name, sizeof(m_import_name), "%s", decoded.name.c_str());
                m_import = std::move(decoded);
            }
            else
            {
                m_import_error = message;
            }
            set_message(message);
        }
        tooltip("Reads a preset code from the clipboard. Nothing changes until you Apply or Save it.");

        if (!m_import_error.empty())
        {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(warn(), "%s", m_import_error.c_str());
            ImGui::PopTextWrapPos();
            if (sk.button("Dismiss"))
            {
                m_import_error.clear();
            }
            return;
        }
        if (!m_import)
        {
            ImGui::TextColored(muted(), "Codes hold bones and morphs only, never files, so they are safe to paste.");
            return;
        }

        ImGui::TextColored(muted(), "Pasted code: %s%s%s", m_import->name.empty() ? "unnamed" : m_import->name.c_str(), " - ", m_import->summary.c_str());
        const std::string who =
            m_target.label.empty() ? std::string("this character") : CharacterNames::instance().shown(m_target.label, m_target.key, identity_of(m_target.key));
        const std::string apply = "Apply to " + who;
        if (sk.button(apply.c_str(), "load"))
        {
            MorphBook morphs = m_import->morphs;
            looks::keep_when_absent(morphs, Registry::instance().morphs()); // a code without looks keeps them, like a preset
            Registry::instance().replace_edits(m_import->bones);
            Registry::instance().replace_morphs(std::move(morphs));
            set_message("Applied the pasted code to " + who + " (" + m_import->summary + ")");
        }
        ImGui::SameLine();
        const float save_w = sk.button_width("Save as preset", "save");
        const float clear_w = sk.button_width("Clear");
        ImGui::SetNextItemWidth(std::max(80.0f, ImGui::GetContentRegionAvail().x - save_w - clear_w - ImGui::GetStyle().ItemSpacing.x * 2));
        ImGui::InputTextWithHint("##importname", "Name for the preset", m_import_name, sizeof(m_import_name));
        ImGui::SameLine();
        const std::string name = PresetShelf::clean_name(m_import_name);
        const bool taken = !name.empty() && m_shelf->exists(name);
        ImGui::BeginDisabled(name.empty() || taken);
        if (sk.button("Save as preset", "save"))
        {
            std::string message;
            if (m_shelf->save(name, m_import->bones, message, &m_import->morphs))
            {
                m_picked = name;
            }
            set_message(message);
            refresh_presets();
        }
        ImGui::EndDisabled();
        if (taken && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort))
        {
            ImGui::SetTooltip("A preset with this name exists already: pick another name");
        }
        ImGui::SameLine();
        if (sk.button("Clear"))
        {
            m_import.reset();
        }
    }

    void PanelView::draw_skin_picker()
    {
        Skin& sk = skin();
        ImGui::Spacing();
        section_bar("Window skin");
        if (Clock::now() - m_skins_time > std::chrono::seconds(3))
        {
            m_skins = sk.available();
            m_skins_time = Clock::now();
        }
        const float open_w = sk.button_width("Open folder", "folder");
        const float reload_w = sk.button_width("Reload", "refresh");
        ImGui::SetNextItemWidth(std::max(120.0f, ImGui::GetContentRegionAvail().x - open_w - reload_w - ImGui::GetStyle().ItemSpacing.x * 2));
        const std::string current = sk.name().empty() ? std::string(Skin::kBuiltinName) + " (built-in)" : sk.display_name();
        if (ImGui::BeginCombo("##skin", current.c_str(), ImGuiComboFlags_HeightLarge))
        {
            if (ImGui::Selectable("Default (built-in)", sk.name().empty()))
            {
                Skin::request("");
            }
            for (const std::string& n : m_skins)
            {
                if (ImGui::Selectable(n.c_str(), n == sk.name()))
                {
                    Skin::request(n);
                }
            }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemClicked())
        {
            m_skins = sk.available();
        }
        tooltip("Skins live in the folder next to the presets folder: one sub-folder per skin with a skin.json\n"
                "and PNG/JPG images. See README.txt there. The active skin reloads when its files change.");
        ImGui::SameLine();
        if (sk.button("Reload", "refresh"))
        {
            Skin::request(sk.name());
            m_skins = sk.available();
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(sk.folder().empty());
        if (sk.button("Open folder##skins", "folder"))
        {
            m_host->open_folder(sk.folder());
        }
        ImGui::EndDisabled();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(muted(), "Skin: %s", sk.status().c_str());
        for (const std::string& p : sk.problems())
        {
            ImGui::TextColored(warn(), "  %s", p.c_str());
        }
        ImGui::PopTextWrapPos();
    }

    void PanelView::draw_status()
    {
        Skin& sk = skin();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(m_host->hook_active() ? good() : warn(), "%s", m_host->hook_text().c_str());
        ImGui::PopTextWrapPos();
        ImGui::Spacing();

        if (sk.button("Rescan", "refresh"))
        {
            if (m_targets)
            {
                m_targets->request_rescan();
            }
            else
            {
                Registry::instance().request_rescan();
            }
            set_message("Rescan requested");
        }
        ImGui::SameLine();
        if (m_targets && sk.button("Write diagnostics"))
        {
            set_message("Diagnostics written to " + m_targets->write_diagnostics());
        }
        tooltip("Writes what the hook finder sees into the log file next to the DLL.\nSend that file along with bug reports.");
        ImGui::SameLine();
        if (sk.checkbox("Keep this window on top", &m_topmost))
        {
            m_host->set_topmost(m_topmost);
        }

        ImGui::Spacing();
        ImGui::PushTextWrapPos(0.0f);
        if (m_targets)
        {
            ImGui::TextColored(muted(), "Lua link: %s", m_targets->link_text().c_str());
            if (!m_target.label.empty())
            {
                const std::string identity = identity_of(m_target.key);
                const std::string name = CharacterNames::instance().shown(m_target.label, m_target.key, identity);
                std::string line = "Character: " + name;
                if (name != m_target.label)
                {
                    line += " (game name: " + m_target.label + ")";
                }
                line += "; engine name: " + (m_target.engine.empty() ? std::string("not reported by the script") : m_target.engine);
                if (!identity.empty())
                {
                    line += "; remembered as: " + identity;
                }
                ImGui::TextColored(muted(), "%s", line.c_str());
            }
        }
        ImGui::TextColored(muted(), "Mirroring: %s", m_mirror_text.c_str());
        ImGui::TextColored(muted(), "Simplified Panel: %zu group(s)%s; %s", m_body.groups.size(),
                           m_body.style.empty() ? "" : (", " + m_body.style + " rig").c_str(), m_body_status.c_str());
        ImGui::TextColored(muted(), "Morphs: %zu on the meshes%s, %zu set; game profile: %s", m_morph_names.size(),
                           m_morphs_hidden ? (" (+" + std::to_string(m_morphs_hidden) + " hidden by ExcludeMorphs)").c_str() : "", m_morphs.size() - looks::count(m_morphs),
                           m_morph_status.c_str());
        const std::string renderer = m_host->renderer_text();
        if (!renderer.empty())
        {
            ImGui::TextColored(muted(), "Window drawing: %s", renderer.c_str());
        }
        ImGui::PopTextWrapPos();

        draw_edited_characters();
        draw_skin_picker();

        ImGui::Spacing();
        section_bar("Tracked meshes");
        if (ImGui::BeginTable("##rigs", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableSetupColumn("Component");
            ImGui::TableSetupColumn("Bones");
            ImGui::TableSetupColumn("Frames seen");
            ImGui::TableHeadersRow();
            for (const auto& r : m_rigs)
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextColored(r.stale ? warn() : r.active ? ImGui::GetStyleColorVec4(ImGuiCol_Text) : muted(), "%s / %s%s%s", r.owner.c_str(),
                                   r.label.c_str(), r.primary && r.active ? " (bones listed)" : "", r.stale ? " (stale)" : "");
                if (!r.owner.empty() && ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("%s", r.owner.c_str());
                }
                ImGui::TableNextColumn();
                ImGui::Text("%d", r.bones);
                ImGui::TableNextColumn();
                ImGui::Text("%llu", static_cast<unsigned long long>(r.frames));
            }
            ImGui::EndTable();
        }
        ImGui::Spacing();
        ImGui::PushTextWrapPos(0.0f);
        const std::string key = m_targets ? m_targets->hotkey_text() : std::string("F6");
        ImGui::TextColored(muted(), "%s opens/closes this window (in game or here). Changes are visual only: collision, "
                                    "physics bodies and sockets keep their original size. While the game is paused the "
                                    "pose updates on the next animated frame.",
                           key.c_str());
        ImGui::PopTextWrapPos();
    }

    // ---- undo / redo ----------------------------------------------------------

    EditSnapshot PanelView::snapshot() const
    {
        return EditSnapshot{m_book, m_morphs};
    }

    void PanelView::track_history()
    {
        const std::string actor = Registry::instance().active_actor();
        if (actor != m_history_actor)
        {
            m_history_actor = actor;
            // The window's copies may still be the previous character's until the next sync.
            m_book = Registry::instance().edits(&m_book_revision);
            m_morphs = Registry::instance().morphs(&m_morph_revision);
        }
        // A slider being dragged or typed into is one step, recorded once it is let go.
        m_history.observe(actor, snapshot(), ImGui::IsAnyItemActive());

        const ImGuiIO& io = ImGui::GetIO();
        if (io.WantTextInput || ImGui::IsAnyItemActive())
        {
            return; // text fields keep their own Ctrl+Z
        }
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Z))
        {
            undo_edit();
        }
        else if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Y) || ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z))
        {
            redo_edit();
        }
    }

    void PanelView::apply_snapshot(const EditSnapshot& s)
    {
        Registry& reg = Registry::instance();
        reg.replace_edits(s.bones);
        reg.replace_morphs(s.morphs);
        m_book = reg.edits(&m_book_revision);
        m_morphs = reg.morphs(&m_morph_revision);
    }

    void PanelView::undo_edit()
    {
        if (const auto s = m_history.undo(m_history_actor, snapshot()))
        {
            apply_snapshot(*s);
            set_message("Undone");
        }
    }

    void PanelView::redo_edit()
    {
        if (const auto s = m_history.redo(m_history_actor, snapshot()))
        {
            apply_snapshot(*s);
            set_message("Redone");
        }
    }

    float PanelView::undo_redo_width() const
    {
        return skin().button_width("Undo", "undo") + skin().button_width("Redo", "redo") + ImGui::GetStyle().ItemSpacing.x;
    }

    void PanelView::draw_undo_redo()
    {
        Skin& sk = skin();
        ImGui::BeginDisabled(!m_history.can_undo(m_history_actor));
        if (sk.button("Undo", "undo"))
        {
            undo_edit();
        }
        ImGui::EndDisabled();
        tooltip("Undo the last slider change of this character (Ctrl+Z).\nA whole drag, a preset load or a reset is one step.");
        ImGui::SameLine();
        ImGui::BeginDisabled(!m_history.can_redo(m_history_actor));
        if (sk.button("Redo", "redo"))
        {
            redo_edit();
        }
        ImGui::EndDisabled();
        tooltip("Redo what Undo took back (Ctrl+Y or Ctrl+Shift+Z).");
    }

    // ---- Body tab filter ------------------------------------------------------

    void PanelView::draw_changed_only_filter()
    {
        skin().checkbox("Changed only", &m_simplified_changed_only);
        tooltip("Show only the body parts (and morph groups) that differ from the game's own shape.");
    }

    // ---- preset details ----------------------------------------------------------

    void PanelView::refresh_preset_details()
    {
        const auto now = Clock::now();
        if (m_details_for == m_picked && now - m_details_time < std::chrono::seconds(2))
        {
            return;
        }
        m_details_for = m_picked;
        m_details_time = now;
        m_details = {};
        m_details_ok = false;
        m_details_error.clear();
        if (m_picked.empty() || !m_shelf)
        {
            return;
        }
        EditBook book;
        MorphBook morphs;
        std::string message;
        if (!m_shelf->load(m_picked, book, message, &morphs))
        {
            m_details_error = message;
            return;
        }
        m_details = summarize_edits(book, morphs, m_body, m_morph_groups, m_skeleton.names, m_morph_names);
        m_details_ok = true;
    }

    void PanelView::draw_preset_details()
    {
        refresh_preset_details();
        if (m_picked.empty())
        {
            ImGui::TextColored(muted(), "Pick a preset to see what it changes.");
            return;
        }
        if (!m_details_ok)
        {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(warn(), "%s", m_details_error.c_str());
            ImGui::PopTextWrapPos();
            return;
        }
        const EditSummary& d = m_details;
        ImGui::TextColored(accent(), "%s", m_picked.c_str());
        ImGui::TextColored(muted(), "%zu bone%s, %zu morph%s", d.bones, d.bones == 1 ? "" : "s", d.morphs.size(), d.morphs.size() == 1 ? "" : "s");
        draw_preset_tags();

        auto part_row = [](const PartChange& p) {
            std::string extra;
            if (p.turn)
            {
                extra += ", rotated";
            }
            if (p.shift)
            {
                extra += ", moved";
            }
            ImGui::BulletText("%s  %.2fx%s", p.title.c_str(), p.scale, extra.c_str());
        };
        if (!d.parts.empty())
        {
            section_bar("Body parts");
            for (const PartChange& p : d.parts)
            {
                part_row(p);
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                {
                    std::string tip = "Bones:";
                    for (const std::string& b : p.bones)
                    {
                        tip += " " + b;
                    }
                    ImGui::SetTooltip("%s", tip.c_str());
                }
            }
        }
        if (!d.other_bones.empty())
        {
            section_bar("Other bones");
            for (const PartChange& p : d.other_bones)
            {
                part_row(p);
            }
        }
        if (!d.morphs.empty())
        {
            section_bar("Morphs");
            for (const MorphChange& m : d.morphs)
            {
                const std::string group = m.group.empty() ? std::string() : "  [" + m.group + "]";
                if (m.present)
                {
                    ImGui::BulletText("%s  %.2f%s", m.name.c_str(), m.weight, group.c_str());
                }
                else
                {
                    ImGui::TextColored(muted(), "  %s  %.2f  (not on this character)", m.name.c_str(), m.weight);
                }
            }
        }
        if (!d.missing.empty())
        {
            std::string list;
            for (const std::string& b : d.missing)
            {
                list += (list.empty() ? "" : ", ") + b;
            }
            ImGui::Spacing();
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(warn(), "Not on this character (%zu): %s", d.missing.size(), list.c_str());
            ImGui::PopTextWrapPos();
        }
        if (d.morphs.empty())
        {
            ImGui::Spacing();
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(muted(), "No morphs: loading it gives every morph back to the game.");
            ImGui::PopTextWrapPos();
        }
    }
    // ---- preset browser ----------------------------------------------------------

    void PanelView::search_presets(const std::string& query, std::set<std::string> tags)
    {
        std::snprintf(m_preset_search, sizeof(m_preset_search), "%s", query.c_str());
        m_tag_filter.clear();
        for (const std::string& t : tags)
        {
            m_tag_filter.insert(fold_case(t));
        }
    }

    void PanelView::sync_library()
    {
        const std::filesystem::path folder = m_shelf ? m_shelf->folder() : std::filesystem::path();
        if (folder != m_library_for)
        {
            m_library_for = folder;
            m_library.set_presets_folder(folder);
            preset_thumbnails().set_folder(m_library.folder());
            m_library_time = Clock::now();
        }
        else if (Clock::now() - m_library_time > std::chrono::seconds(2))
        {
            m_library.reload_if_changed(); // tags edited by hand
            m_library_time = Clock::now();
        }
    }

    void PanelView::request_preset_thumbnail(const std::string& preset)
    {
        if (preset.empty() || !m_targets || m_target.id.empty())
        {
            return;
        }
        m_targets->request_thumbnail(m_target.id, preset);
        m_thumb_wait[fold_case(preset)] = Clock::now();
    }

    bool PanelView::tag_chip(const char* label, bool active, bool removable)
    {
        Skin& sk = skin();
        const float pad_x = dpi_px(7.0f);
        const float h = std::floor(ImGui::GetFontSize() * 0.86f + dpi_px(8.0f));
        const float small = ImGui::GetFontSize() * 0.86f;
        ImFont* font = ImGui::GetFont();
        const ImVec2 ts = font->CalcTextSizeA(small, FLT_MAX, 0.0f, label);
        const float x_w = removable ? font->CalcTextSizeA(small, FLT_MAX, 0.0f, "x").x + pad_x : 0.0f;
        const float w = std::floor(ts.x + pad_x * 2.0f + x_w);
        // Wrap onto the next line when the chip does not fit.
        const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x; // GetContentRegionMax() is gone under IMGUI_DISABLE_OBSOLETE_FUNCTIONS
        if (ImGui::GetCursorScreenPos().x + w > right && ImGui::GetCursorScreenPos().x > ImGui::GetWindowPos().x + ImGui::GetStyle().WindowPadding.x + 1.0f)
        {
            ImGui::NewLine();
        }
        ImGui::PushID(label);
        const ImVec2 a = ImGui::GetCursorScreenPos();
        const bool pressed = ImGui::InvisibleButton("##chip", ImVec2(w, h));
        const bool hovered = ImGui::IsItemHovered();
        ImGui::PopID();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 b(a.x + w, a.y + h);
        dl->AddRectFilled(a, b, u32(active ? sk.color(Role::AccentDim) : hovered ? ImGui::GetStyleColorVec4(ImGuiCol_FrameBgHovered) : ImGui::GetStyleColorVec4(ImGuiCol_FrameBg)));
        dl->AddRect(a, b, u32(active ? accent() : hovered ? accent_dim() : sk.color(Role::Border)), 0.0f, 0, 1.0f);
        const float ty = std::floor(a.y + (h - ts.y) * 0.5f);
        dl->AddText(font, small, ImVec2(a.x + pad_x, ty), u32(active ? sk.color(Role::Title) : ImGui::GetStyleColorVec4(ImGuiCol_Text)), label);
        if (removable)
        {
            dl->AddText(font, small, ImVec2(b.x - pad_x - x_w + pad_x * 0.5f, ty), u32(hovered ? warn() : muted()), "x");
        }
        ImGui::SameLine(0.0f, dpi_px(4.0f));
        return pressed;
    }

    void PanelView::draw_preset_tags()
    {
        if (m_picked.empty() || !m_library.ready())
        {
            return;
        }
        ImGui::Dummy(ImVec2(0.0f, dpi_px(2.0f)));
        const std::vector<std::string> tags = m_library.tags(m_picked);
        std::string remove;
        for (const std::string& t : tags)
        {
            if (tag_chip(t.c_str(), false, true))
            {
                remove = t;
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            {
                ImGui::SetTooltip("Remove the tag '%s'", t.c_str());
            }
        }
        if (tags.size() < PresetLibrary::kMaxTags)
        {
            const float w = std::max(dpi_px(90.0f), ImGui::CalcTextSize("Add tag...").x + dpi_px(16.0f));
            const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x; // GetContentRegionMax() is gone under IMGUI_DISABLE_OBSOLETE_FUNCTIONS
            if (!tags.empty() && ImGui::GetCursorScreenPos().x + w > right)
            {
                ImGui::NewLine();
            }
            ImGui::SetNextItemWidth(w);
            ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 0.86f);
            if (ImGui::InputTextWithHint("##newtag", "Add tag...", m_new_tag, sizeof(m_new_tag), ImGuiInputTextFlags_EnterReturnsTrue))
            {
                if (!PresetLibrary::split_tags(m_new_tag).empty() && !m_library.add_tag(m_picked, m_new_tag))
                {
                    set_message("Could not save the tags (presets folder read-only?)");
                }
                m_new_tag[0] = '\0';
                ImGui::SetKeyboardFocusHere(-1); // keep typing the next one
            }
            ImGui::PopFont();
            tooltip("Type a tag and press Enter (commas add several). Tags are saved in the presets folder's _library\\presets.json.");
        }
        else
        {
            ImGui::NewLine();
        }
        if (!remove.empty())
        {
            m_library.remove_tag(m_picked, remove);
        }
    }

    void PanelView::draw_preset_browser(float height)
    {
        Skin& sk = skin();
        const ImGuiStyle& style = ImGui::GetStyle();
        sync_library();

        const float left_w = std::floor(ImGui::GetContentRegionAvail().x * 0.5f);
        ImGui::BeginGroup();

        // Search + Grid/List toggle
        const float grid_w = sk.button_width("Grid");
        const float list_w = sk.button_width("List");
        ImGui::SetNextItemWidth(left_w - grid_w - list_w - style.ItemSpacing.x);
        ImGui::InputTextWithHint("##presetsearch", "Search name or #tag", m_preset_search, sizeof(m_preset_search));
        tooltip("Every word must match the name or a tag. #word matches tags only.");
        ImGui::SameLine();
        auto toggle = [&](const char* label, bool on) {
            ImGui::PushStyleColor(ImGuiCol_Button, on ? sk.color(Role::AccentDim) : ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, style.ItemSpacing.y));
            const bool hit = sk.button(label);
            ImGui::PopStyleVar();
            ImGui::PopStyleColor();
            return hit;
        };
        if (toggle("Grid", !m_preset_list_view))
        {
            m_preset_list_view = false;
        }
        ImGui::SameLine(0.0f, 0.0f);
        if (toggle("List", m_preset_list_view))
        {
            m_preset_list_view = true;
        }

        // Tag filter chips (every tag in use; the active ones first)
        const auto counts = m_library.tag_counts(m_presets);
        std::erase_if(m_tag_filter, [&](const std::string& t) {
            return std::none_of(counts.begin(), counts.end(), [&](const auto& c) { return fold_case(c.first) == t; });
        });
        const float chips_top = ImGui::GetCursorPosY();
        if (!counts.empty())
        {
            ImGui::BeginChild("##tagchips", ImVec2(left_w, 0.0f), ImGuiChildFlags_AutoResizeY, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar);
            constexpr size_t kShownChips = 14;
            size_t shown = 0;
            std::string toggled;
            for (int pass = 0; pass < 2; ++pass)
            {
                for (const auto& [tag, n] : counts)
                {
                    const bool on = m_tag_filter.count(fold_case(tag)) > 0;
                    if ((pass == 0) != on || (!on && shown >= kShownChips))
                    {
                        continue;
                    }
                    ++shown;
                    const std::string label = tag + "  " + std::to_string(n);
                    if (tag_chip(label.c_str(), on))
                    {
                        toggled = fold_case(tag);
                    }
                }
            }
            if (!m_tag_filter.empty() || m_preset_search[0])
            {
                if (tag_chip("Clear", false))
                {
                    m_tag_filter.clear();
                    m_preset_search[0] = '\0';
                }
            }
            ImGui::NewLine();
            if (!toggled.empty() && !m_tag_filter.erase(toggled))
            {
                m_tag_filter.insert(toggled);
            }
            ImGui::EndChild();
        }
        const float used = ImGui::GetCursorPosY() - chips_top;

        // Grid / list
        const std::vector<std::string> shown = m_library.filter(m_presets, m_preset_search, m_tag_filter);
        const float grid_h = std::max(ImGui::GetFrameHeight() * 3.0f, height - used - ImGui::GetFrameHeightWithSpacing());
        sk.begin_child("##presetgrid", ImVec2(left_w, grid_h), ImGuiChildFlags_Borders);
        if (m_presets.empty())
        {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(muted(), "No presets yet. Save one above: a picture of the character is taken for it.");
            ImGui::PopTextWrapPos();
        }
        else if (shown.empty())
        {
            ImGui::TextColored(muted(), "No preset matches.");
        }

        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float base = style.FontSizeBase;
        const float small = base * style.FontScaleDpi * 0.8f;
        const auto now = Clock::now();
        std::string load;
        auto picture = [&](const std::string& name, ImVec2 a, ImVec2 b, bool selected, bool hovered) {
            const ImU32 border = u32(selected ? accent() : hovered ? accent_dim() : sk.color(Role::Border));
            ImTextureData* thumb = preset_thumbnails().find({name});
            const auto wait = m_thumb_wait.find(fold_case(name));
            const bool waiting = !thumb && wait != m_thumb_wait.end() && now - wait->second < std::chrono::seconds(20);
            monogram_frame(dl, a, b, thumb ? std::string() : monogram(name), (b.y - a.y) > dpi_px(60.0f) ? base * style.FontScaleDpi * 1.4f : small,
                           u32(hovered ? ImGui::GetStyleColorVec4(ImGuiCol_FrameBgHovered) : sk.color(Role::Section)), border,
                           u32(ImVec4(sk.color(Role::Title).x, sk.color(Role::Title).y, sk.color(Role::Title).z, 0.85f)));
            if (thumb)
            {
                Portraits::draw(dl, thumb, ImVec2(a.x + 1.0f, a.y + 1.0f), ImVec2(b.x - 1.0f, b.y - 1.0f));
                dl->AddRect(a, b, border, 0.0f, 0, 1.0f);
            }
            else if (waiting && (b.y - a.y) > dpi_px(60.0f))
            {
                const char* t = "taking picture";
                const ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(small, FLT_MAX, 0.0f, t);
                dl->AddText(ImGui::GetFont(), small, ImVec2(std::floor((a.x + b.x - ts.x) * 0.5f), b.y - ts.y - dpi_px(6.0f)), u32(muted()), t);
            }
        };
        auto tags_line = [&](const std::string& name) {
            std::string line;
            for (const std::string& t : m_library.tags(name))
            {
                line += (line.empty() ? "" : ", ") + t;
            }
            return line;
        };
        auto hover_tip = [&](const std::string& name, const std::string& tags) {
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            {
                ImGui::SetTooltip("%s%s%s\nDouble-click to load.", name.c_str(), tags.empty() ? "" : "\nTags: ", tags.c_str());
            }
        };

        if (!m_preset_list_view)
        {
            const float gap = dpi_px(8.0f);
            const float avail = ImGui::GetContentRegionAvail().x;
            const int cols = std::max(2, static_cast<int>((avail + gap) / (dpi_px(76.0f) + gap)));
            const float tile_w = std::floor((avail - gap * static_cast<float>(cols - 1)) / static_cast<float>(cols));
            const float img_h = std::floor(tile_w * 1.25f);
            const float foot_h = std::floor(ImGui::GetFontSize() + small + dpi_px(12.0f));
            int index = 0;
            for (const std::string& name : shown)
            {
                ImGui::PushID(name.c_str());
                if (index % cols != 0)
                {
                    ImGui::SameLine(0.0f, gap);
                }
                const ImVec2 a = ImGui::GetCursorScreenPos();
                const bool selected = m_picked == name;
                if (ImGui::InvisibleButton("##tile", ImVec2(tile_w, img_h + foot_h)))
                {
                    m_picked = name;
                }
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                {
                    load = name;
                }
                const bool hovered = ImGui::IsItemHovered();
                const std::string tags = tags_line(name);
                hover_tip(name, tags);
                if (selected && m_scroll_to_picked)
                {
                    ImGui::SetScrollHereY(0.5f);
                    m_scroll_to_picked = false;
                }
                const ImVec2 img_b(a.x + tile_w, a.y + img_h);
                const ImVec2 b(a.x + tile_w, a.y + img_h + foot_h);
                picture(name, a, img_b, selected, hovered);
                dl->AddRectFilled(ImVec2(a.x, img_b.y), b, u32(selected ? accent_dim() : ImGui::GetStyleColorVec4(ImGuiCol_FrameBg)));
                dl->AddRect(a, b, u32(selected ? accent() : hovered ? accent_dim() : sk.color(Role::Border)), 0.0f, 0, 1.0f);
                if (selected)
                {
                    dl->AddRectFilled(a, ImVec2(b.x, a.y + dpi_px(2.0f)), u32(accent())); // accent top line, like the active tab
                }
                const float tx = a.x + dpi_px(6.0f);
                const float ny = img_b.y + dpi_px(5.0f);
                dl->PushClipRect(ImVec2(a.x, img_b.y), ImVec2(b.x - dpi_px(3.0f), b.y), true);
                dl->AddText(ImVec2(tx, ny), u32(ImGui::GetStyleColorVec4(ImGuiCol_Text)), fit_text(name, tile_w - dpi_px(10.0f)).c_str());
                dl->AddText(ImGui::GetFont(), small, ImVec2(tx, ny + ImGui::GetFontSize() + dpi_px(1.0f)), u32(muted()), tags.empty() ? "no tags" : tags.c_str());
                dl->PopClipRect();
                ImGui::PopID();
                ++index;
            }
        }
        else
        {
            const float row_h = std::floor(ImGui::GetFontSize() * 2.0f + dpi_px(8.0f));
            const float avail = ImGui::GetContentRegionAvail().x;
            for (const std::string& name : shown)
            {
                ImGui::PushID(name.c_str());
                const ImVec2 a = ImGui::GetCursorScreenPos();
                const bool selected = m_picked == name;
                if (ImGui::InvisibleButton("##row", ImVec2(avail, row_h)))
                {
                    m_picked = name;
                }
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                {
                    load = name;
                }
                const bool hovered = ImGui::IsItemHovered();
                const std::string tags = tags_line(name);
                hover_tip(name, tags);
                if (selected && m_scroll_to_picked)
                {
                    ImGui::SetScrollHereY(0.5f);
                    m_scroll_to_picked = false;
                }
                const ImVec2 b(a.x + avail, a.y + row_h);
                if (selected || hovered)
                {
                    dl->AddRectFilled(a, b, u32(selected ? accent_dim() : ImGui::GetStyleColorVec4(ImGuiCol_FrameBgHovered)));
                }
                const float pic = row_h - dpi_px(4.0f);
                const ImVec2 pa(a.x + dpi_px(2.0f), a.y + dpi_px(2.0f));
                picture(name, pa, ImVec2(pa.x + std::floor(pic * 0.75f), pa.y + pic), selected, hovered);
                const float tx = pa.x + std::floor(pic * 0.75f) + dpi_px(8.0f);
                dl->PushClipRect(a, ImVec2(b.x - dpi_px(3.0f), b.y), true);
                dl->AddText(ImVec2(tx, a.y + dpi_px(3.0f)), u32(ImGui::GetStyleColorVec4(ImGuiCol_Text)), name.c_str());
                dl->AddText(ImGui::GetFont(), small, ImVec2(tx, a.y + dpi_px(4.0f) + ImGui::GetFontSize()), u32(muted()), tags.empty() ? "no tags" : tags.c_str());
                dl->PopClipRect();
                ImGui::PopID();
            }
        }
        sk.end_child();
        ImGui::TextColored(muted(), "%zu of %zu preset%s", shown.size(), m_presets.size(), m_presets.size() == 1 ? "" : "s");
        ImGui::EndGroup();

        ImGui::SameLine();
        sk.begin_child("##presetdetails", ImVec2(0, height), ImGuiChildFlags_Borders);
        draw_preset_details();
        sk.end_child();

        if (!load.empty())
        {
            m_picked = load;
            load_preset(load);
        }
        // Forget waits that are over (picture arrived or gave up).
        for (auto it = m_thumb_wait.begin(); it != m_thumb_wait.end();)
        {
            it = now - it->second > std::chrono::seconds(30) ? m_thumb_wait.erase(it) : std::next(it);
        }
    }

    // ---- symmetry tools ------------------------------------------------------------

    void PanelView::apply_action(const EditSnapshot& next, const char* what)
    {
        if (same_edits(next, snapshot()))
        {
            set_message(std::string(what) + ": nothing to change");
            return;
        }
        m_history.checkpoint(m_history_actor, snapshot()); // an edit still settling stays its own step
        apply_snapshot(next);
        m_history.checkpoint(m_history_actor, snapshot()); // the action is exactly one step
    }

    void PanelView::mirror_all(Side from)
    {
        MirrorCount count;
        const EditSnapshot next = mirror_sides(snapshot(), m_skeleton.names, m_morph_names, from, Registry::instance().mirror_table(), &count);
        const char* what = from == Side::Left ? "Mirror left to right" : "Mirror right to left";
        apply_action(next, what);
        if (count.bones || count.morphs)
        {
            char buf[160];
            std::snprintf(buf, sizeof(buf), "%s: %zu bone%s, %zu morph%s (Ctrl+Z to undo)", what, count.bones, count.bones == 1 ? "" : "s",
                          count.morphs, count.morphs == 1 ? "" : "s");
            set_message(buf);
        }
    }

    void PanelView::randomize_body()
    {
        std::vector<RandomPart> parts;
        for (const GroupBones& gb : m_body.groups)
        {
            if (m_random_category_only && gb.group.section != m_category)
            {
                continue;
            }
            RandomPart p;
            p.spread = gb.group.spread;
            for (const std::string& b : gb.bones)
            {
                if (has_bone(b))
                {
                    p.bones.push_back(b);
                }
            }
            if (!p.bones.empty())
            {
                parts.push_back(std::move(p));
            }
        }
        std::vector<std::vector<std::string>> morphs;
        for (const MorphGroupMorphs& mg : m_morph_groups)
        {
            const std::string section = mg.group.section.empty() ? std::string("Morphs") : mg.group.section;
            if (!mg.morphs.empty() && mg.group.randomize && (!m_random_category_only || section == m_category))
            {
                morphs.push_back(mg.morphs);
            }
        }
        const uint64_t seed = static_cast<uint64_t>(Clock::now().time_since_epoch().count()) ^ (++m_random_count * 0x9E3779B97F4A7C15ull);
        apply_action(randomize_parts(snapshot(), parts, morphs, m_random, seed), "Randomize");
        set_message(m_random_category_only ? "Randomized " + m_category + " (Ctrl+Z to undo)" : std::string("Randomized the body (Ctrl+Z to undo)"));
    }

    void PanelView::draw_random_limits()
    {
        Skin& sk = skin();
        if (!ImGui::BeginPopup("##randomlimits"))
        {
            return;
        }
        ImGui::TextUnformatted("Randomize limits");
        ImGui::Separator();
        const float w = ImGui::GetFontSize() * 12.0f;
        float size = static_cast<float>(m_random.size * 100.0);
        float shape = static_cast<float>(m_random.shape * 100.0);
        float lo = static_cast<float>(m_random.morph_lo);
        float hi = static_cast<float>(m_random.morph_hi);
        sk.checkbox("Body parts", &m_random.parts);
        ImGui::BeginDisabled(!m_random.parts);
        ImGui::SetNextItemWidth(w);
        if (ImGui::SliderFloat("Size +/-", &size, 0.0f, 50.0f, "%.0f %%", ImGuiSliderFlags_AlwaysClamp))
        {
            m_random.size = size / 100.0;
        }
        tooltip("How far each part's overall size may move from 1.0 (15 % = 0.85 to 1.15).");
        ImGui::SetNextItemWidth(w);
        if (ImGui::SliderFloat("Shape +/-", &shape, 0.0f, 25.0f, "%.0f %%", ImGuiSliderFlags_AlwaysClamp))
        {
            m_random.shape = shape / 100.0;
        }
        tooltip("Extra variation of length, width and depth on top of the size. 0 keeps parts in proportion.");
        ImGui::EndDisabled();
        ImGui::BeginDisabled(m_morph_groups.empty());
        sk.checkbox("Morph groups", &m_random.morphs);
        ImGui::BeginDisabled(!m_random.morphs);
        ImGui::SetNextItemWidth(w);
        if (ImGui::DragFloatRange2("Weights", &lo, &hi, 0.01f, static_cast<float>(kMorphMin), static_cast<float>(kMorphMax), "%.2f", "%.2f",
                                   ImGuiSliderFlags_AlwaysClamp))
        {
            m_random.morph_lo = lo;
            m_random.morph_hi = hi;
        }
        tooltip("Morph group sliders get a weight between these two values.\n"
                "Face, Groin and Other sliders, forms and pregnancy are left alone.");
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        ImGui::Separator();
        const std::string only = "Only " + (m_category.empty() ? std::string("this category") : m_category);
        sk.checkbox(only.c_str(), &m_random_category_only);
        tooltip("Leave the other Body tab categories as they are.");
        if (sk.button("Defaults", "reset"))
        {
            m_random = RandomLimits{};
            m_random_category_only = false;
        }
        m_random.clamp();
        ImGui::EndPopup();
    }

    // One row above the sliders:  [x] Symmetry lock   Mirror L to R   Mirror R to L   ...   Randomize  Limits
    void PanelView::draw_symmetry_tools(bool randomize)
    {
        Skin& sk = skin();
        const ImGuiStyle& style = ImGui::GetStyle();
        sk.checkbox("Symmetry lock", &m_mirror);
        tooltip("On: editing a left or right bone (or _L/_R morph) under Advanced also edits its partner, as a mirror image.\n"
                "Body tab parts always move both sides together.");
        const bool any = !m_book.empty() || !m_morphs.empty();
        ImGui::SameLine(0.0f, style.ItemSpacing.x * 3.0f);
        ImGui::BeginDisabled(!any);
        if (sk.button("Mirror L to R", "mirror"))
        {
            mirror_all(Side::Left);
        }
        tooltip("Copy every left-side edit onto the right side, as a mirror image.\nRight-side edits are replaced. One Ctrl+Z undoes it.");
        ImGui::SameLine();
        if (sk.button("Mirror R to L", "mirror"))
        {
            mirror_all(Side::Right);
        }
        tooltip("Copy every right-side edit onto the left side, as a mirror image.\nLeft-side edits are replaced. One Ctrl+Z undoes it.");
        ImGui::EndDisabled();
        if (randomize)
        {
            const float right = sk.button_width("Randomize", "random") + sk.button_width("Limits", "settings") + style.ItemSpacing.x;
            ImGui::SameLine();
            const float room = ImGui::GetContentRegionAvail().x - right;
            if (room > 0.0f)
            {
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + room);
            }
            ImGui::BeginDisabled(!(m_random.parts && !m_body.groups.empty()) && !(m_random.morphs && !m_morph_groups.empty()));
            if (sk.button("Randomize", "random"))
            {
                randomize_body();
            }
            ImGui::EndDisabled();
            tooltip("Give every body part a random size inside the limits (both sides alike). One Ctrl+Z undoes it.");
            ImGui::SameLine();
            if (sk.button("Limits", "settings"))
            {
                ImGui::OpenPopup("##randomlimits");
            }
            tooltip("How far Randomize may go, and what it touches.");
            draw_random_limits();
        }
        ImGui::Dummy(ImVec2(0.0f, dpi_px(4.0f)));
    }
} // namespace uuepbs::ui
