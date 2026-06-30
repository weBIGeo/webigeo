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

#include "GeoTiffNodeRenderer.h"

#include <apps/webgpu_app/ImGuiManager.h>
#include <cstring>
#include <filesystem>
#include <imgui.h>
#include <webgpu/compute/nodes/GeoTiffNode.h>

namespace webgpu_app {
namespace nodes = webgpu_compute::nodes;

GeoTiffNodeRenderer::GeoTiffNodeRenderer(const std::string& name, nodes::GeoTiffNode& node)
    : NodeRenderer(name, node)
    , m_node(&node)
    , m_dialog_id("geotiffnode_" + std::to_string(get_node_id()))
{
    const auto& s = m_node->get_settings();
    std::strncpy(m_path_buffer.data(), s.file_path.c_str(), m_path_buffer.size() - 1);
    std::strncpy(m_crs_buffer.data(), s.target_crs.c_str(), m_crs_buffer.size() - 1);
}

void GeoTiffNodeRenderer::render_settings_content()
{
    auto settings = m_node->get_settings();

    // File path
    ImGui::TextUnformatted("File:");
    const float btn_w = ImGui::CalcTextSize("Browse...").x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SetNextItemWidth(-btn_w - ImGui::GetStyle().ItemSpacing.x);
    ImGui::InputText("##geotiff_path", m_path_buffer.data(), m_path_buffer.size());
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        settings.file_path = m_path_buffer.data();
        m_node->set_settings(settings);
        m_node->rerun();
    }
    ImGui::SameLine();
    m_want_open_dialog = ImGui::Button("Browse...");

    // Target CRS
    ImGui::TextUnformatted("Target CRS:");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##geotiff_crs", m_crs_buffer.data(), m_crs_buffer.size());
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        settings.target_crs = m_crs_buffer.data();
        m_node->set_settings(settings);
        m_node->rerun();
    }

    // Status
    const auto& info = m_node->load_info();
    if (info.loaded) {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextDisabled("CRS:   %s", info.src_crs.c_str());
        ImGui::TextDisabled("Size:  %d \xc3\x97 %d  \xc2\xb7  %d band%s  \xc2\xb7  %s",
            info.src_width, info.src_height,
            info.src_bands, info.src_bands == 1 ? "" : "s",
            info.src_data_type.c_str());
        ImGui::TextDisabled("GPU:   %u \xc3\x97 %u  (%s)", info.out_width, info.out_height, info.out_format.c_str());
    }
}

void GeoTiffNodeRenderer::render_dialogs()
{
    m_picked_files.clear();
    if (ImGuiManager::FilePicker(m_dialog_id.c_str(), "Choose GeoTIFF File", ".tif,.tiff,.geotiff,.*",
            m_want_open_dialog, m_picked_files, /*allow_multiple=*/false, m_last_dialog_directory.c_str())) {
        m_last_dialog_directory = std::filesystem::path(m_picked_files[0]).parent_path().string();
        auto s = m_node->get_settings();
        s.file_path = m_picked_files[0];
        std::strncpy(m_path_buffer.data(), s.file_path.c_str(), m_path_buffer.size() - 1);
        m_node->set_settings(s);
        m_node->rerun();
    }
}

} // namespace webgpu_app
