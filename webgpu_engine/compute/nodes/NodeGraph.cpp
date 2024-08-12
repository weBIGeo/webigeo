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

#include "NodeGraph.h"

#include "CreateHashMapNode.h"
#include "DownsampleComputeNode.h"
#include "NormalComputeNode.h"
#include "SnowComputeNode.h"
#include "TileRequestNode.h"
#include "TileSelectNode.h"
#include <QDebug>
#include <memory>

namespace webgpu_engine::compute::nodes {

void NodeGraph::add_node(std::unique_ptr<Node> node) { m_nodes.emplace_back(std::move(node)); }

Node& NodeGraph::get_node(size_t node_index) { return *m_nodes.at(node_index); }

const Node& NodeGraph::get_node(size_t node_index) const { return *m_nodes.at(node_index); }

void NodeGraph::connect_sockets(Node* from_node, SocketIndex output_socket, Node* to_node, SocketIndex input_socket)
{
    from_node->connect_output_socket(output_socket, to_node, input_socket);
    to_node->connect_input_socket(input_socket, from_node, output_socket);
}

const GpuHashMap<tile::Id, uint32_t, GpuTileId>& NodeGraph::output_hash_map() const { return *m_output_hash_map_ptr; }

const TileStorageTexture& NodeGraph::output_texture_storage() const { return *m_output_texture_storage_ptr; }

void NodeGraph::run()
{
    qDebug() << "running node graph ...";
    Node* tile_select_node = m_nodes[0].get();
    tile_select_node->run();
}

std::unique_ptr<NodeGraph> NodeGraph::create_normal_compute_graph(const PipelineManager& manager, WGPUDevice device)
{
    auto node_graph = std::make_unique<NodeGraph>();

    size_t capacity = 1024;
    glm::uvec2 input_resolution = { 65, 65 };
    glm::uvec2 output_resolution = { 256, 256 };
    node_graph->add_node(std::make_unique<TileSelectNode>());
    node_graph->add_node(std::make_unique<TileRequestNode>());
    node_graph->add_node(std::make_unique<CreateHashMapNode>(device, input_resolution, capacity, WGPUTextureFormat_R16Uint));
    node_graph->add_node(std::make_unique<NormalComputeNode>(manager, device, output_resolution, capacity, WGPUTextureFormat_RGBA8Unorm));
    node_graph->add_node(std::make_unique<DownsampleComputeNode>(manager, device, capacity, 5));

    Node* tile_select_node = node_graph->m_nodes[0].get();
    Node* height_request_node = node_graph->m_nodes[1].get();
    Node* hash_map_node = node_graph->m_nodes[2].get();
    Node* normal_compute_node = node_graph->m_nodes[3].get();
    DownsampleComputeNode* downsample_tiles_node = static_cast<DownsampleComputeNode*>(node_graph->m_nodes[4].get());

    node_graph->connect_sockets(tile_select_node, TileSelectNode::Output::TILE_ID_LIST, height_request_node, TileRequestNode::Input::TILE_ID_LIST);

    node_graph->connect_sockets(tile_select_node, TileSelectNode::Output::TILE_ID_LIST, hash_map_node, CreateHashMapNode::Input::TILE_ID_LIST);
    node_graph->connect_sockets(height_request_node, TileRequestNode::Output::TILE_TEXTURE_LIST, hash_map_node, CreateHashMapNode::Input::TILE_TEXTURE_LIST);

    node_graph->connect_sockets(tile_select_node, TileSelectNode::Output::TILE_ID_LIST, normal_compute_node, NormalComputeNode::Input::TILE_ID_LIST_TO_PROCESS);
    node_graph->connect_sockets(hash_map_node, CreateHashMapNode::Output::TILE_ID_TO_TEXTURE_ARRAY_INDEX_MAP, normal_compute_node,
        NormalComputeNode::Input::TILE_ID_TO_TEXTURE_ARRAY_INDEX_MAP);
    node_graph->connect_sockets(hash_map_node, CreateHashMapNode::Output::TEXTURE_ARRAY, normal_compute_node, NormalComputeNode::Input::TEXTURE_ARRAY);

    node_graph->connect_sockets(
        tile_select_node, TileSelectNode::Output::TILE_ID_LIST, downsample_tiles_node, DownsampleComputeNode::Input::TILE_ID_LIST_TO_PROCESS);
    node_graph->connect_sockets(normal_compute_node, NormalComputeNode::Output::OUTPUT_TILE_ID_TO_TEXTURE_ARRAY_INDEX_MAP, downsample_tiles_node,
        DownsampleComputeNode::Input::TILE_ID_TO_TEXTURE_ARRAY_INDEX_MAP);
    node_graph->connect_sockets(
        normal_compute_node, NormalComputeNode::Output::OUTPUT_TEXTURE_ARRAY, downsample_tiles_node, DownsampleComputeNode::Input::TEXTURE_ARRAY);

    node_graph->m_output_hash_map_ptr = &downsample_tiles_node->hash_map();
    node_graph->m_output_texture_storage_ptr = &downsample_tiles_node->texture_storage();

    // connect signals
    // TODO do dynamically based on graph
    connect(tile_select_node, &Node::run_finished, height_request_node, &Node::run);
    connect(height_request_node, &Node::run_finished, hash_map_node, &Node::run);
    connect(hash_map_node, &Node::run_finished, normal_compute_node, &Node::run);
    connect(normal_compute_node, &Node::run_finished, downsample_tiles_node, &Node::run);
    connect(downsample_tiles_node, &Node::run_finished, node_graph.get(), &NodeGraph::run_finished); // emits run finished signal in NodeGraph

    return node_graph;
}
std::unique_ptr<NodeGraph> NodeGraph::create_snow_compute_graph(const PipelineManager& manager, WGPUDevice device)
{
    auto node_graph = std::make_unique<NodeGraph>();

    size_t capacity = 256;
    glm::uvec2 input_resolution = { 65, 65 };
    glm::uvec2 output_resolution = { 65, 65 };
    node_graph->add_node(std::make_unique<TileSelectNode>());
    node_graph->add_node(std::make_unique<TileRequestNode>());
    node_graph->add_node(std::make_unique<CreateHashMapNode>(device, input_resolution, capacity, WGPUTextureFormat_R16Uint));
    node_graph->add_node(std::make_unique<SnowComputeNode>(manager, device, output_resolution, capacity, WGPUTextureFormat_RGBA8Unorm));
    node_graph->add_node(std::make_unique<DownsampleComputeNode>(manager, device, capacity, 3));

    Node* tile_select_node = node_graph->m_nodes[0].get();
    Node* height_request_node = node_graph->m_nodes[1].get();
    Node* hash_map_node = node_graph->m_nodes[2].get();
    Node* snow_compute_node = node_graph->m_nodes[3].get();
    DownsampleComputeNode* downsample_tiles_node = static_cast<DownsampleComputeNode*>(node_graph->m_nodes[4].get());

    node_graph->connect_sockets(tile_select_node, TileSelectNode::Output::TILE_ID_LIST, height_request_node, TileRequestNode::Input::TILE_ID_LIST);

    node_graph->connect_sockets(tile_select_node, TileSelectNode::Output::TILE_ID_LIST, hash_map_node, CreateHashMapNode::Input::TILE_ID_LIST);
    node_graph->connect_sockets(height_request_node, TileRequestNode::Output::TILE_TEXTURE_LIST, hash_map_node, CreateHashMapNode::Input::TILE_TEXTURE_LIST);

    node_graph->connect_sockets(tile_select_node, TileSelectNode::Output::TILE_ID_LIST, snow_compute_node, SnowComputeNode::Input::TILE_ID_LIST_TO_PROCESS);
    node_graph->connect_sockets(hash_map_node, CreateHashMapNode::Output::TILE_ID_TO_TEXTURE_ARRAY_INDEX_MAP, snow_compute_node,
        SnowComputeNode::Input::TILE_ID_TO_TEXTURE_ARRAY_INDEX_MAP);
    node_graph->connect_sockets(hash_map_node, CreateHashMapNode::Output::TEXTURE_ARRAY, snow_compute_node, SnowComputeNode::Input::TEXTURE_ARRAY);

    node_graph->connect_sockets(
        tile_select_node, TileSelectNode::Output::TILE_ID_LIST, downsample_tiles_node, DownsampleComputeNode::Input::TILE_ID_LIST_TO_PROCESS);
    node_graph->connect_sockets(snow_compute_node, SnowComputeNode::Output::OUTPUT_TILE_ID_TO_TEXTURE_ARRAY_INDEX_MAP, downsample_tiles_node,
        DownsampleComputeNode::Input::TILE_ID_TO_TEXTURE_ARRAY_INDEX_MAP);
    node_graph->connect_sockets(
        snow_compute_node, SnowComputeNode::Output::OUTPUT_TEXTURE_ARRAY, downsample_tiles_node, DownsampleComputeNode::Input::TEXTURE_ARRAY);

    node_graph->m_output_hash_map_ptr = &downsample_tiles_node->hash_map();
    node_graph->m_output_texture_storage_ptr = &downsample_tiles_node->texture_storage();

    // connect signals
    // TODO do dynamically based on graph
    connect(tile_select_node, &Node::run_finished, height_request_node, &Node::run);
    connect(height_request_node, &Node::run_finished, hash_map_node, &Node::run);
    connect(hash_map_node, &Node::run_finished, snow_compute_node, &Node::run);
    connect(snow_compute_node, &Node::run_finished, downsample_tiles_node, &Node::run);
    connect(downsample_tiles_node, &Node::run_finished, node_graph.get(), &NodeGraph::run_finished); // emits run finished signal in NodeGraph

    return node_graph;
}
} // namespace webgpu_engine::compute::nodes
