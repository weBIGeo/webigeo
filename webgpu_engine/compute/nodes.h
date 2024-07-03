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
#include "nucleus/tile_scheduler/TileLoadService.h"
#include "radix/tile.h"
#include "webgpu_engine/PipelineManager.h"
#include <QByteArray>
#include <QObject>
#include <glm/vec2.hpp>
#include <variant>
#include <vector>

namespace webgpu_engine {

class Node;

using DataType = size_t;

/// datatypes that can be used with nodes have to be declared here.
using Data
    = std::variant<const std::vector<tile::Id>*, const std::vector<QByteArray>*, const TileStorageTexture*, const GpuHashMap<tile::Id, uint32_t, GpuTileId>*>;

using SocketIndex = size_t;

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
///  - in run, subclass should
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
    void connect_input_socket(SocketIndex input_index, Node* connected_node, SocketIndex connected_output_index);

    /// Connects an output socket of this node to an input socket of another node (unidirectional)
    /// TODO should set both directions?
    void connect_output_socket(SocketIndex output_index, Node* connected_node, SocketIndex connected_input_index);

public slots:
    /// Override to implement node behavior.
    /// Postcondition:
    ///   - get_output_data(output-index) returns result
    /// TODO maybe async with signals/slots
    virtual void run() = 0;

signals:
    void run_finished();

protected:
    /// Override to return pointer to output data for respective output slot
    virtual Data get_output_data_impl(SocketIndex output_index) const = 0;

protected:
    struct ConnectedSocket {
        Node* connected_node = nullptr;
        SocketIndex connected_socket_index = std::numeric_limits<SocketIndex>::max(); // is input/output index depending on connection
    };

    std::vector<DataType> input_socket_types;
    std::vector<DataType> output_socket_types;

    std::vector<ConnectedSocket> connected_input_sockets;
    std::vector<ConnectedSocket> connected_output_sockets;

    DataType get_input_socket_type(SocketIndex input_socket_index) const;

    DataType get_output_socket_type(SocketIndex output_socket_index) const;

    Data get_output_data(SocketIndex output_index) const;

    Data get_input_data(SocketIndex input_index) const;
};

class TileSelectNode : public Node {
    Q_OBJECT

public:
    enum Input {};
    enum Output { TILE_ID_LIST = 0 };

    TileSelectNode();

public slots:
    void run() override;

protected:
    Data get_output_data_impl(SocketIndex output_index) const override;

private:
    std::vector<tile::Id> m_output_tile_ids;
};

class HeightRequestNode : public Node {
    Q_OBJECT

public:
    enum Input : SocketIndex { TILE_ID_LIST = 0 };
    enum Output : SocketIndex { TILE_TEXTURE_LIST = 0 };

    HeightRequestNode();

    void on_single_tile_received(const nucleus::tile_scheduler::tile_types::TileLayer& tile);

public slots:
    void run() override;

protected:
    Data get_output_data_impl(SocketIndex output_index) const override;

private:
    std::unique_ptr<nucleus::tile_scheduler::TileLoadService> m_tile_loader;
    size_t m_num_tiles_received = 0;
    size_t m_num_tiles_requested = 0;
    std::vector<QByteArray> m_received_tile_textures;
    std::vector<tile::Id> m_requested_tile_ids;
};

class ConvertTilesToHashMapNode : public Node {
    Q_OBJECT

public:
    enum Input : SocketIndex { TILE_ID_LIST = 0, TILE_TEXTURE_LIST = 1 };
    enum Output : SocketIndex { TILE_ID_TO_TEXTURE_ARRAY_INDEX_MAP = 0, TEXTURE_ARRAY = 1 };

    ConvertTilesToHashMapNode(WGPUDevice device, const glm::uvec2& resolution, size_t capacity, WGPUTextureFormat format);

public slots:
    void run() override;

protected:
    Data get_output_data_impl(SocketIndex output_index) const override;

private:
    WGPUDevice m_device;
    WGPUQueue m_queue;
    GpuHashMap<tile::Id, uint32_t, GpuTileId> m_output_tile_id_to_index; // for looking up index for tile id
    TileStorageTexture m_output_tile_textures; // height texture per tile
};

/// GPU compute node, calling run executes code on the GPU
class NormalComputeNode : public Node {
    Q_OBJECT

public:
    enum Input : SocketIndex { TILE_ID_LIST_TO_PROCESS = 0, TILE_ID_TO_TEXTURE_ARRAY_INDEX_MAP = 1, TEXTURE_ARRAY = 2 };
    enum Output : SocketIndex { OUTPUT_TILE_ID_TO_TEXTURE_ARRAY_INDEX_MAP = 0, OUTPUT_TEXTURE_ARRAY = 1 };

    NormalComputeNode(
        const PipelineManager& pipeline_manager, WGPUDevice device, const glm::uvec2& output_resolution, size_t capacity, WGPUTextureFormat output_format);

    const GpuHashMap<tile::Id, uint32_t, GpuTileId>& hash_map() const { return m_output_tile_map; }
    const TileStorageTexture& texture_storage() const { return m_output_texture; }

public slots:
    void run() override;

protected:
    Data get_output_data_impl(SocketIndex output_index) const override;

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
    GpuHashMap<tile::Id, uint32_t, GpuTileId> m_output_tile_map; // hash map
    TileStorageTexture m_output_texture; // texture per tile
};

// TODO use this instead of compute controller (compute.h)
// TODO define interface - or maybe for now, just use hardcoded graph for complete normals setup
class NodeGraph : public QObject {
    Q_OBJECT

public:
    void add_node(std::unique_ptr<Node> node) { m_nodes.emplace_back(std::move(node)); }

    Node& get_node(size_t node_index) { return *m_nodes.at(node_index); }
    const Node& get_node(size_t node_index) const { return *m_nodes.at(node_index); }

    void connect_sockets(Node* from_node, SocketIndex output_socket, Node* to_node, SocketIndex input_socket);

    void init_test_node_graph(const PipelineManager& manager, WGPUDevice device);

public slots:
    void run();

signals:
    void run_finished();

private:
    std::vector<std::unique_ptr<Node>> m_nodes;
};

} // namespace webgpu_engine
