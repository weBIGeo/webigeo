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

#include "../GpuHashMap.h"
#include "../GpuTileId.h"
#include "../GpuTileStorage.h"
#include "radix/tile.h"
#include <QByteArray>
#include <QObject>
#include <variant>
#include <vector>

namespace webgpu_engine::compute::nodes {

class Node;

using DataType = size_t;

/// datatypes that can be used with nodes have to be declared here.
using Data = std::variant<const std::vector<tile::Id>*, const std::vector<QByteArray>*, TileStorageTexture*, GpuHashMap<tile::Id, uint32_t, GpuTileId>*,
    webgpu::raii::RawBuffer<uint32_t>*>;

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
    void run();

signals:
    void run_started();
    void run_finished();

protected:
    /// Override to implement node behavior.
    /// Postcondition:
    ///   - get_output_data(output-index) returns result
    virtual void run_impl() = 0;

    /// Override to return pointer to output data for respective output slot
    virtual Data get_output_data_impl(SocketIndex output_index) = 0;

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
    size_t get_num_input_sockets() const;

    DataType get_output_socket_type(SocketIndex output_socket_index) const;
    size_t get_num_output_sockets() const;

    Data get_output_data(SocketIndex output_index);

    Data get_input_data(SocketIndex input_index);

    float last_run_duration() const;

private:
    std::chrono::high_resolution_clock::time_point m_last_run_started;
    std::chrono::high_resolution_clock::time_point m_last_run_finished;
};

} // namespace webgpu_engine::compute::nodes
