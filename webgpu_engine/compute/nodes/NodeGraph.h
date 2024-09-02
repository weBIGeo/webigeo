/*****************************************************************************
 * weBIGeo
 * Copyright (C) 2024 Patrick Komon
 * Copyright (C) 2024 Gerald Kimmersdorfer
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
#include <memory>

namespace webgpu_engine::compute::nodes {

class NodeGraph;

// TODO define interface - or maybe for now, just use hardcoded graph for complete normals setup
class NodeGraph : public QObject {
    Q_OBJECT

public:
    NodeGraph() = default;

    Node* add_node(std::unique_ptr<Node> node);

    Node& get_node(size_t node_index);
    const Node& get_node(size_t node_index) const;

    void connect_sockets(Node* from_node, SocketIndex output_socket, Node* to_node, SocketIndex input_socket);

    // obtain outputs - for now all node graphs always output
    //  - a hashmap (mapping tile id to texture array layer)
    //  - a texture array
    const GpuHashMap<tile::Id, uint32_t, GpuTileId>& output_hash_map() const;
    GpuHashMap<tile::Id, uint32_t, GpuTileId>& output_hash_map();
    const TileStorageTexture& output_texture_storage() const;
    TileStorageTexture& output_texture_storage();

    const GpuHashMap<tile::Id, uint32_t, GpuTileId>& output_hash_map_2() const;
    GpuHashMap<tile::Id, uint32_t, GpuTileId>& output_hash_map_2();
    const TileStorageTexture& output_texture_storage_2() const;
    TileStorageTexture& output_texture_storage_2();

public slots:
    void run();

signals:
    void run_finished();

public:
    static std::unique_ptr<NodeGraph> create_normal_compute_graph(const PipelineManager& manager, WGPUDevice device);
    static std::unique_ptr<NodeGraph> create_snow_compute_graph(const PipelineManager& manager, WGPUDevice device);
    static std::unique_ptr<NodeGraph> create_normal_with_snow_compute_graph(const PipelineManager& manager, WGPUDevice device);

private:
    std::vector<std::unique_ptr<Node>> m_nodes;
    GpuHashMap<tile::Id, uint32_t, GpuTileId>* m_output_hash_map_ptr;
    TileStorageTexture* m_output_texture_storage_ptr;

    GpuHashMap<tile::Id, uint32_t, GpuTileId>* m_output_hash_map_ptr_2;
    TileStorageTexture* m_output_texture_storage_ptr_2;
};

} // namespace webgpu_engine::compute::nodes
