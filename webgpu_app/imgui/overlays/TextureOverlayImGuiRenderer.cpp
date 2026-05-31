/*****************************************************************************
 * weBIGeo
 * Copyright (C) 2026 Gerald Kimmersdorfer
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *****************************************************************************/

#include "TextureOverlayImGuiRenderer.h"

#include <ImGuiFileDialog.h>
#include <filesystem>
#include <imgui.h>

#include "webgpu_engine/renderer/OverlayRenderer.h"

namespace webgpu_app {

TextureOverlayImGuiRenderer::TextureOverlayImGuiRenderer(webgpu_engine::TextureOverlay& overlay)
    : OverlayImGuiRenderer(overlay)
    , m_texture_overlay(&overlay)
{
}

bool TextureOverlayImGuiRenderer::render_custom_settings()
{
    auto& s = m_texture_overlay->settings;
    bool changed = false;

    // Image path display
    if (m_loaded_image_path.empty())
        ImGui::TextDisabled("No image loaded");
    else
        ImGui::TextUnformatted(std::filesystem::path(m_loaded_image_path).filename().string().c_str());

    // File open button
    if (ImGui::Button("Load PNG...", ImVec2(-1, 0))) {
        IGFD::FileDialogConfig config;
        config.path = m_last_dialog_directory.empty() ? "." : m_last_dialog_directory;
        ImGuiFileDialog::Instance()->OpenDialog("TextureOverlayFileDialog", "Choose Image", ".png,.*", config);
    }

    // Handle dialog result
    if (ImGuiFileDialog::Instance()->Display("TextureOverlayFileDialog")) {
        if (ImGuiFileDialog::Instance()->IsOk()) {
            const std::string filename_str = ImGuiFileDialog::Instance()->GetFilePathName();
            const auto filename = std::filesystem::path(filename_str);
            m_last_dialog_directory = filename.parent_path().string();

            // Derive AABB file path using the same fallback logic as Window.cpp
            auto aabb_path = filename.parent_path() / (filename.stem().string() + "_aabb.txt");
            if (!std::filesystem::exists(aabb_path)) {
                const std::string stem = filename.stem().string();
                const size_t us = stem.find('_');
                const std::string trackname = (us != std::string::npos) ? stem.substr(0, us) : stem;
                aabb_path = filename.parent_path() / (trackname + "_aabb.txt");
            }
            if (!std::filesystem::exists(aabb_path))
                aabb_path = filename.parent_path() / "aabb.txt";

            if (std::filesystem::exists(aabb_path)) {
                const auto aabb_result = webgpu_engine::OverlayRenderer::load_aabb_from_file(aabb_path.string());
                if (aabb_result.has_value()) {
                    const auto& aabb = aabb_result.value();
                    m_texture_overlay->set_aabb(glm::dvec2(aabb.min.x, aabb.min.y), glm::dvec2(aabb.max.x, aabb.max.y));
                    m_texture_overlay->load_image(QString::fromStdString(filename_str));
                    m_loaded_image_path = filename_str;
                    changed = true;
                } else {
                    ImGui::OpenPopup("AabbError");
                }
            } else {
                ImGui::OpenPopup("AabbError");
            }
        }
        ImGuiFileDialog::Instance()->Close();
    }

    if (ImGui::BeginPopupModal("AabbError", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("No AABB file found for the selected image.");
        ImGui::Text("Expected: <stem>_aabb.txt or aabb.txt in the same directory.");
        if (ImGui::Button("OK"))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::Separator();

    if (ImGui::SliderFloat("Opacity", &s.opacity, 0.0f, 1.0f)) {
        m_texture_overlay->update_gpu_settings();
        changed = true;
    }

    const char* filter_items[] = { "Nearest", "Linear" };
    int filter_idx = (s.filter_mode == webgpu_engine::TextureOverlay::FilterMode::Linear) ? 1 : 0;
    if (ImGui::Combo("Filter Mode", &filter_idx, filter_items, 2))
        s.filter_mode = (filter_idx == 1) ? webgpu_engine::TextureOverlay::FilterMode::Linear
                                          : webgpu_engine::TextureOverlay::FilterMode::Nearest;

    ImGui::Checkbox("Use Mipmaps", &s.use_mipmaps);
    ImGui::SameLine();
    ImGui::TextDisabled("(takes effect on next image load)");

    return changed;
}

} // namespace webgpu_app
