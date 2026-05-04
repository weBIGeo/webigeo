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

#include "BufferExportNodeRenderer.h"

#include "../nodes/BufferExportNode.h"
#include <ImGuiFileDialog.h>
#include <imgui.h>

namespace webgpu_engine::compute {

static std::string short_path(const std::string& path)
{
    const auto pos = path.find_last_of("/\\");
    if (pos == std::string::npos)
        return path;
    return "...\\" + path.substr(pos + 1);
}

BufferExportNodeRenderer::BufferExportNodeRenderer(const std::string& name, nodes::BufferExportNode& node)
    : NodeRenderer(name, node)
    , m_node(&node)
{
}

void BufferExportNodeRenderer::render_settings_content()
{
    auto settings = m_node->get_settings();

    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(255, 255, 255, 30));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(255, 255, 255, 60));
    if (ImGui::SmallButton(short_path(settings.output_file).c_str())) {
        const auto dir_end = settings.output_file.find_last_of("/\\");
        IGFD::FileDialogConfig config;
        config.path = (dir_end != std::string::npos) ? settings.output_file.substr(0, dir_end) : m_last_dialog_directory;
        config.fileName = (dir_end != std::string::npos) ? settings.output_file.substr(dir_end + 1) : settings.output_file;
        ImGuiFileDialog::Instance()->OpenDialog("BufferExportSaveDialog", "Save Buffer As", ".png", config);
    }
    ImGui::PopStyleColor(3);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", settings.output_file.c_str());

}

void BufferExportNodeRenderer::render_dialogs()
{
    auto settings = m_node->get_settings();
    constexpr ImVec2 dialog_size { 600, 400 };
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos({ center.x - dialog_size.x * 0.5f, center.y - dialog_size.y * 0.5f }, ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(dialog_size, ImGuiCond_Appearing);
    if (ImGuiFileDialog::Instance()->Display("BufferExportSaveDialog")) {
        if (ImGuiFileDialog::Instance()->IsOk()) {
            m_last_dialog_directory = ImGuiFileDialog::Instance()->GetCurrentPath();
            settings.output_file = ImGuiFileDialog::Instance()->GetFilePathName();
            m_node->set_settings(settings);
        }
        ImGuiFileDialog::Instance()->Close();
    }
}

} // namespace webgpu_engine::compute
