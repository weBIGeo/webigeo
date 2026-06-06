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

#include "SelectTilesNodeRenderer.h"

#include "../../nodes/SelectTilesNode.h"
#include <imgui.h>

namespace webgpu_engine::compute {

SelectTilesNodeRenderer::SelectTilesNodeRenderer(const std::string& name, nodes::SelectTilesNode& node)
    : NodeRenderer(name, node)
    , m_node(&node)
{
}

void SelectTilesNodeRenderer::render_settings_content()
{
    auto settings = m_node->get_settings();

    const uint32_t min_zoomlevel = 1;
    const uint32_t max_zoomlevel = 18;
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderScalar("Zoom level", ImGuiDataType_U32, &settings.zoomlevel, &min_zoomlevel, &max_zoomlevel, "%u");
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        m_node->set_settings(settings);
        m_node->rerun();
    }
}

} // namespace webgpu_engine::compute
