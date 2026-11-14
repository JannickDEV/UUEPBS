#include "portraits.hpp"

#include "../core/presets.hpp"

#include <imgui_internal.h> // RegisterUserTexture / UnregisterUserTexture

#include <algorithm>
#include <cstring>
#include <system_error>

namespace fs = std::filesystem;

namespace uuepbs::ui
{
    Portraits& portraits()
    {
        static Portraits p;
        return p;
    }

    namespace
    {
        std::string lower(std::string s)
        {
            for (char& c : s)
            {
                if (c >= 'A' && c <= 'Z')
                {
                    c = static_cast<char>(c - 'A' + 'a');
                }
            }
            return s;
        }

        ImTextureData* make_texture(const Rgba8Image& img)
        {
            auto* tex = IM_NEW(ImTextureData)();
            tex->Create(ImTextureFormat_RGBA32, img.width, img.height);
            std::memcpy(tex->Pixels, img.pixels.data(), img.pixels.size());
            tex->UseColors = true;
            tex->RefCount = 1;
            tex->UsedRect = {0, 0, static_cast<unsigned short>(img.width), static_cast<unsigned short>(img.height)};
            tex->SetStatus(ImTextureStatus_WantCreate);
            ImGui::RegisterUserTexture(tex);
            return tex;
        }
    } // namespace

    std::string Portraits::file_key(const std::string& name)
    {
        return lower(PresetShelf::clean_name(name));
    }

    void Portraits::set_folder(fs::path folder)
    {
        std::lock_guard guard(m_folder_lock);
        m_folder = std::move(folder);
    }

    fs::path Portraits::folder() const
    {
        std::lock_guard guard(m_folder_lock);
        return m_folder;
    }

    void Portraits::rescan()
    {
        const fs::path folder = this->folder();
        m_scanned_folder = folder;
        std::map<std::string, fs::path> files;
        std::error_code ec;
        if (!folder.empty() && fs::is_directory(folder, ec))
        {
            for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec))
            {
                if (!it->is_regular_file(ec))
                {
                    continue;
                }
                const std::string ext = lower(path_to_utf8(it->path().extension()));
                if (ext != ".png" && ext != ".jpg" && ext != ".jpeg")
                {
                    continue;
                }
                std::string stem = lower(path_to_utf8(it->path().stem()));
                const bool automatic = stem.size() > 5 && stem.compare(stem.size() - 5, 5, ".auto") == 0;
                if (automatic)
                {
                    stem.resize(stem.size() - 5);
                }
                const auto have = files.find(stem);
                if (have != files.end() && automatic)
                {
                    continue; // a hand-made image wins
                }
                if (have == files.end() || !automatic)
                {
                    files[stem] = it->path();
                }
            }
        }
        m_files = std::move(files);

        // Drop textures whose file went away or changed (they reload on the next find()).
        for (auto it = m_loaded.begin(); it != m_loaded.end();)
        {
            const auto f = m_files.find(it->first);
            std::error_code e2;
            const bool same = f != m_files.end() && f->second == it->second.file && fs::last_write_time(f->second, e2) == it->second.stamp && !e2;
            if (same)
            {
                ++it;
                continue;
            }
            retire(it->second.tex);
            it = m_loaded.erase(it);
        }
    }

    void Portraits::retire(ImTextureData* tex)
    {
        if (!tex)
        {
            return;
        }
        if (tex->Status == ImTextureStatus_WantCreate || tex->Status == ImTextureStatus_Destroyed)
        {
            ImGui::UnregisterUserTexture(tex); // never reached the backend
            IM_DELETE(tex);
            return;
        }
        tex->SetStatus(ImTextureStatus_WantDestroy);
        tex->UnusedFrames = 1;
        m_retired.push_back(tex);
    }

    void Portraits::update(double now)
    {
        if (now >= m_next_scan || folder() != m_scanned_folder)
        {
            m_next_scan = now + 2.0;
            rescan();
        }
        for (auto it = m_retired.begin(); it != m_retired.end();)
        {
            if ((*it)->Status == ImTextureStatus_Destroyed)
            {
                ImGui::UnregisterUserTexture(*it);
                IM_DELETE(*it);
                it = m_retired.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    ImTextureData* Portraits::find(const std::vector<std::string>& candidates)
    {
        for (const std::string& c : candidates)
        {
            const std::string key = file_key(c);
            if (key.empty())
            {
                continue;
            }
            const auto f = m_files.find(key);
            if (f == m_files.end())
            {
                continue;
            }
            if (const auto l = m_loaded.find(key); l != m_loaded.end())
            {
                if (l->second.tex)
                {
                    return l->second.tex;
                }
                continue; // could not be decoded
            }
            if (m_loaded.size() >= kMaxLoaded)
            {
                return nullptr;
            }
            Entry e;
            e.file = f->second;
            std::error_code ec;
            e.stamp = fs::last_write_time(e.file, ec);
            Rgba8Image img;
            std::string error;
            if (decode_image_file(e.file, img, error) && img.width > 0 && img.height > 0)
            {
                shrink_to_fit(img, kMaxSide);
                e.tex = make_texture(img);
            }
            else
            {
                e.failed = true;
            }
            ImTextureData* tex = e.tex;
            m_loaded[key] = e;
            if (tex)
            {
                return tex;
            }
        }
        return nullptr;
    }

    void Portraits::draw(ImDrawList* dl, ImTextureData* tex, ImVec2 a, ImVec2 b)
    {
        if (!tex || tex->Width <= 0 || tex->Height <= 0)
        {
            return;
        }
        // Cover: crop the longer side around the centre (portraits: keep the top, where faces are).
        const float w = b.x - a.x, h = b.y - a.y;
        const float iw = static_cast<float>(tex->Width), ih = static_cast<float>(tex->Height);
        const float s = std::max(w / iw, h / ih);
        const float uw = w / (iw * s), uh = h / (ih * s);
        const float u0 = (1.0f - uw) * 0.5f;
        const float v0 = (1.0f - uh) * 0.25f;
        dl->AddImage(tex->GetTexRef(), a, b, ImVec2(u0, v0), ImVec2(u0 + uw, v0 + uh));
    }

    void Portraits::shutdown()
    {
        for (auto& [key, e] : m_loaded)
        {
            if (e.tex)
            {
                ImGui::UnregisterUserTexture(e.tex);
                IM_DELETE(e.tex);
            }
        }
        for (ImTextureData* t : m_retired)
        {
            ImGui::UnregisterUserTexture(t);
            IM_DELETE(t);
        }
        m_loaded.clear();
        m_retired.clear();
        m_files.clear();
        m_next_scan = -1.0;
    }
} // namespace uuepbs::ui
