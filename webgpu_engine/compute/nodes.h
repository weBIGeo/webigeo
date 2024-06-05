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

#include "GpuHashMap.h"
#include "GpuTileStorage.h"
#include "compute.h"
#include "radix/tile.h"
#include <QByteArray>
#include <QObject>
#include <glm/vec2.hpp>
#include <variant>
#include <vector>

namespace webgpu_engine {

class Node;

using DataType = size_t;

/// Data is used for type erasure in node graphs.
/// All datatypes that can be used with nodes have to be declared here.
using Data = std::variant<const std::vector<tile::Id>*, const std::vector<QByteArray>*, const TileStorageTexture*, const GpuHashMap<tile::Id, uint32_t>*>;

// Get data type (DataType value) for specific C++ type
// adapted from https://stackoverflow.com/a/52303671
template <typename T, std::size_t index = 0> static constexpr DataType data_type()
{
    static_assert(std::variant_size_v<Data> > index, "Type not found in variant");
    if constexpr (index == std::variant_size_v<Data>) {
        return index;
    } else if constexpr (std::is_same_v<std::variant_alternative_t<index, Data>, T>) {
        return index;
    } else {
        return data_type<T, index + 1>();
    }
}

/// Abstract base class for nodes.
///
/// Subclasses usually need to override methods run and get_output_data_impl.
///     - get input data from connected nodes via get_input_data(size_t input_socket_index)
///     - do some calculations
///     - save results somewhere (e.g. class member)
///   - in get_output_data_impl
///     - return pointer to result
class Node : public QObject {
    Q_OBJECT

public:
    Node(const std::vector<DataType>& input_types, const std::vector<DataType>& output_types);
    virtual ~Node() = default;

    /// Connects an input socket of this node to an output socket of another node
    /// TODO should set both directions?
    void connect_input_socket(size_t input_index, Node* connected_node, size_t connected_output_index);

    /// Connects an output socket of this node to an input socket of another node (unidirectional)
    /// TODO should set both directions?
    void connect_output_socket(size_t output_index, Node* connected_node, size_t connected_input_index);

    /// Override to implement node behavior.
    /// Postcondition:
    ///   - get_output_data(output-index) returns result
    /// TODO maybe async with signals/slots
    virtual void run() = 0;

protected:
    /// Override to return pointer to output data for respective output slot
    virtual Data get_output_data_impl(size_t output_index) const = 0;

protected:
    struct ConnectedSocket {
        Node* connected_node = nullptr;
        size_t connected_socket_index = std::numeric_limits<size_t>::max(); // is input/output index depending on connection
    };

    std::vector<DataType> input_socket_types;
    std::vector<DataType> output_socket_types;

    std::vector<ConnectedSocket> connected_input_sockets;
    std::vector<ConnectedSocket> connected_output_sockets;

    DataType get_input_socket_type(size_t input_socket_index) const;

    DataType get_output_socket_type(size_t output_socket_index) const;

    Data get_output_data(size_t output_index) const;

    Data get_input_data(size_t input_index) const;
};

class TileSelectNode : public Node {
    Q_OBJECT
public:
    TileSelectNode();

    void run() override;
    Data get_output_data_impl(size_t output_index) const override;

private:
    std::vector<tile::Id> m_output_tile_ids;
};

class HeightRequestNode : public Node {
    Q_OBJECT
public:
    HeightRequestNode();

    void run() override;
    Data get_output_data_impl([[maybe_unused]] size_t output_index) const override;

    void on_single_tile_received(const nucleus::tile_scheduler::tile_types::TileLayer& tile);

signals:
    void all_tiles_received();

private:
    std::unique_ptr<nucleus::tile_scheduler::TileLoadService> m_tile_loader;
    size_t m_num_tiles_received = 0;
    size_t m_num_tiles_requested = 0;
    std::vector<QByteArray> m_received_tile_textures;
    std::vector<tile::Id> m_requested_tile_ids;
};

class ConvertTilesToHashMapNode : public Node {
public:
    ConvertTilesToHashMapNode(WGPUDevice device, const glm::uvec2& resolution, size_t capacity, WGPUTextureFormat format);

    void run() override;
    Data get_output_data_impl(size_t output_index) const override;

private:
    GpuHashMap<tile::Id, uint32_t> m_output_tile_id_to_index; // for looking up index for tile id
    TileStorageTexture m_output_tile_textures; // height texture per tile
};

/// GPU compute node, calling run executes code on the GPU
class NormalComputeNode : public Node {
    Q_OBJECT
public:
    // for representing tile ids on the GPU side
    struct GpuTileId {
        uint32_t x;
        uint32_t y;
        uint32_t zoomlevel;
    };

    NormalComputeNode(
        const PipelineManager& pipeline_manager, WGPUDevice device, const glm::uvec2& output_resolution, size_t capacity, WGPUTextureFormat output_format);

    void run() override;
    Data get_output_data_impl(size_t output_index) const override;

    void recreate_bind_groups();

private:
    const PipelineManager* m_pipeline_manager;
    WGPUDevice m_device;
    WGPUQueue m_queue;
    size_t m_capacity;

    // calculated on cpu-side before each invocation
    raii::RawBuffer<glm::vec4> m_tile_bounds; // aabb per tile

    // input
    raii::RawBuffer<GpuTileId> m_input_tile_ids; // tile ids for which to calculate normals

    // output
    GpuHashMap<tile::Id, uint32_t> m_output_tile_map; // hash map
    TileStorageTexture m_output_texture; // texture per tile
};

// TODO use this instead of compute controller (compute.h)
// TODO define interface - or maybe for now, just use hardcoded graph for complete normals setup
class NodeGraph {
public:
    void add_node(std::unique_ptr<Node> node) { m_nodes.emplace_back(std::move(node)); }

    void connect_sockets(Node* from_node, int output_socket, Node* to_node, int input_socket)
    {
        from_node->connect_output_socket(output_socket, to_node, input_socket);
        to_node->connect_input_socket(input_socket, to_node, output_socket);
    }

    void init_test_node_graph(const PipelineManager& manager, WGPUDevice device)
    {
        size_t capacity = 256;
        glm::uvec2 input_resolution = { 65, 65 };
        glm::uvec2 output_resolution = { 256, 256 };
        add_node(std::make_unique<TileSelectNode>());
        add_node(std::make_unique<HeightRequestNode>());
        add_node(std::make_unique<ConvertTilesToHashMapNode>(device, input_resolution, capacity, WGPUTextureFormat_R16Uint));
        add_node(std::make_unique<NormalComputeNode>(manager, device, output_resolution, capacity, WGPUTextureFormat_RGBA8Unorm));

        Node* tile_select_node = m_nodes[0].get();
        Node* hash_map_node = m_nodes[1].get();
        Node* height_request_node = m_nodes[2].get();
        Node* normal_compute_node = m_nodes[3].get();

        connect_sockets(tile_select_node, 0, height_request_node, 0); // tile ids from select node to request node

        connect_sockets(height_request_node, 0, hash_map_node, 0);

        connect_sockets(tile_select_node, 0, normal_compute_node, 0); // tile ids to process
        connect_sockets(hash_map_node, 0, normal_compute_node, 1); // hash map for texture lookup
        connect_sockets(hash_map_node, 1, normal_compute_node, 2); // texture array containing textures
    }

private:
    std::vector<std::unique_ptr<Node>> m_nodes;
};

} // namespace webgpu_engine
