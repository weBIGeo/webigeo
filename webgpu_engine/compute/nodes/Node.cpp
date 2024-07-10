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

#include "Node.h"

namespace webgpu_engine::compute::nodes {

Node::Node(const std::vector<DataType>& input_types, const std::vector<DataType>& output_types)
    : input_socket_types(input_types)
    , output_socket_types(output_types)
    , connected_input_sockets(input_types.size())
    , connected_output_sockets(output_types.size())
{
}

void Node::connect_input_socket(size_t input_index, Node* connected_node, size_t connected_output_index)
{
    assert(input_index < input_socket_types.size());
    assert(connected_output_index < connected_node->output_socket_types.size());
    assert(get_input_socket_type(input_index) == connected_node->get_output_socket_type(connected_output_index));

    connected_input_sockets[input_index].connected_node = connected_node;
    connected_input_sockets[input_index].connected_socket_index = connected_output_index;
}

void Node::connect_output_socket(size_t output_index, Node* connected_node, size_t connected_input_index)
{
    assert(output_index < output_socket_types.size());
    assert(connected_input_index < connected_node->input_socket_types.size());
    assert(get_output_socket_type(output_index) == connected_node->get_input_socket_type(connected_input_index));

    connected_output_sockets[output_index].connected_node = connected_node;
    connected_output_sockets[output_index].connected_socket_index = connected_input_index;
}

DataType Node::get_input_socket_type(size_t input_socket_index) const
{
    assert(input_socket_index < input_socket_types.size());
    return input_socket_types[input_socket_index];
}

size_t Node::get_num_input_sockets() const { return input_socket_types.size(); }

DataType Node::get_output_socket_type(size_t output_socket_index) const
{
    assert(output_socket_index < output_socket_types.size());
    return output_socket_types[output_socket_index];
}

size_t Node::get_num_output_sockets() const { return output_socket_types.size(); }

Data Node::get_output_data(size_t output_index)
{
    assert(output_index < output_socket_types.size());

    // get output data for output socket index, implemented in subclasses
    Data data = get_output_data_impl(output_index);

    assert(output_socket_types[output_index] == data.index()); // implementation returned correct type
    return data;
}

Data Node::get_input_data(size_t input_index)
{
    assert(input_index < input_socket_types.size());
    assert(connected_input_sockets[input_index].connected_node != nullptr);

    // get output data from the output socket connected to this input socket
    Data data = connected_input_sockets[input_index].connected_node->get_output_data(connected_input_sockets[input_index].connected_socket_index);

    assert(input_socket_types[input_index] == data.index()); // correct type
    return data;
}

} // namespace webgpu_engine::compute::nodes
