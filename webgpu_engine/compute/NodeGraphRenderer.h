/*****************************************************************************
 * weBIGeo
 * Copyright (C) 2025 Patrick Komon
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

#include <string>
#include <unordered_map>

#include "compute/nodes/Node.h"
#include "compute/nodes/NodeGraph.h"

namespace webgpu_engine::compute {

class NodeRenderer;

class NodeGraphRenderer {
public:
    NodeGraphRenderer() = default;

    void init(nodes::NodeGraph& m_node_graph);

    void render();

private:
    nodes::NodeGraph* m_node_graph;

    std::unordered_map<std::string, std::unique_ptr<NodeRenderer>> m_node_renderers;
    std::unordered_map<const nodes::Node*, const NodeRenderer*> m_node_renderers_by_node;
};

class NodeRenderer {
public:
    NodeRenderer(const std::string& name, nodes::Node& node);
    virtual ~NodeRenderer() = default;

    void render();

    void render_sockets();

    virtual void render_settings();

    int get_input_socket_id(const std::string& input_socket_name) const;
    int get_output_socket_id(const std::string& output_socket_name) const;

private:
    std::string m_name;
    nodes::Node* m_node;
    int m_node_id;
    std::vector<int> m_input_socket_ids;
    std::vector<int> m_output_socket_ids;
};

class SelectTilesNodeRenderer : public NodeRenderer {
public:
    // void render() override;
};

} // namespace webgpu_engine::compute
