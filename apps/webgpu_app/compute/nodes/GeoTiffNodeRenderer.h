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

#pragma once

#include "NodeRenderer.h"
#include <array>
#include <string>
#include <vector>

namespace webgpu_compute::nodes {
class GeoTiffNode;
}

namespace webgpu_app {
namespace nodes = webgpu_compute::nodes;

class GeoTiffNodeRenderer : public NodeRenderer {
public:
    GeoTiffNodeRenderer(const std::string& name, nodes::GeoTiffNode& node);
    bool has_settings() const override { return true; }
    void render_settings_content() override;
    void render_dialogs() override;

private:
    nodes::GeoTiffNode* m_node;
    std::array<char, 512> m_path_buffer {};
    std::array<char, 64> m_crs_buffer {};
    std::string m_last_dialog_directory = ".";
    std::string m_dialog_id;
    std::vector<std::string> m_picked_files;
    bool m_want_open_dialog = false;
};

} // namespace webgpu_app
