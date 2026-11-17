// UUEPBS - Looks tab: skin and hair, recoloured through the outfit's own material parameters.
//
// Same layout as the Body tab: categories on the left (Skin, Hair) with a short summary of what
// is set, the picked category's sliders on the right under a section bar with Reset. The values
// are "look:<id>" entries of the character's morph book (core/looks.hpp), so they are per
// character, saved with the session / NPC memory and in presets, like every other slider.
#include "../core/looks.hpp"
#include "panel_view.hpp"
#include "skin.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>

namespace uuepbs::ui
{
    namespace
    {
        float px(float v) { return v * ImGui::GetStyle().FontScaleDpi; }
        ImU32 col(const ImVec4& c) { return ImGui::GetColorU32(c); }
        const ImVec4& accent() { return skin().color(Role::Accent); }
        const ImVec4& accent_dim() { return skin().color(Role::AccentDim); }
        const ImVec4& muted() { return skin().color(Role::Muted); }

        void bullet(ImDrawList* dl, ImVec2 centre, float size, ImU32 c)
        {
            const float h = std::floor(size * 0.5f);
            dl->AddRectFilled(ImVec2(std::floor(centre.x) - h, std::floor(centre.y) - h), ImVec2(std::floor(centre.x) + h, std::floor(centre.y) + h), c);
        }

        void small_muted_wrapped(const char* text, float scale = 0.86f)
        {
            ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * scale);
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(muted(), "%s", text);
            ImGui::PopTextWrapPos();
            ImGui::PopFont();
        }
    } // namespace

    void PanelView::draw_looks_rail(float width)
    {
        Skin& sk = skin();
        const ImGuiStyle& style = ImGui::GetStyle();
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
        ImGui::BeginChild("##look_categories", ImVec2(width, 0), ImGuiChildFlags_None);
        ImGui::PopStyleColor();
        ImGui::PushFont(nullptr, style.FontSizeBase * 0.78f);
        ImGui::TextColored(muted(), "LOOKS");
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(0.0f, px(2.0f)));
        const float row_h = std::floor(ImGui::GetFrameHeight() * 1.25f);
        const float small = style.FontSizeBase * style.FontScaleDpi * 0.8f;
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(style.ItemSpacing.x, px(3.0f)));
        for (const looks::Category& c : looks::categories())
        {
            ImGui::PushID(c.name);
            const bool picked = m_look_category == c.name;
            const size_t changed = looks::changed(m_morphs, c.name);
            const ImVec2 a = ImGui::GetCursorScreenPos();
            const float w = ImGui::GetContentRegionAvail().x;
            if (ImGui::InvisibleButton("##cat", ImVec2(w, row_h)))
            {
                m_look_category = c.name;
            }
            const bool hovered = ImGui::IsItemHovered();
            const ImVec2 b(a.x + w, a.y + row_h);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            if (picked)
            {
                dl->AddRectFilled(a, b, col(accent_dim()));
                dl->AddRect(a, b, col(accent()), 0.0f, 0, 1.0f);
            }
            else if (hovered)
            {
                dl->AddRectFilled(a, b, col(ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered)));
            }
            float x = a.x + px(12.0f);
            const float cy = a.y + row_h * 0.5f;
            const std::string icon = "section." + std::string(c.name);
            if (sk.has_icon(icon))
            {
                const float ic = ImGui::GetFontSize();
                sk.draw_image(dl, "icon." + fold_case(icon), ImVec2(x, cy - ic * 0.5f), ImVec2(x + ic, cy + ic * 0.5f));
                x += ic + px(8.0f);
            }
            else
            {
                const float bs = std::max(3.0f, std::floor(ImGui::GetFontSize() * 0.26f));
                bullet(dl, ImVec2(x + bs * 0.5f, cy), bs, col(picked ? ImGui::GetStyleColorVec4(ImGuiCol_Text) : changed ? accent() : muted()));
                x += bs + px(9.0f);
            }
            dl->AddText(ImVec2(x, std::floor(cy - ImGui::GetFontSize() * 0.5f)), col(ImGui::GetStyleColorVec4(ImGuiCol_Text)), c.name);
            const std::string words = looks::summary(m_morphs, c.name);
            if (!words.empty())
            {
                const ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(small, FLT_MAX, 0.0f, words.c_str());
                dl->AddText(ImGui::GetFont(), small, ImVec2(b.x - ts.x - px(10.0f), std::floor(cy - ts.y * 0.5f)),
                            col(picked ? ImGui::GetStyleColorVec4(ImGuiCol_Text) : muted()), words.c_str());
            }
            ImGui::PopID();
        }
        ImGui::PopStyleVar();
        ImGui::Dummy(ImVec2(0.0f, px(10.0f)));
        small_muted_wrapped("Per character, like the sliders: saved with the session (NPCs: in their memory) and in presets. "
                            "Game models and custom bodies alike: the outfit's own materials are recoloured, nothing is replaced.");
        ImGui::EndChild();
        const ImVec2 r = ImGui::GetItemRectMax();
        const float x = r.x + ImGui::GetStyle().ItemSpacing.x;
        ImGui::GetWindowDrawList()->AddLine(ImVec2(x, ImGui::GetItemRectMin().y), ImVec2(x, r.y), col(sk.color(Role::Border)), 1.0f);
    }

    void PanelView::draw_looks_category(const char* category)
    {
        Skin& sk = skin();
        const ImGuiStyle& style = ImGui::GetStyle();
        const size_t changed = looks::changed(m_morphs, category);
        const bool reset_all = section_bar(category, "Reset", changed > 0);
        for (const looks::Category& c : looks::categories())
        {
            if (std::string_view(c.name) == category)
            {
                small_muted_wrapped(c.note);
            }
        }
        ImGui::Dummy(ImVec2(0.0f, px(4.0f)));

        float label_w = 0.0f;
        for (const looks::Control& c : looks::controls())
        {
            if (std::string_view(c.category) == category)
            {
                label_w = std::max(label_w, ImGui::CalcTextSize(c.title).x);
            }
        }
        label_w = std::min(label_w + ImGui::GetFontSize() * 1.6f, ImGui::GetContentRegionAvail().x * 0.34f);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(16.0f), px(14.0f)));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(style.ItemSpacing.x, px(12.0f)));
        sk.begin_child("##look_group", ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
        ImGui::PopStyleVar();
        for (const looks::Control& c : looks::controls())
        {
            if (std::string_view(c.category) != category)
            {
                continue;
            }
            const std::string key = looks::key_of(c.id);
            if (reset_all && looks::is_set(m_morphs, c))
            {
                write_morph(key, std::nullopt);
            }
            float v = looks::value_of(m_morphs, c);
            RowSpec spec;
            spec.id = c.id;
            spec.title = c.title;
            spec.tip = c.hint;
            spec.edited = looks::is_set(m_morphs, c);
            spec.lo = c.lo;
            spec.hi = c.hi;
            spec.neutral = c.neutral;
            spec.can_reset = spec.edited;
            spec.label_w = label_w;
            const RowResult r = part_row(spec, v);
            if (r.changed)
            {
                v = std::clamp(v, c.lo, c.hi);
                if (std::fabs(v - c.neutral) <= 1e-4f)
                {
                    write_morph(key, std::nullopt);
                }
                else
                {
                    write_morph(key, static_cast<double>(v));
                }
            }
            if (r.reset)
            {
                write_morph(key, std::nullopt);
            }
        }
        sk.end_child();
        ImGui::PopStyleVar();
    }

    void PanelView::draw_looks()
    {
        Skin& sk = skin();
        if (std::none_of(looks::categories().begin(), looks::categories().end(),
                         [this](const looks::Category& c) { return m_look_category == c.name; }))
        {
            m_look_category = looks::categories().front().name;
        }
        float rail_w = ImGui::CalcTextSize("Hair").x + ImGui::CalcTextSize("tan + sweat").x;
        rail_w = std::min(rail_w + ImGui::GetFontSize() * 3.5f, ImGui::GetContentRegionAvail().x * 0.29f);
        draw_looks_rail(rail_w);
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x * 2.0f + 1.0f);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
        ImGui::BeginChild("##look_parts", ImVec2(0, 0), ImGuiChildFlags_None);
        ImGui::PopStyleColor();
        draw_looks_category(m_look_category.c_str());

        // What the script did with it, under a 1px divider.
        ImGui::Dummy(ImVec2(0.0f, px(10.0f)));
        {
            const ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddLine(p, ImVec2(p.x + ImGui::GetContentRegionAvail().x, p.y), col(sk.color(Role::Border)), 1.0f);
            ImGui::Dummy(ImVec2(0.0f, px(10.0f)));
        }
        const size_t set = looks::count(m_morphs);
        std::string report = looks::Report::instance().get(m_target.key);
        if (report.empty())
        {
            report = set == 0 ? "Nothing changed: the outfit looks as the game made it."
                              : (Registry::instance().enabled() ? "Waiting for the game script to recolour the outfit..."
                                                                : "Sliders are off (Enabled at the top), so the game's own look is shown.");
        }
        small_muted_wrapped(report.c_str(), 0.9f);
        small_muted_wrapped("A game whose materials use other parameter names can name them in its game profile (\"Looks\", see the README). "
                            "The log lists the parameters each slot has.");
        ImGui::EndChild();
    }
} // namespace uuepbs::ui
