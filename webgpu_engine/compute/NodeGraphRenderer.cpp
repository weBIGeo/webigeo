/*****************************************************************************
 * weBIGeo
 * Copyright (C) 2025 Patrick Komon
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

#include "NodeGraphRenderer.h"

#include <imgui.h>
#include <imnodes.h>
#include <qDebug>

namespace webgpu_engine::compute {

static std::hash<std::string> hasher;

void NodeGraphRenderer::init(nodes::NodeGraph& node_graph)
{
    m_node_renderers.clear();
    m_node_renderers_by_node.clear();
    m_links.clear();

    m_node_graph = &node_graph;
    auto& nodes = m_node_graph->get_nodes();
    for (auto& [name, node] : nodes) {
        auto renderer = std::make_unique<NodeRenderer>(name, *node.get());
        m_node_renderers.emplace(name, std::move(renderer));
        m_node_renderers_by_node.emplace(node.get(), m_node_renderers.at(name).get());
    }

    // create links
    for (auto& [name, node_renderer] : m_node_renderers) {
        // get connected attribute index
        const auto& node = *nodes.at(name).get();
        const auto& input_sockets = node.input_sockets();
        for (size_t i = 0; i < input_sockets.size(); i++) {
            const auto& input_socket = input_sockets.at(i);
            if (input_socket.is_socket_connected()) {
                const auto first_attribute_index = node_renderer->get_input_socket_id(input_socket.name());

                // find connected node name and socket name
                const std::string& connected_socket_name = input_socket.connected_socket().name();
                const nodes::Node& connected_node = input_socket.connected_socket().node();
                const NodeRenderer* connected_node_renderer = m_node_renderers_by_node.at(&connected_node);

                const auto second_attribute_index = connected_node_renderer->get_output_socket_id(connected_socket_name);

                m_links.emplace_back(first_attribute_index, second_attribute_index);
            }
        }
    }

    // Layout the nodes in a grid-like structure
    init_layout();
}

void NodeGraphRenderer::init_layout()
{
    ImVec2 node_distance_scaling(270.0f, 300.0f);
    std::queue<std::pair<NodeRenderer*, int>> queue;

    // Step 1: Enqueue root nodes (those with no inputs)
    for (auto& [name, node_renderer] : m_node_renderers) {
        nodes::Node* node = node_renderer->get_node();
        // Root node = no input sockets
        if (node->input_sockets().empty()) {
            int x = 0;
            node_renderer->set_position(ImVec2(x, 0));
            queue.emplace(std::make_pair(node_renderer.get(), x));
        }
    }

    // Step 2: BFS-style traversal to place child nodes
    while (!queue.empty()) {
        auto [node_renderer, current_x] = queue.front();
        queue.pop();
        nodes::Node* current_node = node_renderer->get_node();

        const auto& output_sockets = current_node->output_sockets();
        for (const auto& output_socket : output_sockets) {
            auto connections = output_socket.connected_sockets();
            for (const auto& connection : connections) {
                nodes::Node* target_node = &connection->node();
                NodeRenderer* target_renderer = m_node_renderers_by_node[target_node];

                // NOTE: If we do this check it will mostly be vertically aligned as the
                // select_tiles_node is connected to almost all other nodes
                // if (target_renderer->has_position())
                //     continue;

                int x = current_x + 1;
                target_renderer->set_position(ImVec2(x, 0));
                queue.emplace(std::make_pair(target_renderer, x));
            }
        }
    }

    // Step 3: Go through all the nodes to determine y position based on how many share the same x position
    std::unordered_map<int, std::vector<NodeRenderer*>> x_count_map;
    for (auto& [name, node_renderer] : m_node_renderers) {
        ImVec2 pos = node_renderer->get_position();
        x_count_map[int(pos.x)].push_back(node_renderer.get());
    }
    for (auto& [x, renderers] : x_count_map) {
        int y = 1;
        for (auto& renderer : renderers) {
            renderer->set_position(ImVec2(x * node_distance_scaling.x, y * node_distance_scaling.y));
            y += 1;
        }
    }
}

void NodeGraphRenderer::render()
{
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);

    ImGui::Begin("node editor");

    ImNodes::BeginNodeEditor();

    // draw nodes
    for (auto& [name, node_renderer] : m_node_renderers) {
        node_renderer->render();
    }

    // draw links
    for (size_t i = 0; i < m_links.size(); ++i) {
        const std::pair<int, int> p = m_links[i];
        // in this case, we just use the array index of the link as the unique identifier
        ImNodes::Link(i, p.first, p.second);
    }

    ImNodes::MiniMap(0.1f, ImNodesMiniMapLocation_BottomRight);
    ImNodes::EndNodeEditor();

    ImGui::End();
}

NodeRenderer::NodeRenderer(const std::string& name, nodes::Node& node)
    : m_name(name)
    , m_node(&node)
    , m_node_id(int(hasher(m_name)))
{
    // compute input socket ids (hashes), needed by ImNodes
    for (const auto& socket : m_node->input_sockets()) {
        const int socket_id = int(hasher(m_name + socket.name()));
        m_input_socket_ids.push_back(socket_id);
    }

    // compute output socket ids (hashes), needed by ImNodes
    for (const auto& socket : m_node->output_sockets()) {
        const int socket_id = int(hasher(m_name + socket.name()));
        m_output_socket_ids.push_back(socket_id);
    }
}

void NodeRenderer::render()
{
    if (!m_position_set) {
        ImNodes::SetNodeEditorSpacePos(m_node_id, m_position);
        m_position_set = true;
    }
    ImNodes::BeginNode(m_node_id);

    ImNodes::BeginNodeTitleBar();
    ImGui::TextUnformatted(m_name.c_str());
    ImNodes::EndNodeTitleBar();

    render_settings();
    render_sockets();

    ImNodes::EndNode();
}

void NodeRenderer::render_sockets()
{
    // input sockets
    for (size_t i = 0; i < m_input_socket_ids.size(); i++) {
        ImNodes::BeginInputAttribute(m_input_socket_ids.at(i));
        ImGui::Text("%s", m_node->input_sockets().at(i).name().c_str());
        ImNodes::EndInputAttribute();
    }

    // output sockets
    for (size_t i = 0; i < m_output_socket_ids.size(); i++) {
        ImNodes::BeginOutputAttribute(m_output_socket_ids.at(i));
        ImGui::Text("%s", m_node->output_sockets().at(i).name().c_str());
        ImNodes::EndOutputAttribute();
    }
}

void NodeRenderer::render_settings() { }

int NodeRenderer::get_input_socket_id(const std::string& input_socket_name) const
{
    assert(m_node->has_input_socket(input_socket_name));

    for (size_t i = 0; i < m_node->input_sockets().size(); i++) {
        if (input_socket_name == m_node->input_sockets().at(i).name()) {
            return m_input_socket_ids.at(i);
        }
    }
    qFatal() << "tried to get non-existing input socket " << input_socket_name << " from node renderer for node " << m_name;
}

int NodeRenderer::get_output_socket_id(const std::string& output_socket_name) const
{
    assert(m_node->has_output_socket(output_socket_name));

    for (size_t i = 0; i < m_node->output_sockets().size(); i++) {
        if (output_socket_name == m_node->output_sockets().at(i).name()) {
            return m_output_socket_ids.at(i);
        }
    }
    qFatal() << "tried to get non-existing input socket " << output_socket_name << " from node renderer for node " << m_name;
}

} // namespace webgpu_engine::compute
