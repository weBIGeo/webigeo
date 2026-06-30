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

#include "TextureToRgba8NodeRenderer.h"

#include <imgui.h>
#include <webgpu/compute/nodes/TextureToRgba8Node.h>

namespace webgpu_app {
namespace nodes = webgpu_compute::nodes;

TextureToRgba8NodeRenderer::TextureToRgba8NodeRenderer(const std::string& name, nodes::TextureToRgba8Node& node)
    : NodeRenderer(name, node)
    , m_node(&node)
{
}

void TextureToRgba8NodeRenderer::render_settings_content()
{
    auto s = m_node->get_settings();
    bool changed = false;

    ImGui::TextUnformatted("Value range:");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::DragFloatRange2("##range", &s.value_range.x, &s.value_range.y, 1.0f,
            -1e9f, 1e9f, "Min: %.3f", "Max: %.3f")) {
        changed = true;
    }

    if (changed) {
        m_node->set_settings(s);
        m_node->rerun();
    }
}

} // namespace webgpu_app
