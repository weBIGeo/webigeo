/*****************************************************************************
 * weBIGeo
 * Copyright (C) 2025 Gerald Kimmersdorfer
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

namespace webgpu_engine::compute::nodes {
class BufferExportNode;
}

namespace webgpu_engine::compute {

class BufferExportNodeRenderer : public NodeRenderer {
public:
    BufferExportNodeRenderer(const std::string& name, nodes::BufferExportNode& node);
    bool has_settings() const override { return true; }
    void render_settings_content() override;

private:
    nodes::BufferExportNode* m_node;
    std::string m_last_dialog_directory = ".";
};

} // namespace webgpu_engine::compute
