/*****************************************************************************
 * weBIGeo
 * Copyright (C) 2024 Patrick Komon
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

#include "Node.h"
#include "webgpu_engine/PipelineManager.h"

namespace webgpu_engine::compute::nodes {

// TODO use this instead of compute controller (compute.h)
// TODO define interface - or maybe for now, just use hardcoded graph for complete normals setup
class NodeGraph : public QObject {
    Q_OBJECT

public:
    void add_node(std::unique_ptr<Node> node) { m_nodes.emplace_back(std::move(node)); }

    Node& get_node(size_t node_index) { return *m_nodes.at(node_index); }
    const Node& get_node(size_t node_index) const { return *m_nodes.at(node_index); }

    void connect_sockets(Node* from_node, SocketIndex output_socket, Node* to_node, SocketIndex input_socket);

    // TODO remove, for debugging only
    void init_test_node_graph(const PipelineManager& manager, WGPUDevice device);

public slots:
    void run();

signals:
    void run_finished();

private:
    std::vector<std::unique_ptr<Node>> m_nodes;
};

} // namespace webgpu_engine::compute::nodes
