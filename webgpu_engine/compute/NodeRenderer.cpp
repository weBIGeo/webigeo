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

#include "NodeRenderer.h"

#include <imgui.h>
#include <imnodes.h>
#include <qDebug>

namespace webgpu_engine::compute {

static std::hash<std::string> hasher;

std::string NodeRenderer::format_node_name(const std::string& name)
{
    std::string n = name;
    while (n.find("_node") != std::string::npos)
        n.erase(n.find("_node"), 5);

    for (char& c : n)
        if (c == '_')
            c = ' ';

    bool cap = true;
    for (char& c : n)
        if (std::isspace((unsigned char)c))
            cap = true;
        else if (cap) {
            c = std::toupper((unsigned char)c);
            cap = false;
        }

    return n;
}

NodeRenderer::NodeRenderer(const std::string& name, nodes::Node& node)
    : m_name(name)
    , m_name_formatted(NodeRenderer::format_node_name(m_name))
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

ImVec2 NodeRenderer::get_size() const
{
    if (m_size.x >= 0)
        return m_size;
    float width = std::max(100.0f, (float)m_name_formatted.size() * 7.3f + 21.0f);
    size_t num = m_node->input_sockets().size() + m_node->output_sockets().size();
    float height = 75.0f + num * 20.0f;
    return ImVec2(width, height);
}

void NodeRenderer::render(bool reset_position)
{
    if (reset_position) {
        ImNodes::SetNodeEditorSpacePos(m_node_id, m_position);
    }

    bool is_disabled = !m_node->is_enabled();
    if (is_disabled) {
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.3f);
        ImNodes::PushColorStyle(ImNodesCol_TitleBar, IM_COL32(100, 100, 100, 255));
        ImNodes::PushColorStyle(ImNodesCol_TitleBarHovered, IM_COL32(100, 100, 100, 255));
        ImNodes::PushColorStyle(ImNodesCol_TitleBarSelected, IM_COL32(100, 100, 100, 255));
    }

    ImNodes::BeginNode(m_node_id);

    ImNodes::BeginNodeTitleBar();
    bool enabled = m_node->is_enabled();
    ImGui::TextUnformatted(m_name_formatted.c_str());
    ImGui::SameLine();
    if (ImGui::Checkbox("##enabled", &enabled)) {
        m_node->set_enabled(enabled);
    }
    // ImGui::TextUnformatted(m_name_formatted.c_str());
    ImNodes::EndNodeTitleBar();

    render_settings();
    render_sockets();

    // Display the last run duration
    float duration_ms = m_node->last_run_duration();
    ImGui::Dummy(ImVec2(0.0f, 4.0f)); // spacing
    ImGui::Text("Last run: %0.0f ms", duration_ms);

    ImNodes::EndNode();

    // Get size of the node
    ImVec2 min = ImGui::GetItemRectMin();
    ImVec2 max = ImGui::GetItemRectMax();
    ImVec2 size = ImVec2(max.x - min.x, max.y - min.y);
    m_size = size;

    if (is_disabled) {
        ImNodes::PopColorStyle();
        ImNodes::PopColorStyle();
        ImNodes::PopColorStyle();
        ImGui::PopStyleVar();
    }
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
