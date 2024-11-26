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

#include "BufferToTextureNode.h"

namespace webgpu_engine::compute::nodes {

glm::uvec3 BufferToTextureNode::SHADER_WORKGROUP_SIZE = { 1, 16, 16 };

BufferToTextureNode::BufferToTextureNode(
    const PipelineManager& pipeline_manager, WGPUDevice device, const glm::uvec2& output_resolution, size_t capacity, WGPUTextureFormat output_format)
    : Node(
          {
              InputSocket(*this, "tile ids", data_type<const std::vector<tile::Id>*>()),
              InputSocket(*this, "hash map", data_type<GpuHashMap<tile::Id, uint32_t, GpuTileId>*>()),
              InputSocket(*this, "storage buffer", data_type<webgpu::raii::RawBuffer<uint32_t>*>()),
          },
          {
              OutputSocket(*this, "textures", data_type<TileStorageTexture*>(), [this]() { return &m_output_texture; }),
          })
    , m_pipeline_manager { &pipeline_manager }
    , m_device { device }
    , m_queue { wgpuDeviceGetQueue(device) }
    , m_input_tile_ids(
          device, WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc, capacity, "avalanche trajectories compute, tile ids")
    , m_output_texture(device, output_resolution, capacity, output_format)
{
}

void BufferToTextureNode::run_impl()
{
    qDebug() << "running ComputeAvalancheTrajectoriesBufferToTextureNode ...";
    const auto& tile_ids = *std::get<data_type<const std::vector<tile::Id>*>()>(input_socket("tile ids").get_connected_data()); // list of tile ids to process
    const auto& hash_map = *std::get<data_type<GpuHashMap<tile::Id, uint32_t, GpuTileId>*>()>(input_socket("hash map").get_connected_data());
    const auto& input_storage_buffer = *std::get<data_type<webgpu::raii::RawBuffer<uint32_t>*>()>(input_socket("storage buffer").get_connected_data());

    std::vector<GpuTileId> gpu_tile_ids(tile_ids.size());
    std::vector<glm::vec4> tile_bounds(tile_ids.size());
    for (size_t i = 0; i < gpu_tile_ids.size(); i++) {
        gpu_tile_ids[i] = { tile_ids[i].coords.x, tile_ids[i].coords.y, tile_ids[i].zoom_level };
    }
    m_input_tile_ids.write(m_queue, gpu_tile_ids.data(), gpu_tile_ids.size());

    // mark texture array elements as used
    m_output_texture.clear();
    for (const auto& tile_id : tile_ids) {
        m_output_texture.reserve(hash_map.value_at(tile_id));
    }

    // create bind group
    // TODO re-create bind groups only when input handles change
    // TODO adapter shader code
    // TODO compute bounds in other node!
    std::vector<WGPUBindGroupEntry> entries {
        m_input_tile_ids.create_bind_group_entry(0),
        hash_map.key_buffer().create_bind_group_entry(1),
        hash_map.value_buffer().create_bind_group_entry(2),
        input_storage_buffer.create_bind_group_entry(3),
        m_output_texture.texture().texture_view().create_bind_group_entry(4),
    };
    webgpu::raii::BindGroup compute_bind_group(
        m_device, m_pipeline_manager->buffer_to_texture_bind_group_layout(), entries, "avalanche trajectories buffer to texture compute bind group");

    // bind GPU resources and run pipeline
    {
        WGPUCommandEncoderDescriptor descriptor {};
        descriptor.label = "avalanche trajectories buffer to texture compute command encoder";
        webgpu::raii::CommandEncoder encoder(m_device, descriptor);

        {
            WGPUComputePassDescriptor compute_pass_desc {};
            compute_pass_desc.label = "avalanche trajectories buffer to texture compute pass";
            webgpu::raii::ComputePassEncoder compute_pass(encoder.handle(), compute_pass_desc);

            glm::uvec3 workgroup_counts
                = glm::ceil(glm::vec3(m_output_texture.num_used(), m_output_texture.width(), m_output_texture.height()) / glm::vec3(SHADER_WORKGROUP_SIZE));
            wgpuComputePassEncoderSetBindGroup(compute_pass.handle(), 0, compute_bind_group.handle(), 0, nullptr);
            m_pipeline_manager->buffer_to_texture_compute_pipeline().run(compute_pass, workgroup_counts);
        }

        WGPUCommandBufferDescriptor cmd_buffer_descriptor {};
        cmd_buffer_descriptor.label = "avalanche trajectories buffer to texture compute command buffer";
        WGPUCommandBuffer command = wgpuCommandEncoderFinish(encoder.handle(), &cmd_buffer_descriptor);
        wgpuQueueSubmit(m_queue, 1, &command);
        wgpuCommandBufferRelease(command);
    }
    wgpuQueueOnSubmittedWorkDone(
        m_queue,
        []([[maybe_unused]] WGPUQueueWorkDoneStatus status, void* user_data) {
            BufferToTextureNode* _this = reinterpret_cast<BufferToTextureNode*>(user_data);
            _this->run_completed(); // emits signal run_finished()
        },
        this);
}

} // namespace webgpu_engine::compute::nodes
