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

#include "NodeGraph.h"

#include "ComputeAvalancheInfluenceAreaNode.h"
#include "ComputeAvalancheTrajectoriesNode.h"
#include "ComputeNormalsNode.h"
#include "ComputeSnowNode.h"
#include "CreateHashMapNode.h"
#include "DownsampleTilesNode.h"
#include "RequestTilesNode.h"
#include "SelectTilesNode.h"
#include "UpsampleTexturesNode.h"
#include "compute/RectangularTileRegion.h"
#include <QDebug>
#include <memory>

namespace webgpu_engine::compute::nodes {

Node* NodeGraph::add_node(const std::string& name, std::unique_ptr<Node> node)
{
    assert(!m_nodes.contains(name));
    m_nodes.emplace(name, std::move(node));
    return m_nodes.at(name).get();
}

Node& NodeGraph::get_node(const std::string& node_name) { return *m_nodes.at(node_name); }

const Node& NodeGraph::get_node(const std::string& node_name) const { return *m_nodes.at(node_name); }

bool NodeGraph::exists_node(const std::string& node_name) const { return m_nodes.find(node_name) != m_nodes.end(); }

const GpuHashMap<tile::Id, uint32_t, GpuTileId>& NodeGraph::output_hash_map() const { return *m_output_hash_map_ptr; }

GpuHashMap<tile::Id, uint32_t, GpuTileId>& NodeGraph::output_hash_map() { return *m_output_hash_map_ptr; }

const TileStorageTexture& NodeGraph::output_texture_storage() const { return *m_output_texture_storage_ptr; }

TileStorageTexture& NodeGraph::output_texture_storage() { return *m_output_texture_storage_ptr; }

const GpuHashMap<tile::Id, uint32_t, GpuTileId>& NodeGraph::output_hash_map_2() const { return *m_output_hash_map_ptr_2; }

GpuHashMap<tile::Id, uint32_t, GpuTileId>& NodeGraph::output_hash_map_2() { return *m_output_hash_map_ptr_2; }

const TileStorageTexture& NodeGraph::output_texture_storage_2() const { return *m_output_texture_storage_ptr_2; }

TileStorageTexture& NodeGraph::output_texture_storage_2() { return *m_output_texture_storage_ptr_2; }

void NodeGraph::run()
{
    qDebug() << "running node graph ...";
    m_start_node->run();
}

std::unique_ptr<NodeGraph> NodeGraph::create_normal_compute_graph(const PipelineManager& manager, WGPUDevice device)
{
    glm::uvec2 min = { 140288 + 100, 169984 };
    RectangularTileRegion region { .min = min,
        .max = min + glm::uvec2 { 26, 26 }, // inclusive, so this region has 27x27 tiles
        .zoom_level = 18,
        .scheme = tile::Scheme::Tms };
    auto tile_id_generator_func = [region]() { return region.get_tiles(); };

    size_t capacity = 1024;
    glm::uvec2 input_resolution = { 65, 65 };
    glm::uvec2 normal_output_resolution = { 65, 65 };
    glm::uvec2 upsample_output_resolution = { 256, 256 };

    auto node_graph = std::make_unique<NodeGraph>();
    Node* tile_select_node = node_graph->add_node("select_tiles_node", std::make_unique<SelectTilesNode>(tile_id_generator_func));
    Node* height_request_node = node_graph->add_node("request_height_node", std::make_unique<RequestTilesNode>());
    Node* hash_map_node
        = node_graph->add_node("hashmap_node", std::make_unique<CreateHashMapNode>(device, input_resolution, capacity, WGPUTextureFormat_R16Uint));
    Node* normal_compute_node = node_graph->add_node(
        "compute_normals_node", std::make_unique<ComputeNormalsNode>(manager, device, normal_output_resolution, capacity, WGPUTextureFormat_RGBA8Unorm));
    Node* upsample_textures_node
        = node_graph->add_node("upsample_textures_node", std::make_unique<UpsampleTexturesNode>(manager, device, upsample_output_resolution, capacity));
    DownsampleTilesNode* downsample_tiles_node
        = static_cast<DownsampleTilesNode*>(node_graph->add_node("downsample_tiles_node", std::make_unique<DownsampleTilesNode>(manager, device, capacity, 5)));

    // connect height request inputs
    tile_select_node->output_socket("tile ids").connect(height_request_node->input_socket("tile ids"));

    // connect height request inputs
    tile_select_node->output_socket("tile ids").connect(hash_map_node->input_socket("tile ids"));
    height_request_node->output_socket("tile data").connect(hash_map_node->input_socket("texture data"));

    // connect normal node inputs
    tile_select_node->output_socket("tile ids").connect(normal_compute_node->input_socket("tile ids"));
    hash_map_node->output_socket("hash map").connect(normal_compute_node->input_socket("hash map"));
    hash_map_node->output_socket("textures").connect(normal_compute_node->input_socket("height textures"));

    //  connect upsample textures node inputs
    normal_compute_node->output_socket("normal textures").connect(upsample_textures_node->input_socket("source textures"));

    // connect downsample tiles node inputs
    tile_select_node->output_socket("tile ids").connect(downsample_tiles_node->input_socket("tile ids"));
    normal_compute_node->output_socket("hash map").connect(downsample_tiles_node->input_socket("hash map"));
    normal_compute_node->output_socket("normal textures").connect(downsample_tiles_node->input_socket("textures"));

    node_graph->m_output_hash_map_ptr = &downsample_tiles_node->hash_map();
    node_graph->m_output_texture_storage_ptr = &downsample_tiles_node->texture_storage();
    node_graph->m_output_hash_map_ptr_2 = &downsample_tiles_node->hash_map();
    node_graph->m_output_texture_storage_ptr_2 = &downsample_tiles_node->texture_storage();

    // connect signals
    // TODO do dynamically based on graph
    node_graph->m_start_node = tile_select_node;
    connect(tile_select_node, &Node::run_finished, height_request_node, &Node::run);
    connect(height_request_node, &Node::run_finished, hash_map_node, &Node::run);
    connect(hash_map_node, &Node::run_finished, normal_compute_node, &Node::run);
    connect(normal_compute_node, &Node::run_finished, upsample_textures_node, &Node::run);
    connect(upsample_textures_node, &Node::run_finished, downsample_tiles_node, &Node::run);
    connect(downsample_tiles_node, &Node::run_finished, node_graph.get(), &NodeGraph::run_finished); // emits run finished signal in NodeGraph

    return node_graph;
}

std::unique_ptr<NodeGraph> NodeGraph::create_normal_with_snow_compute_graph(const PipelineManager& manager, WGPUDevice device)
{
    glm::uvec2 min = { 140288 + 100, 169984 };
    RectangularTileRegion region { .min = min,
        .max = min + glm::uvec2 { 26, 26 }, // inclusive, so this region has 27x27 tiles
        .zoom_level = 18,
        .scheme = tile::Scheme::Tms };
    auto tile_id_generator_func = [region]() { return region.get_tiles(); };

    size_t capacity = 1024;
    glm::uvec2 input_resolution = { 65, 65 };
    glm::uvec2 normal_output_resolution = { 65, 65 };
    glm::uvec2 upsample_output_resolution = { 256, 256 };

    auto node_graph = std::make_unique<NodeGraph>();
    Node* tile_select_node = node_graph->add_node("select_tiles_node", std::make_unique<SelectTilesNode>(tile_id_generator_func));
    Node* height_request_node = node_graph->add_node("request_height_node", std::make_unique<RequestTilesNode>());
    Node* hash_map_node
        = node_graph->add_node("create_hashmap_node", std::make_unique<CreateHashMapNode>(device, input_resolution, capacity, WGPUTextureFormat_R16Uint));
    Node* normal_compute_node = node_graph->add_node(
        "compute_normals_node", std::make_unique<ComputeNormalsNode>(manager, device, normal_output_resolution, capacity, WGPUTextureFormat_RGBA8Unorm));
    Node* snow_compute_node = node_graph->add_node(
        "compute_snow_node", std::make_unique<ComputeSnowNode>(manager, device, normal_output_resolution, capacity, WGPUTextureFormat_RGBA8Unorm));
    Node* upsample_textures_node
        = node_graph->add_node("upsample_textures_node", std::make_unique<UpsampleTexturesNode>(manager, device, upsample_output_resolution, capacity));
    Node* upsample_snow_textures_node
        = node_graph->add_node("upsample_snow_textures_node", std::make_unique<UpsampleTexturesNode>(manager, device, upsample_output_resolution, capacity));
    DownsampleTilesNode* downsample_snow_tiles_node
        = static_cast<DownsampleTilesNode*>(node_graph->add_node("downsample_tiles_node", std::make_unique<DownsampleTilesNode>(manager, device, capacity, 5)));
    DownsampleTilesNode* downsample_tiles_node = static_cast<DownsampleTilesNode*>(
        node_graph->add_node("downsample_snow_tiles_node", std::make_unique<DownsampleTilesNode>(manager, device, capacity, 5)));

    // connect height request node inputs
    tile_select_node->output_socket("tile ids").connect(height_request_node->input_socket("tile ids"));

    // connect hash map node inputs
    tile_select_node->output_socket("tile ids").connect(hash_map_node->input_socket("tile ids"));
    height_request_node->output_socket("tile data").connect(hash_map_node->input_socket("texture data"));

    // connect normal node inputs
    tile_select_node->output_socket("tile ids").connect(normal_compute_node->input_socket("tile ids"));
    hash_map_node->output_socket("hash map").connect(normal_compute_node->input_socket("hash map"));
    hash_map_node->output_socket("textures").connect(normal_compute_node->input_socket("height textures"));

    // connect snow compute node inputs
    tile_select_node->output_socket("tile ids").connect(snow_compute_node->input_socket("tile ids"));
    hash_map_node->output_socket("hash map").connect(snow_compute_node->input_socket("hash map"));
    hash_map_node->output_socket("textures").connect(snow_compute_node->input_socket("height textures"));

    // upscale snow texture
    snow_compute_node->output_socket("snow textures").connect(upsample_snow_textures_node->input_socket("source textures"));

    // create downsamples snow tiles
    tile_select_node->output_socket("tile ids").connect(downsample_snow_tiles_node->input_socket("tile ids"));
    snow_compute_node->output_socket("hash map").connect(downsample_snow_tiles_node->input_socket("hash map"));
    upsample_snow_textures_node->output_socket("output textures").connect(downsample_snow_tiles_node->input_socket("textures"));

    // connect upsample textures node inputs
    normal_compute_node->output_socket("normal textures").connect(upsample_textures_node->input_socket("source textures"));

    // connect downsample tiles node inputs
    tile_select_node->output_socket("tile ids").connect(downsample_tiles_node->input_socket("tile ids"));
    normal_compute_node->output_socket("hash map").connect(downsample_tiles_node->input_socket("hash map"));
    upsample_textures_node->output_socket("output textures").connect(downsample_tiles_node->input_socket("textures"));

    node_graph->m_output_hash_map_ptr = &downsample_tiles_node->hash_map();
    node_graph->m_output_texture_storage_ptr = &downsample_tiles_node->texture_storage();

    node_graph->m_output_hash_map_ptr_2 = &downsample_snow_tiles_node->hash_map();
    node_graph->m_output_texture_storage_ptr_2 = &downsample_snow_tiles_node->texture_storage();

    // connect signals
    // TODO do dynamically based on graph
    node_graph->m_start_node = tile_select_node;
    connect(tile_select_node, &Node::run_finished, height_request_node, &Node::run);
    connect(height_request_node, &Node::run_finished, hash_map_node, &Node::run);
    connect(hash_map_node, &Node::run_finished, normal_compute_node, &Node::run);
    connect(hash_map_node, &Node::run_finished, snow_compute_node, &Node::run);
    connect(normal_compute_node, &Node::run_finished, upsample_textures_node, &Node::run);
    connect(upsample_textures_node, &Node::run_finished, downsample_tiles_node, &Node::run);
    connect(snow_compute_node, &Node::run_finished, upsample_snow_textures_node, &Node::run);
    connect(upsample_snow_textures_node, &Node::run_finished, downsample_snow_tiles_node, &Node::run);
    // TODO NodeGraph should keep track of all the currently running nodes and emit run_finished when all of them are done. Currently run_finished gets executed
    // twice which in our setup is okay (for now)
    connect(downsample_snow_tiles_node, &Node::run_finished, node_graph.get(), &NodeGraph::run_finished);
    connect(downsample_tiles_node, &Node::run_finished, node_graph.get(), &NodeGraph::run_finished);

    return node_graph;
}

std::unique_ptr<NodeGraph> NodeGraph::create_snow_compute_graph(const PipelineManager& manager, WGPUDevice device)
{
    glm::uvec2 min = { 140288, 169984 };
    RectangularTileRegion region { .min = min,
        .max = min + glm::uvec2 { 12, 12 }, // inclusive, so this region has 13x13 tiles
        .zoom_level = 18,
        .scheme = tile::Scheme::Tms };
    auto tile_id_generator_func = [region]() { return region.get_tiles(); };

    size_t capacity = 256;
    glm::uvec2 input_resolution = { 65, 65 };
    glm::uvec2 output_resolution = { 65, 65 };

    auto node_graph = std::make_unique<NodeGraph>();
    Node* tile_select_node = node_graph->add_node("select_tiles_node", std::make_unique<SelectTilesNode>(tile_id_generator_func));
    Node* height_request_node = node_graph->add_node("request_height_node", std::make_unique<RequestTilesNode>());
    Node* hash_map_node
        = node_graph->add_node("hashmap_node", std::make_unique<CreateHashMapNode>(device, input_resolution, capacity, WGPUTextureFormat_R16Uint));
    Node* snow_compute_node = node_graph->add_node(
        "compute_snow_node", std::make_unique<ComputeSnowNode>(manager, device, output_resolution, capacity, WGPUTextureFormat_RGBA8Unorm));
    DownsampleTilesNode* downsample_tiles_node
        = static_cast<DownsampleTilesNode*>(node_graph->add_node("downsample_tiles_node", std::make_unique<DownsampleTilesNode>(manager, device, capacity, 3)));

    tile_select_node->output_socket("tile ids").connect(height_request_node->input_socket("tile ids"));

    tile_select_node->output_socket("tile ids").connect(hash_map_node->input_socket("tile ids"));
    height_request_node->output_socket("tile data").connect(hash_map_node->input_socket("texture data"));

    tile_select_node->output_socket("tile ids").connect(snow_compute_node->input_socket("tile ids"));
    hash_map_node->output_socket("hash map").connect(snow_compute_node->input_socket("hash map"));
    hash_map_node->output_socket("textures").connect(snow_compute_node->input_socket("height textures"));

    tile_select_node->output_socket("tile ids").connect(downsample_tiles_node->input_socket("tile ids"));
    snow_compute_node->output_socket("hash map").connect(downsample_tiles_node->input_socket("hash map"));
    snow_compute_node->output_socket("snow textures").connect(downsample_tiles_node->input_socket("textures"));

    node_graph->m_output_hash_map_ptr = &downsample_tiles_node->hash_map();
    node_graph->m_output_texture_storage_ptr = &downsample_tiles_node->texture_storage();

    // connect signals
    // TODO do dynamically based on graph
    node_graph->m_start_node = tile_select_node;
    connect(tile_select_node, &Node::run_finished, height_request_node, &Node::run);
    connect(height_request_node, &Node::run_finished, hash_map_node, &Node::run);
    connect(hash_map_node, &Node::run_finished, snow_compute_node, &Node::run);
    connect(snow_compute_node, &Node::run_finished, downsample_tiles_node, &Node::run);
    connect(downsample_tiles_node, &Node::run_finished, node_graph.get(), &NodeGraph::run_finished); // emits run finished signal in NodeGraph

    return node_graph;
}

std::unique_ptr<NodeGraph> NodeGraph::create_avalanche_trajectories_compute_graph(const PipelineManager& manager, WGPUDevice device)
{
    size_t capacity = 1024;
    glm::uvec2 input_resolution = { 65, 65 };
    glm::uvec2 normal_output_resolution = { 65, 65 };
    glm::uvec2 area_of_influence_output_resolution = { 256, 256 };
    glm::uvec2 upsample_output_resolution = { 256, 256 };

    auto node_graph = std::make_unique<NodeGraph>();

    Node* target_tile_select_node = node_graph->add_node("select_target_tiles_node", std::make_unique<SelectTilesNode>());
    Node* source_tile_select_node = node_graph->add_node("select_source_tiles_node", std::make_unique<SelectTilesNode>());

    Node* height_request_node = node_graph->add_node("request_height_node", std::make_unique<RequestTilesNode>());
    Node* hash_map_node
        = node_graph->add_node("create_hashmap_node", std::make_unique<CreateHashMapNode>(device, input_resolution, capacity, WGPUTextureFormat_R16Uint));
    ComputeNormalsNode* normal_compute_node = static_cast<ComputeNormalsNode*>(node_graph->add_node(
        "compute_normals_node", std::make_unique<ComputeNormalsNode>(manager, device, normal_output_resolution, capacity, WGPUTextureFormat_RGBA8Unorm)));
    ComputeAvalancheTrajectoriesNode* avalanche_trajectories_compute_node = static_cast<ComputeAvalancheTrajectoriesNode*>(node_graph->add_node(
        "compute_area_of_influence_node", std::make_unique<ComputeAvalancheTrajectoriesNode>(manager, device, area_of_influence_output_resolution, capacity)));
    ComputeAvalancheTrajectoriesBufferToTextureNode* avalanche_trajectories_buffer_to_texture_compute_node
        = static_cast<ComputeAvalancheTrajectoriesBufferToTextureNode*>(node_graph->add_node("avalanche_trajectories_buffer_to_texture_compute_node",
            std::make_unique<ComputeAvalancheTrajectoriesBufferToTextureNode>(
                manager, device, area_of_influence_output_resolution, capacity, WGPUTextureFormat_RGBA8Unorm)));
    Node* upsample_normals_textures_node
        = node_graph->add_node("upsample_textures_node", std::make_unique<UpsampleTexturesNode>(manager, device, upsample_output_resolution, capacity));
    DownsampleTilesNode* downsample_area_of_influence_tiles_node = static_cast<DownsampleTilesNode*>(
        node_graph->add_node("downsample_area_of_influence_tiles_node", std::make_unique<DownsampleTilesNode>(manager, device, capacity, 5)));
    DownsampleTilesNode* downsample_normals_tiles_node = static_cast<DownsampleTilesNode*>(
        node_graph->add_node("downsample_normals_tiles_node", std::make_unique<DownsampleTilesNode>(manager, device, capacity, 5)));

    // connect tile request node inputs
    source_tile_select_node->output_socket("tile ids").connect(height_request_node->input_socket("tile ids"));

    // connect hash map node inputs
    source_tile_select_node->output_socket("tile ids").connect(hash_map_node->input_socket("tile ids"));
    height_request_node->output_socket("tile data").connect(hash_map_node->input_socket("texture data"));

    // connect normal node inputs
    source_tile_select_node->output_socket("tile ids").connect(normal_compute_node->input_socket("tile ids"));
    hash_map_node->output_socket("hash map").connect(normal_compute_node->input_socket("hash map"));
    hash_map_node->output_socket("textures").connect(normal_compute_node->input_socket("height textures"));

    // connect trajectories node inputs
    target_tile_select_node->output_socket("tile ids").connect(avalanche_trajectories_compute_node->input_socket("tile ids"));
    normal_compute_node->output_socket("hash map").connect(avalanche_trajectories_compute_node->input_socket("hash map"));
    normal_compute_node->output_socket("normal textures").connect(avalanche_trajectories_compute_node->input_socket("normal textures"));
    hash_map_node->output_socket("textures").connect(avalanche_trajectories_compute_node->input_socket("height textures"));

    // connect trajectories buffer to texture node inputs
    target_tile_select_node->output_socket("tile ids").connect(avalanche_trajectories_buffer_to_texture_compute_node->input_socket("tile ids"));
    avalanche_trajectories_compute_node->output_socket("hash map").connect(avalanche_trajectories_buffer_to_texture_compute_node->input_socket("hash map"));
    avalanche_trajectories_compute_node->output_socket("storage buffer")
        .connect(avalanche_trajectories_buffer_to_texture_compute_node->input_socket("storage buffer"));

    // create downsampled area of influence tiles
    target_tile_select_node->output_socket("tile ids").connect(downsample_area_of_influence_tiles_node->input_socket("tile ids"));
    avalanche_trajectories_compute_node->output_socket("hash map").connect(downsample_area_of_influence_tiles_node->input_socket("hash map"));
    avalanche_trajectories_buffer_to_texture_compute_node->output_socket("textures").connect(downsample_area_of_influence_tiles_node->input_socket("textures"));

    // connect upsample textures node inputs
    normal_compute_node->output_socket("normal textures").connect(upsample_normals_textures_node->input_socket("source textures"));

    // connect downsample normal tiles node inputs
    source_tile_select_node->output_socket("tile ids").connect(downsample_normals_tiles_node->input_socket("tile ids"));
    normal_compute_node->output_socket("hash map").connect(downsample_normals_tiles_node->input_socket("hash map"));
    upsample_normals_textures_node->output_socket("output textures").connect(downsample_normals_tiles_node->input_socket("textures"));

    node_graph->m_output_hash_map_ptr = &downsample_normals_tiles_node->hash_map();
    node_graph->m_output_texture_storage_ptr = &downsample_normals_tiles_node->texture_storage();

    node_graph->m_output_hash_map_ptr_2 = &downsample_area_of_influence_tiles_node->hash_map();
    node_graph->m_output_texture_storage_ptr_2 = &downsample_area_of_influence_tiles_node->texture_storage();

    // connect signals
    // TODO do dynamically based on graph
    node_graph->m_start_node = source_tile_select_node;
    connect(source_tile_select_node, &Node::run_finished, target_tile_select_node, &Node::run);
    connect(target_tile_select_node, &Node::run_finished, height_request_node, &Node::run);
    connect(height_request_node, &Node::run_finished, hash_map_node, &Node::run);
    connect(hash_map_node, &Node::run_finished, normal_compute_node, &Node::run);
    connect(normal_compute_node, &Node::run_finished, avalanche_trajectories_compute_node, &Node::run);
    connect(normal_compute_node, &Node::run_finished, upsample_normals_textures_node, &Node::run);
    connect(upsample_normals_textures_node, &Node::run_finished, downsample_normals_tiles_node, &Node::run);
    connect(avalanche_trajectories_compute_node, &Node::run_finished, avalanche_trajectories_buffer_to_texture_compute_node, &Node::run);
    connect(avalanche_trajectories_buffer_to_texture_compute_node, &Node::run_finished, downsample_area_of_influence_tiles_node, &Node::run);
    // TODO NodeGraph should keep track of all the currently running nodes and emit run_finished when all of them are done. Currently run_finished gets executed
    // twice which in our setup is okay (for now)
    connect(downsample_area_of_influence_tiles_node, &Node::run_finished, node_graph.get(), &NodeGraph::run_finished);
    // connect(downsample_normals_tiles_node, &Node::run_finished, node_graph.get(), &NodeGraph::run_finished);
    return node_graph;
}

std::unique_ptr<NodeGraph> NodeGraph::create_avalanche_influence_area_compute_graph(const PipelineManager& manager, WGPUDevice device)
{
    size_t capacity = 1024;
    glm::uvec2 input_resolution = { 65, 65 };
    glm::uvec2 normal_output_resolution = { 65, 65 };
    glm::uvec2 area_of_influence_output_resolution = { 256, 256 };
    glm::uvec2 upsample_output_resolution = { 256, 256 };

    auto node_graph = std::make_unique<NodeGraph>();

    Node* target_tile_select_node = node_graph->add_node("select_target_tiles_node", std::make_unique<SelectTilesNode>());
    Node* source_tile_select_node = node_graph->add_node("select_source_tiles_node", std::make_unique<SelectTilesNode>());

    Node* height_request_node = node_graph->add_node("request_height_node", std::make_unique<RequestTilesNode>());
    Node* hash_map_node
        = node_graph->add_node("create_hashmap_node", std::make_unique<CreateHashMapNode>(device, input_resolution, capacity, WGPUTextureFormat_R16Uint));
    ComputeNormalsNode* normal_compute_node = static_cast<ComputeNormalsNode*>(node_graph->add_node(
        "compute_normals_node", std::make_unique<ComputeNormalsNode>(manager, device, normal_output_resolution, capacity, WGPUTextureFormat_RGBA8Unorm)));
    ComputeAvalancheInfluenceAreaNode* avalanche_influence_area_compute_node
        = static_cast<ComputeAvalancheInfluenceAreaNode*>(node_graph->add_node("compute_area_of_influence_node",
            std::make_unique<ComputeAvalancheInfluenceAreaNode>(manager, device, area_of_influence_output_resolution, capacity, WGPUTextureFormat_RGBA8Unorm)));
    Node* upsample_normals_textures_node
        = node_graph->add_node("upsample_textures_node", std::make_unique<UpsampleTexturesNode>(manager, device, upsample_output_resolution, capacity));
    DownsampleTilesNode* downsample_area_of_influence_tiles_node = static_cast<DownsampleTilesNode*>(
        node_graph->add_node("downsample_area_of_influence_tiles_node", std::make_unique<DownsampleTilesNode>(manager, device, capacity, 5)));
    DownsampleTilesNode* downsample_normals_tiles_node = static_cast<DownsampleTilesNode*>(
        node_graph->add_node("downsample_normals_tiles_node", std::make_unique<DownsampleTilesNode>(manager, device, capacity, 5)));

    // connect tile request node inputs
    source_tile_select_node->output_socket("tile ids").connect(height_request_node->input_socket("tile ids"));

    // connect hash map node inputs
    source_tile_select_node->output_socket("tile ids").connect(hash_map_node->input_socket("tile ids"));
    height_request_node->output_socket("tile data").connect(hash_map_node->input_socket("texture data"));

    // connect normal node inputs
    source_tile_select_node->output_socket("tile ids").connect(normal_compute_node->input_socket("tile ids"));
    hash_map_node->output_socket("hash map").connect(normal_compute_node->input_socket("hash map"));
    hash_map_node->output_socket("textures").connect(normal_compute_node->input_socket("height textures"));

    // connect influence area compute node inputs
    target_tile_select_node->output_socket("tile ids").connect(avalanche_influence_area_compute_node->input_socket("tile ids"));
    normal_compute_node->output_socket("hash map").connect(avalanche_influence_area_compute_node->input_socket("hash map"));
    normal_compute_node->output_socket("normal textures").connect(avalanche_influence_area_compute_node->input_socket("normal textures"));
    hash_map_node->output_socket("textures").connect(avalanche_influence_area_compute_node->input_socket("height textures"));

    // create downsampled area of influence tiles
    target_tile_select_node->output_socket("tile ids").connect(downsample_area_of_influence_tiles_node->input_socket("tile ids"));
    avalanche_influence_area_compute_node->output_socket("hash map").connect(downsample_area_of_influence_tiles_node->input_socket("hash map"));
    avalanche_influence_area_compute_node->output_socket("influence area textures").connect(downsample_area_of_influence_tiles_node->input_socket("textures"));

    // connect upsample textures node inputs
    normal_compute_node->output_socket("normal textures").connect(upsample_normals_textures_node->input_socket("source textures"));

    // connect downsample normal tiles node inputs
    source_tile_select_node->output_socket("tile ids").connect(downsample_normals_tiles_node->input_socket("tile ids"));
    normal_compute_node->output_socket("hash map").connect(downsample_normals_tiles_node->input_socket("hash map"));
    upsample_normals_textures_node->output_socket("output textures").connect(downsample_normals_tiles_node->input_socket("textures"));

    node_graph->m_output_hash_map_ptr = &downsample_normals_tiles_node->hash_map();
    node_graph->m_output_texture_storage_ptr = &downsample_normals_tiles_node->texture_storage();

    node_graph->m_output_hash_map_ptr_2 = &downsample_area_of_influence_tiles_node->hash_map();
    node_graph->m_output_texture_storage_ptr_2 = &downsample_area_of_influence_tiles_node->texture_storage();

    // connect signals
    // TODO do dynamically based on graph
    node_graph->m_start_node = source_tile_select_node;
    connect(source_tile_select_node, &Node::run_finished, target_tile_select_node, &Node::run);
    connect(target_tile_select_node, &Node::run_finished, height_request_node, &Node::run);
    connect(height_request_node, &Node::run_finished, hash_map_node, &Node::run);
    connect(hash_map_node, &Node::run_finished, normal_compute_node, &Node::run);
    connect(normal_compute_node, &Node::run_finished, avalanche_influence_area_compute_node, &Node::run);
    connect(normal_compute_node, &Node::run_finished, upsample_normals_textures_node, &Node::run);
    connect(upsample_normals_textures_node, &Node::run_finished, downsample_normals_tiles_node, &Node::run);
    connect(avalanche_influence_area_compute_node, &Node::run_finished, downsample_area_of_influence_tiles_node, &Node::run);
    // TODO NodeGraph should keep track of all the currently running nodes and emit run_finished when all of them are done. Currently run_finished gets executed
    // twice which in our setup is okay (for now)
    connect(downsample_area_of_influence_tiles_node, &Node::run_finished, node_graph.get(), &NodeGraph::run_finished);
    connect(downsample_normals_tiles_node, &Node::run_finished, node_graph.get(), &NodeGraph::run_finished);

    return node_graph;
}

} // namespace webgpu_engine::compute::nodes
