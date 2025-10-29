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

// see https://easings.net/#easeOutElastic
static float easeOutElastic(float x)
{
    const float pi = 3.14159265359f;
    const float c4 = (2.0f * pi) / 3.0f;

    if (x == 0.0f)
        return 0.0f;
    if (x == 1.0f)
        return 1.0f;

    return powf(2.0f, -10.0f * x) * sinf((x * 10.0f - 0.75f) * c4) + 1.0f;
}

void NodeGraphRenderer::init(nodes::NodeGraph& node_graph)
{
    m_window_title = "Compute Graph Editor - " + NodeRenderer::format_node_name(node_graph.get_name());

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

    m_first_frame_after_init = true;
}

void NodeGraphRenderer::calculate_window_size()
{
    if (!ImGui::GetCurrentContext()) {
        m_window_size = ImVec2(0.0f, 0.0f);
        return;
    }
    m_window_size = ImGui::GetIO().DisplaySize;
    m_window_size.x -= 430;
}

void NodeGraphRenderer::calculate_auto_layout()
{
    m_target_layout.clear();

    // Step 1: Identify root nodes (no inputs) and enqueue them at x = 0
    std::queue<std::pair<NodeRenderer*, int>> queue;
    for (auto& [name, nr] : m_node_renderers) {
        nodes::Node* node = nr->get_node();
        if (node->input_sockets().empty()) {
            int x = 0;
            m_target_layout[node] = ImVec2(x, 0);
            queue.emplace(nr.get(), x);
        }
    }

    // Step 2: Perform BFS to assign x-layer positions based on the maximum distance from root nodes
    while (!queue.empty()) {
        auto [nr, current_x] = queue.front();
        queue.pop();

        nodes::Node* current_node = nr->get_node();
        const auto& outputs = current_node->output_sockets();
        for (const auto& os : outputs) {
            for (auto* conn : os.connected_sockets()) {
                nodes::Node* target_node = &conn->node();
                NodeRenderer* target = m_node_renderers_by_node[target_node];

                int x = current_x + 1;
                m_target_layout[target_node] = ImVec2(x, 0);
                queue.emplace(target, x);
            }
        }
    }

    // Step 3: Group nodes by x-layer and assign sequential y-indices
    std::unordered_map<int, std::vector<NodeRenderer*>> x_buckets;
    x_buckets.reserve(m_node_renderers.size());

    for (auto& [name, nr] : m_node_renderers) {
        int x = (int)m_target_layout[nr->get_node()].x;
        x_buckets[x].push_back(nr.get());
    }

    for (auto& [x, vec] : x_buckets) {
        int y = 0;
        for (auto* r : vec)
            m_target_layout[r->get_node()] = ImVec2((float)x, (float)y++);
    }

    // Step 4: Compute pixel layout. Determine column widths, x-offsets, and center vertically
    std::vector<int> cols;
    cols.reserve(x_buckets.size());
    for (auto& kv : x_buckets)
        cols.push_back(kv.first);
    std::sort(cols.begin(), cols.end());

    std::unordered_map<int, float> col_width;
    col_width.reserve(cols.size());

    for (int cx : cols) {
        float wmax = 0.f;
        for (NodeRenderer* r : x_buckets[cx]) {
            ImVec2 sz = r->get_size();
            wmax = std::max(wmax, sz.x);
        }
        col_width[cx] = wmax;
    }

    std::unordered_map<int, float> col_x_offset;
    col_x_offset.reserve(cols.size());

    float x_cursor = 0.f;
    for (size_t i = 0; i < cols.size(); ++i) {
        int cx = cols[i];
        if (i == 0)
            col_x_offset[cx] = x_cursor;
        else
            col_x_offset[cx] = (x_cursor += col_width[cols[i - 1]] + m_initial_node_spacing.x);
    }

    float frame_height = 0.f;
    for (int cx : cols) {
        float hsum = 0.f;
        for (NodeRenderer* r : x_buckets[cx]) {
            ImVec2 sz = r->get_size();
            hsum += sz.y + m_initial_node_spacing.y;
        }
        if (!x_buckets[cx].empty())
            hsum -= m_initial_node_spacing.y;
        frame_height = std::max(frame_height, hsum);
    }

    for (int cx : cols) {
        float column_height = 0.f;
        for (NodeRenderer* r : x_buckets[cx])
            column_height += r->get_size().y + m_initial_node_spacing.y;

        if (!x_buckets[cx].empty())
            column_height -= m_initial_node_spacing.y;

        float y_cursor = (frame_height - column_height) * 0.5f;

        for (NodeRenderer* r : x_buckets[cx]) {
            ImVec2 sz = r->get_size();
            m_target_layout[r->get_node()] = ImVec2(col_x_offset[cx], y_cursor);
            y_cursor += sz.y + m_initial_node_spacing.y;
        }
    }
}

void NodeGraphRenderer::apply_node_layout(float animation_duration)
{
    if (animation_duration <= 0.0f) {
        // apply instantly
        for (auto& [nodePtr, pos] : m_target_layout)
            m_node_renderers_by_node[nodePtr]->set_position(pos);

        m_animation_running = false;
        m_force_node_positions_on_next_frame = true;
        m_animation_runtime = 0.0f;
        return;
    }

    // Prepare animation
    m_animation_running = true;
    m_animation_duration = animation_duration;
    m_animation_runtime = 0.0f;

    m_start_layout.clear();
    for (auto& [nodePtr, nr] : m_node_renderers_by_node)
        m_start_layout[nodePtr] = nr->get_position();
}

void NodeGraphRenderer::process_animation(float dt)
{
    if (!m_animation_running)
        return;

    m_animation_runtime += dt;
    float t = m_animation_runtime / m_animation_duration;
    if (t >= 1.0f)
        t = 1.0f;

    float smooth = easeOutElastic(t);

    for (auto& [nodePtr, startPos] : m_start_layout) {
        ImVec2 endPos = m_target_layout[nodePtr];
        ImVec2 p;
        p.x = startPos.x + (endPos.x - startPos.x) * smooth;
        p.y = startPos.y + (endPos.y - startPos.y) * smooth;

        m_node_renderers_by_node[nodePtr]->set_position(p);
    }
    recenter_node_graph();

    if (t >= 1.0f)
        m_animation_running = false;
}

ImVec4 NodeGraphRenderer::get_graph_aabb() const
{
    ImVec4 aabb(FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX);
    for (auto& [name, nr] : m_node_renderers) {
        ImVec2 p = nr->get_position();
        ImVec2 s = nr->get_size();
        aabb.x = std::min(aabb.x, p.x); // minX
        aabb.y = std::min(aabb.y, p.y); // minY
        aabb.z = std::max(aabb.z, p.x + s.x); // maxX
        aabb.w = std::max(aabb.w, p.y + s.y); // maxY
    }
    return aabb;
}

void NodeGraphRenderer::recenter_node_graph()
{
    ImVec4 aabb = get_graph_aabb();
    float graph_width = aabb.z - aabb.x;
    float graph_height = aabb.w - aabb.y;
    float offset_x = (m_window_size.x - graph_width) * 0.5f - aabb.x;
    float offset_y = (m_window_size.y - graph_height) * 0.5f - aabb.y;
    for (auto& [name, nr] : m_node_renderers) {
        ImVec2 p = nr->get_position();
        p.x += offset_x;
        p.y += offset_y;
        nr->set_position(p);
    }
}

void NodeGraphRenderer::push_style()
{
    // Always use transparent grid background
    ImNodes::PushColorStyle(ImNodesCol_GridBackground, IM_COL32(50, 50, 50, 0));

    if (m_render_mode == GraphRenderingMode::Default) {
        ImNodes::PushColorStyle(ImNodesCol_GridLine, IM_COL32(200, 200, 200, 40)); // light gray
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::GetStyleColorVec4(ImGuiCol_WindowBg)); // default ImGui bg

    } else if (m_render_mode == GraphRenderingMode::Transparent) {
        ImNodes::PushColorStyle(ImNodesCol_GridLine, IM_COL32(200, 200, 200, 40));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f)); // fully transparent

    } else if (m_render_mode == GraphRenderingMode::White) {
        ImNodes::PushColorStyle(ImNodesCol_GridLine, IM_COL32(200, 200, 200, 40));
        ImVec4 old_color = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(1.0f, 1.0f, 1.0f, old_color.w)); // white, normal alpha

    } else if (m_render_mode == GraphRenderingMode::WhiteOpaque) {
        ImNodes::PushColorStyle(ImNodesCol_GridLine, IM_COL32(255, 255, 255, 0)); // no gridlines
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(1.0f, 1.0f, 1.0f, 1.0f)); // full white opaque
    }
}

void NodeGraphRenderer::pop_style()
{
    ImGui::PopStyleColor(); // ImGui window background
    ImNodes::PopColorStyle(); // Grid line
    ImNodes::PopColorStyle(); // Grid background
}

void NodeGraphRenderer::render()
{
    // --- Check for mode toggle key ---
    if (ImGui::IsKeyPressed(ImGuiKey_M)) {
        m_render_mode = static_cast<GraphRenderingMode>((static_cast<int>(m_render_mode) + 1) % 4);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_L)) {
        // reset nodes to center
        for (auto& [name, nr] : m_node_renderers) {
            nr->set_position(ImVec2(0.0f, 0.0f));
        }
        calculate_auto_layout();
        apply_node_layout(1.0f);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_C)) {
        recenter_node_graph();
    }

    calculate_window_size();
    if (m_first_frame_after_init) {
        calculate_auto_layout();
        apply_node_layout(1.0f);
    }

    bool force_node_position = m_animation_running || m_force_node_positions_on_next_frame;
    if (m_animation_running) {
        float dt = ImGui::GetIO().DeltaTime;
        process_animation(dt);
    }

    push_style();

    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(m_window_size, ImGuiCond_Always);

    ImGui::Begin(m_window_title.c_str());

    ImNodes::BeginNodeEditor();

    // draw nodes
    for (auto& [name, node_renderer] : m_node_renderers) {
        node_renderer->render(force_node_position);
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
    pop_style();

    m_force_node_positions_on_next_frame = false;
    m_first_frame_after_init = false;
}

} // namespace webgpu_engine::compute
