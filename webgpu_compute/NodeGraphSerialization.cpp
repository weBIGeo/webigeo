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

#include "NodeGraphSerialization.h"

#include <QJsonArray>
#include <algorithm>
#include <vector>

namespace webgpu_compute::nodes {

QJsonObject serialize_node_graph(const NodeGraph& graph)
{
    QJsonObject root;
    root["format"] = QLatin1String(NODE_GRAPH_JSON_FORMAT);
    root["version"] = NODE_GRAPH_JSON_VERSION;
    root["name"] = QString::fromStdString(graph.get_name());

    // m_nodes is an unordered_map; sort by name for stable, diffable output
    std::vector<std::pair<std::string, const Node*>> sorted_nodes;
    sorted_nodes.reserve(graph.get_nodes().size());
    for (const auto& [name, node] : graph.get_nodes())
        sorted_nodes.emplace_back(name, node.get());
    std::sort(sorted_nodes.begin(), sorted_nodes.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

    QJsonArray nodes_array;
    QJsonArray connections_array;
    for (const auto& [name, node] : sorted_nodes) {
        QJsonObject node_object;
        node_object["name"] = QString::fromStdString(name);
        node_object["type"] = QString::fromStdString(node->get_type_name());
        node_object["enabled"] = node->is_enabled();
        QJsonObject settings;
        node->serialize_settings(settings);
        if (!settings.isEmpty())
            node_object["settings"] = settings;
        nodes_array.append(node_object);

        // Collecting connections from the input side (each input has at most one source)
        // yields each connection exactly once, in deterministic order.
        for (const InputSocket& input_socket : node->input_sockets()) {
            if (!input_socket.is_socket_connected())
                continue;
            const OutputSocket& output_socket = input_socket.connected_socket();
            QJsonObject from;
            from["node"] = QString::fromStdString(output_socket.node().get_node_name());
            from["socket"] = QString::fromStdString(output_socket.name());
            QJsonObject to;
            to["node"] = QString::fromStdString(name);
            to["socket"] = QString::fromStdString(input_socket.name());
            QJsonObject connection;
            connection["from"] = from;
            connection["to"] = to;
            connections_array.append(connection);
        }
    }
    root["nodes"] = nodes_array;
    root["connections"] = connections_array;
    return root;
}

// deserialize_node_graph and load_node_graph_from_file are implemented in the load-path
// phase; the declarations exist so the API surface is complete.

} // namespace webgpu_compute::nodes
