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

#include "TileExportNodeRenderer.h"

#include "../nodes/TileExportNode.h"
#include <ImGuiFileDialog.h>
#include <imgui.h>

namespace webgpu_engine::compute {

TileExportNodeRenderer::TileExportNodeRenderer(const std::string& name, nodes::TileExportNode& node)
    : NodeRenderer(name, node)
    , m_node(&node)
{
}

void TileExportNodeRenderer::render_settings_content()
{
    auto settings = m_node->get_settings();
    bool changed = false;

    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(255, 255, 255, 30));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(255, 255, 255, 60));
    if (ImGui::SmallButton(settings.output_directory.c_str())) {
        IGFD::FileDialogConfig config;
        config.path = settings.output_directory;
        ImGuiFileDialog::Instance()->OpenDialog("TileExportDirDialog", "Select Output Directory", nullptr, config);
    }
    ImGui::PopStyleColor(3);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", settings.output_directory.c_str());

}

void TileExportNodeRenderer::render_dialogs()
{
    constexpr ImVec2 dialog_size { 600, 400 };
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos({ center.x - dialog_size.x * 0.5f, center.y - dialog_size.y * 0.5f }, ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(dialog_size, ImGuiCond_Appearing);
    if (ImGuiFileDialog::Instance()->Display("TileExportDirDialog")) {
        if (ImGuiFileDialog::Instance()->IsOk()) {
            auto settings = m_node->get_settings();
            settings.output_directory = ImGuiFileDialog::Instance()->GetCurrentPath();
            m_node->set_settings(settings);
        }
        ImGuiFileDialog::Instance()->Close();
    }
}

} // namespace webgpu_engine::compute
