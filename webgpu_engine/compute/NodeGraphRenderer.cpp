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

#include "NodeGraphRenderer.h"

#include <imgui.h>
#include <imnodes.h>
#include <qDebug>

namespace webgpu_engine::compute {

static std::hash<std::string> hasher;

void NodeGraphRenderer::init(nodes::NodeGraph& node_graph)
{
    m_node_graph = &node_graph;
    auto& nodes = m_node_graph->get_nodes();
    for (auto& [name, node] : nodes) {
        m_node_renderers.emplace(name, std::make_unique<NodeRenderer>(name, *node.get()));
        m_node_renderers_by_node.emplace(node.get(), m_node_renderers.at(name).get());
    }
}

void NodeGraphRenderer::render()
{
    ImGui::Begin("node editor");

    ImNodes::BeginNodeEditor();

    std::vector<std::pair<int, int>> links;

    auto& nodes = m_node_graph->get_nodes();
    for (auto& [name, node_renderer] : m_node_renderers) {
        node_renderer->render();

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

                links.emplace_back(first_attribute_index, second_attribute_index);
            }
        }
    }

    // draw links
    for (size_t i = 0; i < links.size(); ++i) {
        const std::pair<int, int> p = links[i];
        // in this case, we just use the array index of the link as the unique identifier
        ImNodes::Link(i, p.first, p.second);
    }

    ImNodes::MiniMap();
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
