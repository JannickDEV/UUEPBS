#include "panel_view.hpp"

#include "../core/character_names.hpp"
#include "../core/rig_names.hpp"
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
        // Morph sliders show 0..1; Ctrl+click can type anything in kMorphMin..kMorphMax.
        constexpr float kMorphSliderMin = 0.0f;
        constexpr float kMorphSliderMax = 1.0f;

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
            m_body_revision = body_rev;
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
            m_morph_groups = resolve_morph_groups(m_morph_names, m_morph_profile);
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

    bool PanelView::size_slider(const char* id, BoneEdit& s, float lo, float hi, const char* format)
    {
        const double avg = (s.axis[0] + s.axis[1] + s.axis[2]) / 3.0;
        float v = static_cast<float>(avg);
        const bool dragged = skin().slider_float(id, &v, lo, hi, format, ImGuiSliderFlags_Logarithmic);
        const bool nudged = wheel_nudge(v, 0.01f, 0.001f, lo, hi);
        if (!dragged && !nudged)
        {
            return false;
        }
        const bool uniform = std::fabs(s.axis[0] - s.axis[1]) < 1e-6 && std::fabs(s.axis[1] - s.axis[2]) < 1e-6;
        if (uniform || avg <= 1e-6)
        {
            s.axis[0] = s.axis[1] = s.axis[2] = v;
        }
        else
        {
            const double k = v / avg;
            for (double& a : s.axis)
            {
                a *= k;
            }
        }
        return true;
    }

    bool PanelView::axis_sliders(BoneEdit& s)
    {
        static const char* names[3] = {"Length (X)", "Width (Y)", "Depth (Z)"};
        bool changed = false;
        for (int a = 0; a < 3; ++a)
        {
            float v = static_cast<float>(s.axis[a]);
            ImGui::PushID(a);
            ImGui::SetNextItemWidth(-ImGui::CalcTextSize("Length (X)").x - ImGui::GetStyle().ItemInnerSpacing.x * 2);
            if (skin().slider_float(names[a], &v, kScaleMin, kScaleMax, "%.3fx", ImGuiSliderFlags_Logarithmic))
            {
                s.axis[a] = v;
                changed = true;
            }
            if (wheel_nudge(v, 0.01f, 0.001f, kScaleMin, kScaleMax))
            {
                s.axis[a] = v;
                changed = true;
            }
            ImGui::PopID();
        }
        return changed;
    }

    // Shared layout for a labelled three-row slider block.
    static bool triple_sliders(const char* const labels[3], double (&v)[3], float lo, float hi, const char* format, float step, float fine)
    {
        bool changed = false;
        for (int a = 0; a < 3; ++a)
        {
            float f = static_cast<float>(v[a]);
            ImGui::PushID(labels[a]);
            ImGui::SetNextItemWidth(-ImGui::CalcTextSize("Length (X)").x - ImGui::GetStyle().ItemInnerSpacing.x * 2);
            const bool dragged = skin().slider_float(labels[a], &f, lo, hi, format);
            if (dragged || wheel_nudge(f, step, fine, -BoneEdit::kMaxShift * 10.0f, BoneEdit::kMaxShift * 10.0f))
            {
                v[a] = f;
                changed = true;
            }
            ImGui::PopID();
        }
        return changed;
    }

    bool PanelView::turn_sliders(BoneEdit& s)
    {
        static const char* const labels[3] = {"Roll (X)", "Pitch (Y)", "Yaw (Z)"};
        return triple_sliders(labels, s.turn, -180.0f, 180.0f, "%.1f\xC2\xB0", 1.0f, 0.1f);
    }

    bool PanelView::shift_sliders(BoneEdit& s)
    {
        static const char* const labels[3] = {"Along (X)", "Side (Y)", "Up (Z)"};
        return triple_sliders(labels, s.shift, -20.0f, 20.0f, "%.2f cm", 0.1f, 0.01f);
    }

    bool PanelView::morph_slider(const char* id, float& v, const char* format)
    {
        const bool dragged = skin().slider_float(id, &v, kMorphSliderMin, kMorphSliderMax, format);
        const bool nudged = wheel_nudge(v, 0.05f, 0.01f, static_cast<float>(kMorphMin), static_cast<float>(kMorphMax));
        v = static_cast<float>(clamp_morph(v)); // typed values (Ctrl+click) may leave 0..1
        return dragged || nudged;
    }

    void PanelView::tooltip(const char* text)
    {
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort) && text && *text)
        {
            ImGui::SetTooltip("%s", text);
        }
    }

    void PanelView::draw()
    {
        sync();
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
        if (ImGui::BeginTabBar("##tabs"))
        {
            auto tab_flags = [this](Tab tab) {
                return m_focus_tab == static_cast<int>(tab) ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
            };
            // The ### ids (and icon keys) stay the same as before 2.5 so skins and saved focus keep working.
            if (sk.begin_tab_item("Body###simplified", "simplified", tab_flags(Tab::Simplified)))
            {
                draw_simplified();
                ImGui::EndTabItem();
            }
            if (sk.begin_tab_item("Presets###presets", "presets", tab_flags(Tab::Presets)))
            {
                draw_presets();
                ImGui::EndTabItem();
            }
            if (sk.begin_tab_item("Advanced###detailed", "detailed", tab_flags(Tab::Detailed)))
            {
                draw_detailed();
                ImGui::EndTabItem();
            }
            if (sk.begin_tab_item("Status###status", "status", tab_flags(Tab::Status)))
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

    // Small coloured dot + one or two words; the full sentence is in the tooltip.
    void PanelView::draw_live_badge()
    {
        size_t live = 0;
        for (const auto& r : m_rigs)
        {
            live += (!r.stale && r.frames > 0) ? 1 : 0;
        }
        const char* word = "Live";
        ImVec4 col = good();
        std::string tip;
        char buf[256];
        if (m_skeleton.names.empty())
        {
            word = "No character";
            col = warn();
            tip = "No character found yet. Pick one in the list or press Refresh.";
        }
        else if (!m_host->hook_active())
        {
            word = "Waiting";
            col = warn();
            std::snprintf(buf, sizeof(buf), "Found %s - waiting for the pose hook (see Status)",
                          m_skeleton.owner.empty() ? m_skeleton.label.c_str() : m_skeleton.owner.c_str());
            tip = buf;
        }
        else if (live == 0)
        {
            word = "Waiting";
            col = warn();
            std::snprintf(buf, sizeof(buf), "Tracking %zu mesh%s - waiting for an animated frame", m_rigs.size(), m_rigs.size() == 1 ? "" : "es");
            tip = buf;
        }
        else
        {
            std::snprintf(buf, sizeof(buf), "Live on %zu mesh%s - %s, %zu bones, %zu morphs", live, live == 1 ? "" : "es", m_skeleton.label.c_str(),
                          m_skeleton.names.size(), m_morph_names.size());
            tip = buf;
        }
        const float r = ImGui::GetFontSize() * 0.22f;
        ImGui::AlignTextToFramePadding();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float cy = p.y + ImGui::GetFrameHeight() * 0.5f;
        ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + r, cy), r, ImGui::GetColorU32(col));
        ImGui::Dummy(ImVec2(r * 2.0f, ImGui::GetFrameHeight()));
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(col, "%s", word);
        tooltip(tip.c_str());
    }

    // Compact header: title row (logo, version, live badge, Enabled), then the character row.
    void PanelView::draw_header()
    {
        Skin& sk = skin();
        const float base = ImGui::GetStyle().FontSizeBase;
        sk.draw_header_strip(std::max(ImGui::GetFontSize() * 1.18f, ImGui::GetFrameHeight()));
        if (!sk.logo(ImGui::GetFontSize()))
        {
            ImGui::PushFont(nullptr, base * 1.18f);
            ImGui::PushStyleColor(ImGuiCol_Text, sk.color(Role::Title));
            ImGui::TextUnformatted("UUEPBS");
            ImGui::PopStyleColor();
            ImGui::PopFont();
        }
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(muted(), "%s", m_version.c_str());

        std::string message;
        {
            std::lock_guard guard(m_message_lock);
            if (Clock::now() - m_message_time < std::chrono::seconds(6))
            {
                message = m_message;
            }
        }

        const ImGuiStyle& style = ImGui::GetStyle();
        const float box = ImGui::GetFrameHeight() + ImGui::CalcTextSize("Enabled").x + style.ItemInnerSpacing.x;
        const float badge = ImGui::GetFontSize() * 0.44f + style.ItemInnerSpacing.x + ImGui::CalcTextSize("No character").x;
        const float right = box + badge + style.ItemSpacing.x * 2.0f;
        ImGui::SameLine();
        const float msg_room = ImGui::GetContentRegionAvail().x - right;
        if (!message.empty() && msg_room > ImGui::GetFontSize() * 3.0f)
        {
            // Latest action ("Loaded 'Curvy'...") between the title and the badge, cut to fit.
            ImGui::AlignTextToFramePadding();
            const ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::PushClipRect(p, ImVec2(p.x + msg_room, p.y + ImGui::GetFrameHeight()), true);
            ImGui::TextColored(accent(), "%s", message.c_str());
            ImGui::PopClipRect();
            tooltip(message.c_str());
            ImGui::SameLine();
        }
        ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - right + style.ItemSpacing.x);
        draw_live_badge();
        ImGui::SameLine();
        ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - box);
        bool on = Registry::instance().enabled();
        if (sk.checkbox("Enabled", &on))
        {
            Registry::instance().set_enabled(on);
        }
        tooltip("Turn every slider on or off at once (nothing is lost).");

        draw_target_picker();
        ImGui::Spacing();
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

    void PanelView::draw_target_picker()
    {
        if (!m_targets)
        {
            return;
        }
        const ImGuiStyle& style = ImGui::GetStyle();
        const float refresh_w = compact_button_width("Refresh", "refresh");
        const float rename_w = compact_button_width("Rename", "rename");
        ImGui::SetNextItemWidth(std::max(120.0f, ImGui::GetContentRegionAvail().x - refresh_w - rename_w - style.ItemSpacing.x * 2));
        const std::string shown =
            m_target.label.empty() ? std::string("(searching...)") : CharacterNames::instance().shown(m_target.label, m_target.key, identity_of(m_target.key));
        const bool was_open = m_target_combo_open;
        m_target_combo_open = ImGui::BeginCombo("##target", shown.c_str(), ImGuiComboFlags_HeightLarge);
        if (m_target_combo_open && !was_open)
        {
            // Just opened: ask for the list sorted by the current distances (cheap; Refresh searches the world).
            m_target_combo_since = Clock::now();
            m_targets->relist_targets();
        }
        if (m_target_combo_open)
        {
            const std::string auto_label = std::string("Automatic (player / game default)") + (is_edited(kPlayerActor) ? "   - edited" : "");
            if (ImGui::Selectable(auto_label.c_str(), m_target.id == "auto"))
            {
                m_targets->pick_target("auto");
                set_message("Switching to the default character");
            }
            if (!m_target_list.empty())
            {
                ImGui::Separator();
            }
            for (const TargetChoice& t : m_target_list)
            {
                ImGui::PushID(t.id.c_str());
                const bool edited = is_edited(t.key);
                if (edited)
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, accent());
                }
                const std::string name = CharacterNames::instance().shown(t.label, t.key, identity_of(t.key));
                const std::string label = name + (edited ? "   - edited" : "");
                if (ImGui::Selectable(label.c_str(), t.id == m_target.id))
                {
                    m_targets->pick_target(t.id);
                    set_message("Switching to " + name);
                }
                if (name != t.label || !t.engine.empty())
                {
                    std::string tip = name != t.label ? "Game name: " + t.label : std::string();
                    tip += t.engine.empty() ? "" : (tip.empty() ? "" : "\n") + std::string("Engine name: ") + t.engine;
                    tooltip(tip.c_str());
                }
                if (edited)
                {
                    ImGui::PopStyleColor();
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        {
            std::string tip = "Whose body the sliders edit. The list shows characters currently loaded in the level, nearest first.\n"
                              "Characters you have edited are always listed and keep their sliders while you edit someone else.";
            const std::string name = CharacterNames::instance().shown(m_target.label, m_target.key, identity_of(m_target.key));
            if (name != m_target.label && !m_target.label.empty())
            {
                tip += "\n\nGame name: " + m_target.label;
            }
            if (!m_target.engine.empty())
            {
                tip += (name != m_target.label ? "\n" : "\n\n") + std::string("Engine name: ") + m_target.engine;
            }
            std::string others;
            for (const ActorSummary& a : m_edited)
            {
                if (!a.active)
                {
                    others += (others.empty() ? "" : ", ") + CharacterNames::instance().shown(a.label, a.key, a.identity);
                }
            }
            if (!others.empty())
            {
                tip += "\n\nAlso keeping sliders on: " + others;
            }
            tooltip(tip.c_str());
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(m_target.label.empty());
        if (compact_button("Rename", "rename"))
        {
            const std::string identity = identity_of(m_target.key);
            m_rename_key = CharacterNames::key_for(m_target.key, identity, &m_rename_stable);
            m_rename_label = m_target.label;
            std::snprintf(m_rename_buf, sizeof(m_rename_buf), "%s", CharacterNames::instance().get(m_rename_key).c_str());
            ImGui::OpenPopup("Rename character");
        }
        ImGui::EndDisabled();
        tooltip("Give this character your own name. It is only shown in this window and saved next to your presets.");
        draw_rename_popup();
        ImGui::SameLine();
        if (compact_button("Refresh", "refresh"))
        {
            m_targets->refresh_targets();
            m_targets->request_rescan();
            set_message("Looking for characters...");
        }
        {
            const std::string key = m_targets->refresh_hotkey_text();
            const std::string tip = "Rescan the character and its meshes, relist the characters and retry the pose hook." +
                                    (key.empty() ? std::string() : "\nHotkey: " + key + " (works in game too).");
            tooltip(tip.c_str());
        }
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
        ImGui::SeparatorText("Edited characters");
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
        return out;
    }

    void PanelView::draw_category_rail(const std::vector<Category>& cats, float width)
    {
        Skin& sk = skin();
        sk.begin_child("##categories", ImVec2(width, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
        const float row_h = ImGui::GetFrameHeight() * 1.45f;
        const float icon_w = ImGui::GetFontSize();
        for (const Category& c : cats)
        {
            ImGui::PushID(c.name.c_str());
            const bool picked = c.name == m_category;
            const ImVec2 p = ImGui::GetCursorScreenPos();
            if (ImGui::Selectable("##cat", picked, ImGuiSelectableFlags_None, ImVec2(0, row_h)))
            {
                m_category = c.name;
            }
            const ImVec2 q = ImGui::GetItemRectMax();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            float x = p.x + ImGui::GetStyle().FramePadding.x * 0.5f;
            const float text_y = p.y + (row_h - ImGui::GetFontSize()) * 0.5f;
            if (picked)
            {
                dl->AddRectFilled(ImVec2(p.x, p.y + row_h * 0.18f), ImVec2(p.x + 3.0f, q.y - row_h * 0.18f), ImGui::GetColorU32(accent()), 2.0f);
            }
            if (sk.has_icon("section." + c.name))
            {
                sk.draw_image(dl, "icon." + fold_case("section." + c.name), ImVec2(x + 4.0f, text_y), ImVec2(x + 4.0f + icon_w, text_y + icon_w));
                x += icon_w + ImGui::GetStyle().ItemInnerSpacing.x;
            }
            const ImVec4& tc = picked ? sk.color(Role::Title) : ImGui::GetStyleColorVec4(ImGuiCol_Text);
            dl->AddText(ImVec2(x + 6.0f, text_y), ImGui::GetColorU32(tc), c.name.c_str());
            if (c.changed > 0)
            {
                // Pill with the number of changed parts, right-aligned.
                char n[16];
                std::snprintf(n, sizeof(n), "%zu", c.changed);
                const ImVec2 ts = ImGui::CalcTextSize(n);
                const float ph = ImGui::GetFontSize() * 1.05f;
                const float pw = std::max(ph, ts.x + ph * 0.6f);
                const ImVec2 a(q.x - pw - 4.0f, p.y + (row_h - ph) * 0.5f);
                dl->AddRectFilled(a, ImVec2(a.x + pw, a.y + ph), ImGui::GetColorU32(accent_dim()), ph * 0.5f);
                dl->AddText(ImVec2(a.x + (pw - ts.x) * 0.5f, a.y + (ph - ts.y) * 0.5f), ImGui::GetColorU32(sk.color(Role::Text)), n);
            }
            std::string tip = std::to_string(c.parts) + (c.parts == 1 ? " part" : " parts");
            if (c.changed)
            {
                tip += ", " + std::to_string(c.changed) + " changed";
            }
            tooltip(tip.c_str());
            ImGui::PopID();
        }
        sk.end_child();
    }

    int PanelView::part_header(const std::string& title, const std::string& icon_key, bool edited, const char* value, bool value_muted,
                               const std::string& tip, const char* button1, const char* icon1, bool button1_on, bool button2_enabled)
    {
        Skin& sk = skin();
        const ImGuiStyle& style = ImGui::GetStyle();
        int pressed = 0;
        sk.icon(icon_key, ImGui::GetFontSize());
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(edited ? accent() : ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s", title.c_str());
        tooltip(tip.c_str());

        const float b1 = button1 ? compact_button_width(button1, icon1) + style.ItemSpacing.x : 0.0f;
        const float b2 = compact_button_width("Reset", "reset");
        ImGui::PushFont(nullptr, style.FontSizeBase * 1.08f);
        const float vw = ImGui::CalcTextSize(value).x;
        ImGui::PopFont();
        ImGui::SameLine();
        const float end = ImGui::GetWindowContentRegionMax().x;
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), end - b2 - b1 - vw - style.ItemSpacing.x * 1.5f));
        ImGui::AlignTextToFramePadding();
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.08f);
        ImGui::TextColored(value_muted ? muted() : edited ? sk.color(Role::Title) : ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s", value);
        ImGui::PopFont();
        tooltip("Ctrl+click the slider to type a value. Ctrl+wheel nudges it (add Shift for finer steps).");
        if (button1)
        {
            ImGui::SameLine(0.0f, style.ItemSpacing.x * 1.5f);
            if (button1_on)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, accent_dim());
            }
            if (compact_button(button1, icon1))
            {
                pressed = 1;
            }
            if (button1_on)
            {
                ImGui::PopStyleColor();
            }
            tooltip("Edit length / width / depth separately");
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!button2_enabled);
        if (compact_button("Reset", "reset"))
        {
            pressed = 2;
        }
        ImGui::EndDisabled();
        tooltip("Back to the game's own shape");
        return pressed;
    }

    namespace
    {
        // Slimmer track for the main sliders; the value is printed above it, so the grab never hides it.
        struct SlimSlider
        {
            SlimSlider()
            {
                const ImGuiStyle& s = ImGui::GetStyle();
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(s.FramePadding.x, std::max(1.0f, s.FramePadding.y * 0.45f)));
                ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, std::max(s.GrabMinSize, ImGui::GetFontSize() * 0.9f));
            }
            ~SlimSlider() { ImGui::PopStyleVar(2); }
        };

        // Tick at the slider's neutral point (1.0x sits in the middle of the log scale).
        void neutral_tick(float t)
        {
            const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
            const float grab = std::max(ImGui::GetStyle().GrabMinSize, ImGui::GetFontSize() * 0.9f);
            const float x = a.x + 2.0f + grab * 0.5f + (b.x - a.x - 4.0f - grab) * t;
            ImGui::GetWindowDrawList()->AddLine(ImVec2(x, b.y + 1.0f), ImVec2(x, b.y + 4.0f), ImGui::GetColorU32(muted()), 1.5f);
        }
    } // namespace

    void PanelView::draw_category(const Category& cat)
    {
        Skin& sk = skin();
        const ImGuiStyle& style = ImGui::GetStyle();
        // Category title + summary + "Reset category".
        ImGui::PushFont(nullptr, style.FontSizeBase * 1.3f);
        ImGui::TextColored(sk.color(Role::Title), "%s", cat.name.c_str());
        ImGui::PopFont();
        ImGui::SameLine();
        ImGui::TextColored(muted(), cat.changed ? "%zu of %zu changed" : "%zu parts", cat.changed ? cat.changed : cat.parts, cat.parts);
        ImGui::SameLine();
        const float rw = sk.button_width("Reset all", "reset");
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - rw));
        ImGui::BeginDisabled(cat.changed == 0);
        const bool reset_all = sk.button("Reset all", "reset");
        ImGui::EndDisabled();
        tooltip("Reset every part in this category");
        ImGui::Separator();
        ImGui::Spacing();

        for (const GroupBones& gb : m_body.groups)
        {
            const BodyGroup& g = gb.group;
            if (g.section != cat.name)
            {
                continue;
            }
            ImGui::PushID(g.title.c_str());
            BoneEdit s = scale_of(gb.bones.front(), g.spread);
            const bool edited = s.has_scale();
            if (reset_all && edited)
            {
                const double one[3] = {1.0, 1.0, 1.0};
                write_group(gb, one);
                s = scale_of(gb.bones.front(), g.spread);
            }
            std::string tip = g.hint + "\nBones:";
            for (const std::string& b : gb.bones)
            {
                tip += " " + b;
            }
            char value[32];
            const double avg = (s.axis[0] + s.axis[1] + s.axis[2]) / 3.0;
            std::snprintf(value, sizeof(value), "%.2f\xC3\x97", avg); // "1.30x" with a real multiplication sign
            bool& open = m_group_open[g.title];
            switch (part_header(g.title, "group." + g.title, s.has_scale(), value, !s.has_scale(), tip, "XYZ", "xyz", open, s.has_scale()))
            {
            case 1: open = !open; break;
            case 2:
            {
                const double one[3] = {1.0, 1.0, 1.0};
                write_group(gb, one);
                s = scale_of(gb.bones.front(), g.spread);
                break;
            }
            default: break;
            }
            {
                SlimSlider slim;
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (size_slider("##size", s, kScaleMin, kScaleMax, ""))
                {
                    write_group(gb, s.axis);
                }
                neutral_tick(0.5f);
            }
            if (open)
            {
                ImGui::Indent(ImGui::GetFontSize());
                if (axis_sliders(s))
                {
                    write_group(gb, s.axis);
                }
                ImGui::Unindent(ImGui::GetFontSize());
            }
            ImGui::Dummy(ImVec2(0, style.ItemSpacing.y * 0.6f));
            ImGui::PopID();
        }

        for (const MorphGroupMorphs& mg : m_morph_groups)
        {
            if ((mg.group.section.empty() ? std::string("Morphs") : mg.group.section) != cat.name)
            {
                continue;
            }
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
            if (reset_all && current)
            {
                for (const std::string& m : mg.morphs)
                {
                    write_morph(m, std::nullopt);
                }
                current.reset();
            }
            std::string tip = mg.group.hint;
            tip += tip.empty() ? "Morphs:" : "\nMorphs:";
            for (const std::string& m : mg.morphs)
            {
                tip += " " + m;
            }
            char value[32];
            if (current)
            {
                std::snprintf(value, sizeof(value), "%.0f%%", *current * 100.0);
            }
            else
            {
                std::snprintf(value, sizeof(value), "game");
            }
            if (part_header(mg.group.title, "group." + mg.group.title, current.has_value(), value, !current, tip, nullptr, nullptr, false,
                            current.has_value()) == 2)
            {
                for (const std::string& m : mg.morphs)
                {
                    write_morph(m, std::nullopt);
                }
                current.reset();
            }
            {
                SlimSlider slim;
                ImGui::SetNextItemWidth(-FLT_MIN);
                float v = static_cast<float>(current.value_or(0.0));
                if (morph_slider("##w", v, ""))
                {
                    for (const std::string& m : mg.morphs)
                    {
                        write_morph(m, v);
                    }
                }
            }
            ImGui::Dummy(ImVec2(0, style.ItemSpacing.y * 0.6f));
            ImGui::PopID();
        }
    }

    void PanelView::draw_simplified()
    {
        Skin& sk = skin();
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
            rail_w = std::max(rail_w, ImGui::CalcTextSize(c.name.c_str()).x + (sk.has_icon("section." + c.name) ? ImGui::GetFontSize() * 1.4f : 0.0f));
        }
        rail_w = std::min(rail_w + ImGui::GetFontSize() * 3.2f, ImGui::GetContentRegionAvail().x * 0.36f);

        const float footer = ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
        sk.begin_child("##body", ImVec2(0, -footer), ImGuiChildFlags_None);
        draw_category_rail(cats, rail_w);
        ImGui::SameLine();
        sk.begin_child("##parts", ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
        for (const Category& c : cats)
        {
            if (c.name == m_category)
            {
                draw_category(c);
            }
        }
        if (m_morph_groups.empty() && !m_morph_names.empty())
        {
            ImGui::Spacing();
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(muted(), "%zu morph target%s: see Advanced > Morphs. Add \"MorphGroups\" to the game profile to put sliders here.",
                               m_morph_names.size(), m_morph_names.size() == 1 ? "" : "s");
            ImGui::PopTextWrapPos();
        }
        sk.end_child();
        sk.end_child();
        ImGui::TextColored(muted(), "Ctrl+click a slider to type a value. Ctrl+wheel nudges it (add Shift for finer steps).");
    }

    void PanelView::draw_detailed()
    {
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
            ImGui::TextColored(muted(), "+ %s", mirror.c_str());
        }

        BoneEdit s = scale_of(m_selected, Spread::Chain);
        bool changed = false;
        sk.begin_child("##editor", ImVec2(0, 0), ImGuiChildFlags_None);
        if (ImGui::CollapsingHeader(s.has_scale() ? "Scale  *###scale" : "Scale###scale", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::SetNextItemWidth(-ImGui::CalcTextSize("Length (X)").x - ImGui::GetStyle().ItemInnerSpacing.x * 2);
            changed |= size_slider("Size", s, kScaleMin, kScaleMax);
            changed |= axis_sliders(s);
            if (s.has_scale() && sk.small_button("Reset scale"))
            {
                s.axis[0] = s.axis[1] = s.axis[2] = 1.0;
                changed = true;
            }
        }
        if (ImGui::CollapsingHeader(s.has_turn() ? "Rotate  *###rotate" : "Rotate###rotate", s.has_turn() ? ImGuiTreeNodeFlags_DefaultOpen : 0))
        {
            changed |= turn_sliders(s);
            if (s.has_turn() && sk.small_button("Reset rotation"))
            {
                s.turn[0] = s.turn[1] = s.turn[2] = 0.0;
                changed = true;
            }
        }
        if (ImGui::CollapsingHeader(s.has_shift() ? "Move  *###move" : "Move###move", s.has_shift() ? ImGuiTreeNodeFlags_DefaultOpen : 0))
        {
            changed |= shift_sliders(s);
            if (s.has_shift() && sk.small_button("Reset move"))
            {
                s.shift[0] = s.shift[1] = s.shift[2] = 0.0;
                changed = true;
            }
        }
        ImGui::Spacing();

        int spread = static_cast<int>(s.spread);
        const char* spreads[3] = {spread_label(Spread::Chain), spread_label(Spread::Keep), spread_label(Spread::Solo)};
        ImGui::SetNextItemWidth(-ImGui::CalcTextSize("Length (X)").x - ImGui::GetStyle().ItemInnerSpacing.x * 2);
        if (ImGui::Combo("Children", &spread, spreads, 3))
        {
            s.spread = static_cast<Spread>(spread);
            changed = true; // remembered as a hint even while the bone is still at 1.0x
        }
        tooltip("Scale children too: children follow the bone's scale, rotation and move.\n"
                "Keep children size: children follow rotation/move and the new shape but keep their own size.\n"
                "This bone only: nothing below it changes (rotation/move included).");

        if (can_mirror)
        {
            sk.checkbox("Mirror to other side", &m_mirror);
            ImGui::SameLine();
        }
        if (sk.button("Reset bone", "reset"))
        {
            BoneEdit neutral;
            neutral.spread = s.spread;
            s = neutral;
            changed = true;
        }

        ImGui::TextColored(muted(), "Ctrl+click to type a value. Ctrl+wheel nudges (Shift: finer).");
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
        const float reset_w = sk.button_width("Reset", "reset");
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
                continue;
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
        name_w = std::min(name_w + warn_w, ImGui::GetContentRegionAvail().x * 0.45f) + ImGui::GetStyle().ItemSpacing.x;

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
                ImGui::AlignTextToFramePadding();
                if (w)
                {
                    ImGui::TextColored(accent(), "%s", name.c_str());
                }
                else
                {
                    ImGui::TextUnformatted(name.c_str());
                }
                if (animated)
                {
                    ImGui::SameLine(0.0f, 0.0f);
                    ImGui::TextColored(warn(), " !");
                    tooltip("The game keeps setting this morph itself (animation or its own code),\nso the slider may not stick. UUEPBS re-applies it each time it notices.");
                }
                ImGui::SameLine(name_w);
                ImGui::SetNextItemWidth(std::max(80.0f, ImGui::GetContentRegionAvail().x - reset_w - ImGui::GetStyle().ItemSpacing.x));
                float v = static_cast<float>(w.value_or(0.0));
                if (morph_slider("##w", v))
                {
                    write_morph(name, v);
                    const std::string other = mirror_bone_name(name);
                    if (m_mirror && !other.empty() && has_morph(other))
                    {
                        write_morph(other, v);
                    }
                }
                ImGui::SameLine();
                ImGui::BeginDisabled(!w);
                if (sk.button("Reset", "reset"))
                {
                    write_morph(name, std::nullopt);
                    const std::string other = mirror_bone_name(name);
                    if (m_mirror && !other.empty() && has_morph(other))
                    {
                        write_morph(other, std::nullopt);
                    }
                }
                ImGui::EndDisabled();
                ImGui::PopID();
            }
        }
        if (rows.empty())
        {
            ImGui::TextColored(muted(), m_morph_only_edited ? "No morph is set yet." : "No morph matches the search.");
        }
        sk.end_child();

        sk.checkbox("Mirror left/right morphs", &m_mirror);
        tooltip("Moving a _L morph also moves its _R twin (and the other way round).");
        ImGui::SameLine();
        ImGui::BeginDisabled(m_morphs.empty());
        if (sk.button("Reset all morphs", "reset"))
        {
            Registry::instance().clear_morphs();
            m_morphs.clear();
            m_morph_revision = Registry::instance().morph_revision();
            set_message("All morphs given back to the game");
        }
        ImGui::EndDisabled();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(muted(), "A set morph replaces the game's value (even at 0); Reset gives it back. Ctrl+click to type values outside 0..1.");
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
            Registry::instance().replace_edits(std::move(book));
            Registry::instance().replace_morphs(std::move(morphs));
            std::snprintf(m_preset_name, sizeof(m_preset_name), "%s", name.c_str());
        }
        set_message(message);
    }

    void PanelView::save_preset(const std::string& name)
    {
        std::string message;
        if (m_shelf)
        {
            const MorphBook morphs = Registry::instance().morphs();
            m_shelf->save(name, Registry::instance().edits(), message, &morphs);
        }
        set_message(message);
        refresh_presets();
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

        ImGui::SeparatorText("Save current sliders");
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

        ImGui::SeparatorText("Saved presets");
        const float list_h = std::max(120.0f, ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing() * 4.5f);
        sk.begin_child("##presetlist", ImVec2(0, list_h), ImGuiChildFlags_Borders);
        if (m_presets.empty())
        {
            ImGui::TextColored(muted(), "No presets yet.");
        }
        for (const std::string& name : m_presets)
        {
            if (ImGui::Selectable(name.c_str(), m_picked == name, ImGuiSelectableFlags_AllowDoubleClick))
            {
                m_picked = name;
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                {
                    load_preset(name);
                }
            }
        }
        sk.end_child();

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
            ImGui::Text("Delete '%s' from disk?", m_pending.c_str());
            if (sk.button("Delete"))
            {
                std::string message;
                m_shelf->remove(m_pending, message);
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

        ImGui::Spacing();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(muted(), "Folder: %s", path_to_utf8(m_shelf->folder()).c_str());
        ImGui::PopTextWrapPos();
    }

    void PanelView::draw_skin_picker()
    {
        Skin& sk = skin();
        ImGui::SeparatorText("Window skin");
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
                           m_morphs_hidden ? (" (+" + std::to_string(m_morphs_hidden) + " hidden by ExcludeMorphs)").c_str() : "", m_morphs.size(),
                           m_morph_status.c_str());
        const std::string renderer = m_host->renderer_text();
        if (!renderer.empty())
        {
            ImGui::TextColored(muted(), "Window drawing: %s", renderer.c_str());
        }
        ImGui::PopTextWrapPos();

        draw_edited_characters();
        draw_skin_picker();

        ImGui::SeparatorText("Tracked meshes");
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
} // namespace uuepbs::ui
