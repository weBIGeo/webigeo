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

#include "nodes.h"
#include "nucleus/srs.h"
#include "webgpu_engine/compute.h"
#include <QEventLoop>

namespace webgpu_engine {

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

Data Node::get_output_data(size_t output_index) const
{
    assert(output_index < output_socket_types.size());

    // get output data for output socket index, implemented in subclasses
    Data data = get_output_data_impl(output_index);

    assert(output_socket_types[output_index] == data.index()); // implementation returned correct type
    return data;
}

Data Node::get_input_data(size_t input_index) const
{
    assert(input_index < input_socket_types.size());
    assert(connected_input_sockets[input_index].connected_node != nullptr);

    // get output data from the output socket connected to this input socket
    Data data = connected_input_sockets[input_index].connected_node->get_output_data(connected_input_sockets[input_index].connected_socket_index);

    assert(input_socket_types[input_index] == data.index()); // correct type
    return data;
}

TileSelectNode::TileSelectNode()
    : Node({}, { data_type<const std::vector<tile::Id>*>() })
{
}

void TileSelectNode::run()
{
    qDebug() << "running TileSelectNode ..." << Qt::endl;

    webgpu_engine::RectangularTileRegion region { .min = { 1096, 1328 },
        .max = { 1096 + 14, 1328 + 14 }, // inclusive, so this region has 15x15 tiles
        .zoom_level = 11,
        .scheme = tile::Scheme::Tms };
    m_output_tile_ids = region.get_tiles();
    emit run_finished();
}

Data TileSelectNode::get_output_data_impl([[maybe_unused]] SocketIndex output_index) const { return { &m_output_tile_ids }; }

HeightRequestNode::HeightRequestNode()
    : Node({ data_type<const std::vector<tile::Id>*>() }, { data_type<const std::vector<QByteArray>*>() })
    , m_tile_loader { std::make_unique<nucleus::tile_scheduler::TileLoadService>(
          "https://alpinemaps.cg.tuwien.ac.at/tiles/alpine_png/", nucleus::tile_scheduler::TileLoadService::UrlPattern::ZXY, ".png") }
{
    connect(m_tile_loader.get(), &nucleus::tile_scheduler::TileLoadService::load_finished, this, &HeightRequestNode::on_single_tile_received);
}

void HeightRequestNode::run()
{
    qDebug() << "running HeightRequestNode ..." << Qt::endl;

    // get tile ids to request
    // TODO maybe make get_input_data a template (so usage would become get_input_data<type>(socket_index))
    const auto& tile_ids = *std::get<data_type<const std::vector<tile::Id>*>()>(get_input_data(0)); // input 1, list of tile ids

    // send request for each tile
    m_received_tile_textures.resize(tile_ids.size());
    m_requested_tile_ids = tile_ids;
    m_num_tiles_requested = m_received_tile_textures.size();
    m_num_tiles_received = 0;
    std::cout << "requested " << m_num_tiles_requested << " tiles" << std::endl;
    for (const auto& tile_id : tile_ids) {
        m_tile_loader->load(tile_id);
    }
}

Data HeightRequestNode::get_output_data_impl([[maybe_unused]] SocketIndex output_index) const { return { &m_received_tile_textures }; }

void HeightRequestNode::on_single_tile_received(const nucleus::tile_scheduler::tile_types::TileLayer& tile)
{
    auto found_it = std::find(m_requested_tile_ids.begin(), m_requested_tile_ids.end(), tile.id);

    assert(found_it != m_requested_tile_ids.end()); // cannot receive tile id that was not requested

    size_t found_index = found_it - m_requested_tile_ids.begin();
    m_received_tile_textures[found_index] = *tile.data;

    m_num_tiles_received++;
    if (m_num_tiles_received == m_num_tiles_requested) {
        // emit all_tiles_received();
        emit run_finished();
    }
}

ConvertTilesToHashMapNode::ConvertTilesToHashMapNode(WGPUDevice device, const glm::uvec2& resolution, size_t capacity, WGPUTextureFormat format)
    : Node({ data_type<const std::vector<tile::Id>*>(), data_type<const std::vector<QByteArray>*>() },
        { data_type<const GpuHashMap<tile::Id, uint32_t, GpuTileId>*>(), data_type<const TileStorageTexture*>() })
    , m_device { device }
    , m_queue { wgpuDeviceGetQueue(device) }
    , m_output_tile_id_to_index(device, tile::Id { unsigned(-1), {} }, -1)
    , m_output_tile_textures(device, resolution, capacity, format, WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst)
{
    m_output_tile_id_to_index.update_gpu_data();
}

void ConvertTilesToHashMapNode::run()
{
    qDebug() << "running ConvertToHashMapNode ..." << Qt::endl;

    // get input data
    // TODO maybe make get_input_data a template (so usage would become get_input_data<type>(socket_index))
    const auto& tile_ids = *std::get<data_type<const std::vector<tile::Id>*>()>(get_input_data(0)); // input 1, list of tile ids
    const auto& textures = *std::get<data_type<const std::vector<QByteArray>*>()>(get_input_data(1)); // input 2, list of tile corresponding textures

    assert(tile_ids.size() == textures.size());

    // store each texture in texture array and store resulting index in hashmap
    for (size_t i = 0; i < tile_ids.size(); i++) {
        auto index = m_output_tile_textures.store(textures[i]);
        m_output_tile_id_to_index.store(tile_ids[i], uint32_t(index));
    }
    m_output_tile_id_to_index.update_gpu_data();

    wgpuQueueOnSubmittedWorkDone(
        m_queue,
        []([[maybe_unused]] WGPUQueueWorkDoneStatus status, void* user_data) {
            ConvertTilesToHashMapNode* _this = reinterpret_cast<ConvertTilesToHashMapNode*>(user_data);
            // qDebug() << "hash=" << gpu_hash(tile::Id { 11, { 1096, 1328 } }) << Qt::endl;
            // std::cout << " done, reading back buffer for debugging purposes..." << std::endl;
            // std::vector<GpuTileId> key_buffer_contents = _this->m_output_tile_id_to_index.key_buffer().read_back_sync(_this->m_device, 10000);
            // std::vector<uint32_t> value_buffer_contents = _this->m_output_tile_id_to_index.value_buffer().read_back_sync(_this->m_device, 10000);
            // std::cout << "done" << std::endl;

            _this->run_finished(); // emits signal run_finished()
        },
        this);
}

Data ConvertTilesToHashMapNode::get_output_data_impl(SocketIndex output_index) const
{
    // return pointers to hash map and texture array respectively
    switch (output_index) {
    case 0:
        return { &m_output_tile_id_to_index };
    case 1:
        return { &m_output_tile_textures };
    }
    exit(-1);
}

NormalComputeNode::NormalComputeNode(
    const PipelineManager& pipeline_manager, WGPUDevice device, const glm::uvec2& output_resolution, SocketIndex capacity, WGPUTextureFormat output_format)
    : Node({ data_type<const std::vector<tile::Id>*>(), data_type<const GpuHashMap<tile::Id, uint32_t, GpuTileId>*>(), data_type<const TileStorageTexture*>() },
        { data_type<const GpuHashMap<tile::Id, uint32_t, GpuTileId>*>(), data_type<const TileStorageTexture*>() })
    , m_pipeline_manager { &pipeline_manager }
    , m_device { device }
    , m_queue(wgpuDeviceGetQueue(m_device))
    , m_capacity { capacity }
    , m_tile_bounds(device, WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc, capacity, "normal compute, tile bounds buffer")
    , m_input_tile_ids(device, WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc, capacity, "normal compute, tile id buffer")
    , m_output_tile_map(device, tile::Id { unsigned(-1), {} }, -1)
    , m_output_texture(device, output_resolution, capacity, output_format)
{
    m_output_tile_map.update_gpu_data();
}

void NormalComputeNode::run()
{
    qDebug() << "running NormalComputeNode ..." << Qt::endl;

    // get tile ids to process
    const auto& tile_ids = *std::get<data_type<const std::vector<tile::Id>*>()>(get_input_data(0)); // list of tile ids to process
    const auto& hash_map = *std::get<data_type<const GpuHashMap<tile::Id, uint32_t, GpuTileId>*>()>(get_input_data(1)); // hash map for height lookup
    const auto& height_textures = *std::get<data_type<const TileStorageTexture*>()>(get_input_data(2)); // hash map for lookup

    assert(tile_ids.size() <= m_capacity);

    // calculate bounds per tile id, write tile ids and bounds to buffer
    std::vector<GpuTileId> gpu_tile_ids(tile_ids.size());
    std::vector<glm::vec4> tile_bounds(tile_ids.size());
    for (size_t i = 0; i < gpu_tile_ids.size(); i++) {
        gpu_tile_ids[i] = { tile_ids[i].coords.x, tile_ids[i].coords.y, tile_ids[i].zoom_level };
        tile::SrsBounds bounds = nucleus::srs::tile_bounds(tile_ids[i]);
        tile_bounds[i] = { bounds.min.x, bounds.min.y, bounds.max.x, bounds.max.y };
    }
    m_input_tile_ids.write(m_queue, gpu_tile_ids.data(), gpu_tile_ids.size());
    m_tile_bounds.write(m_queue, tile_bounds.data(), tile_bounds.size());

    // create bind group
    // TODO re-create bind groups only when input handles change
    // TODO adapter shader code
    // TODO compute bounds in other node!
    WGPUBindGroupEntry input_tile_ids_entry = m_input_tile_ids.create_bind_group_entry(0);
    WGPUBindGroupEntry input_bounds_entry = m_tile_bounds.create_bind_group_entry(1);
    WGPUBindGroupEntry input_hash_map_key_buffer_entry = hash_map.key_buffer().create_bind_group_entry(2);
    WGPUBindGroupEntry input_hash_map_value_buffer_entry = hash_map.value_buffer().create_bind_group_entry(3);
    WGPUBindGroupEntry input_height_texture_array_entry = height_textures.texture().texture_view().create_bind_group_entry(4);
    WGPUBindGroupEntry output_texture_array_entry = m_output_texture.texture().texture_view().create_bind_group_entry(5);
    std::vector<WGPUBindGroupEntry> entries { input_tile_ids_entry, input_bounds_entry, input_hash_map_key_buffer_entry, input_hash_map_value_buffer_entry,
        input_height_texture_array_entry, output_texture_array_entry };
    raii::BindGroup compute_bind_group(m_device, m_pipeline_manager->compute_bind_group_layout(), entries, "compute controller bind group");

    // bind GPU resources and run pipeline
    // the result is a texture array with the calculated overlays, and a hashmap that maps id to texture array index
    // the shader will only writes into texture array, the hashmap is written on cpu side
    {
        WGPUCommandEncoderDescriptor descriptor {};
        descriptor.label = "compute controller command encoder";
        raii::CommandEncoder encoder(m_device, descriptor);

        {
            WGPUComputePassDescriptor compute_pass_desc {};
            compute_pass_desc.label = "compute controller compute pass";
            raii::ComputePassEncoder compute_pass(encoder.handle(), compute_pass_desc);

            // const glm::uvec3& workgroup_counts = { tile_ids.size(), 1, 1 };
            glm::uvec3 workgroup_counts = { tile_ids.size(), 1, 1 };
            wgpuComputePassEncoderSetBindGroup(compute_pass.handle(), 0, compute_bind_group.handle(), 0, nullptr);
            m_pipeline_manager->dummy_compute_pipeline().run(compute_pass, workgroup_counts);
        }

        WGPUCommandBufferDescriptor cmd_buffer_descriptor {};
        cmd_buffer_descriptor.label = "NormalComputeNode command buffer";
        WGPUCommandBuffer command = wgpuCommandEncoderFinish(encoder.handle(), &cmd_buffer_descriptor);
        wgpuQueueSubmit(m_queue, 1, &command);
        wgpuCommandBufferRelease(command);
    }

    // write hashmap
    // since the compute pass stores textures at indices [0, num_tile_ids), we can just write those indices into the hashmap
    for (uint16_t i = 0; i < tile_ids.size(); i++) {
        m_output_tile_map.store(tile_ids[i], i);
    }
    m_output_tile_map.update_gpu_data();

    wgpuQueueOnSubmittedWorkDone(
        m_queue,
        []([[maybe_unused]] WGPUQueueWorkDoneStatus status, void* user_data) {
            NormalComputeNode* _this = reinterpret_cast<NormalComputeNode*>(user_data);
            _this->run_finished(); // emits signal run_finished()
        },
        this);
}

Data NormalComputeNode::get_output_data_impl(SocketIndex output_index) const
{
    switch (output_index) {
    case 0:
        return { &m_output_tile_map };
    case 1:
        return { &m_output_texture };
    }
    exit(-1);
}

void NodeGraph::connect_sockets(Node* from_node, SocketIndex output_socket, Node* to_node, SocketIndex input_socket)
{
    from_node->connect_output_socket(output_socket, to_node, input_socket);
    to_node->connect_input_socket(input_socket, from_node, output_socket);
}

void NodeGraph::init_test_node_graph(const PipelineManager& manager, WGPUDevice device)
{
    size_t capacity = 256;
    glm::uvec2 input_resolution = { 65, 65 };
    glm::uvec2 output_resolution = { 256, 256 };
    add_node(std::make_unique<TileSelectNode>());
    add_node(std::make_unique<HeightRequestNode>());
    add_node(std::make_unique<ConvertTilesToHashMapNode>(device, input_resolution, capacity, WGPUTextureFormat_R16Uint));
    add_node(std::make_unique<NormalComputeNode>(manager, device, output_resolution, capacity, WGPUTextureFormat_RGBA8Unorm));

    Node* tile_select_node = m_nodes[0].get();
    Node* height_request_node = m_nodes[1].get();
    Node* hash_map_node = m_nodes[2].get();
    Node* normal_compute_node = m_nodes[3].get();

    connect_sockets(tile_select_node, TileSelectNode::Output::TILE_ID_LIST, height_request_node, HeightRequestNode::Input::TILE_ID_LIST);

    connect_sockets(tile_select_node, TileSelectNode::Output::TILE_ID_LIST, hash_map_node, ConvertTilesToHashMapNode::Input::TILE_ID_LIST);
    connect_sockets(height_request_node, HeightRequestNode::Output::TILE_TEXTURE_LIST, hash_map_node, ConvertTilesToHashMapNode::Input::TILE_TEXTURE_LIST);

    connect_sockets(tile_select_node, TileSelectNode::Output::TILE_ID_LIST, normal_compute_node, NormalComputeNode::Input::TILE_ID_LIST_TO_PROCESS);
    connect_sockets(hash_map_node, ConvertTilesToHashMapNode::Output::TILE_ID_TO_TEXTURE_ARRAY_INDEX_MAP, normal_compute_node,
        NormalComputeNode::Input::TILE_ID_TO_TEXTURE_ARRAY_INDEX_MAP);
    connect_sockets(hash_map_node, ConvertTilesToHashMapNode::Output::TEXTURE_ARRAY, normal_compute_node, NormalComputeNode::Input::TEXTURE_ARRAY);

    // connect signals
    // TODO do dynamically based on graph
    connect(tile_select_node, &Node::run_finished, height_request_node, &Node::run);
    connect(height_request_node, &Node::run_finished, hash_map_node, &Node::run);
    connect(hash_map_node, &Node::run_finished, normal_compute_node, &Node::run);
    connect(normal_compute_node, &Node::run_finished, this, &NodeGraph::run_finished); // emits run finished signal in NodeGraph
}

void NodeGraph::run()
{
    Node* tile_select_node = m_nodes[0].get();
    tile_select_node->run();
}

} // namespace webgpu_engine
