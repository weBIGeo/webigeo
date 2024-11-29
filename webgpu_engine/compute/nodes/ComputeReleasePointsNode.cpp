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

#include "ComputeReleasePointsNode.h"

namespace webgpu_engine::compute::nodes {

glm::uvec3 ComputeReleasePointsNode::SHADER_WORKGROUP_SIZE = { 1, 16, 16 };

ComputeReleasePointsNode::ComputeReleasePointsNode(
    const PipelineManager& pipeline_manager, WGPUDevice device, const glm::uvec2& output_resolution, size_t capacity)
    : Node(
          {
              InputSocket(*this, "tile ids", data_type<const std::vector<tile::Id>*>()),
              InputSocket(*this, "hash map", data_type<GpuHashMap<tile::Id, uint32_t, GpuTileId>*>()),
              InputSocket(*this, "normal textures", data_type<TileStorageTexture*>()),
          },
          {
              OutputSocket(*this, "release point textures", data_type<TileStorageTexture*>(), [this]() { return &m_output_texture; }),
          })
    , m_pipeline_manager(&pipeline_manager)
    , m_device(device)
    , m_queue(wgpuDeviceGetQueue(device))
    , m_settings_uniform(device, WGPUBufferUsage_CopyDst | WGPUBufferUsage_Uniform)
    , m_input_tile_ids(device, WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc, capacity, "release points compute, tile ids")
    , m_output_texture(device, output_resolution, capacity, WGPUTextureFormat_RGBA8Unorm,
          WGPUTextureUsage_StorageBinding | WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst | WGPUTextureUsage_CopySrc)
{
}

void ComputeReleasePointsNode::run_impl()
{
    qDebug() << "running ComputeReleasePointsNode ...";

    const auto& tile_ids = *std::get<data_type<const std::vector<tile::Id>*>()>(input_socket("tile ids").get_connected_data());
    const auto& hash_map = *std::get<data_type<GpuHashMap<tile::Id, uint32_t, GpuTileId>*>()>(input_socket("hash map").get_connected_data());
    const auto& normal_textures = *std::get<data_type<TileStorageTexture*>()>(input_socket("normal textures").get_connected_data());

    if (tile_ids.size() > m_output_texture.capacity()) {
        emit run_failed(NodeRunFailureInfo(*this,
            std::format("failed to store textures on GPU: trying to calculate {} textures, but texture array capacity is {}", tile_ids.size(),
                m_output_texture.capacity())));
        return;
    }

    // calculate bounds per tile id, write tile ids and bounds to buffer
    std::vector<GpuTileId> gpu_tile_ids(tile_ids.size());
    for (size_t i = 0; i < gpu_tile_ids.size(); i++) {
        gpu_tile_ids[i] = { tile_ids[i].coords.x, tile_ids[i].coords.y, tile_ids[i].zoom_level };
    }
    m_input_tile_ids.write(m_queue, gpu_tile_ids.data(), gpu_tile_ids.size());

    // update input settings on GPU side
    m_settings_uniform.update_gpu_data(m_queue);

    // create bind group
    WGPUBindGroupEntry input_tile_ids_entry = m_input_tile_ids.create_bind_group_entry(0);
    WGPUBindGroupEntry input_settings_buffer_entry = m_settings_uniform.raw_buffer().create_bind_group_entry(1);
    WGPUBindGroupEntry input_hash_map_key_buffer_entry = hash_map.key_buffer().create_bind_group_entry(2);
    WGPUBindGroupEntry input_hash_map_value_buffer_entry = hash_map.value_buffer().create_bind_group_entry(3);
    WGPUBindGroupEntry input_normals_texture_array_entry = normal_textures.texture().texture_view().create_bind_group_entry(4);
    WGPUBindGroupEntry output_texture_entry = m_output_texture.texture().texture_view().create_bind_group_entry(5);

    std::vector<WGPUBindGroupEntry> entries {
        input_tile_ids_entry,
        input_settings_buffer_entry,
        input_hash_map_key_buffer_entry,
        input_hash_map_value_buffer_entry,
        input_normals_texture_array_entry,
        output_texture_entry,
    };
    webgpu::raii::BindGroup compute_bind_group(
        m_device, m_pipeline_manager->release_point_compute_bind_group_layout(), entries, "release points compute bind group");

    // bind GPU resources and run pipeline
    // the result is a texture array with the calculated overlays, and a hashmap that maps id to texture array index
    // the shader will only writes into texture array, the hashmap is written on cpu side
    {
        WGPUCommandEncoderDescriptor descriptor {};
        descriptor.label = "release points compute command encoder";
        webgpu::raii::CommandEncoder encoder(m_device, descriptor);

        {
            WGPUComputePassDescriptor compute_pass_desc {};
            compute_pass_desc.label = "release points compute pass";
            webgpu::raii::ComputePassEncoder compute_pass(encoder.handle(), compute_pass_desc);

            glm::uvec3 workgroup_counts
                = glm::ceil(glm::vec3(tile_ids.size(), m_output_texture.width(), m_output_texture.height()) / glm::vec3(SHADER_WORKGROUP_SIZE));
            wgpuComputePassEncoderSetBindGroup(compute_pass.handle(), 0, compute_bind_group.handle(), 0, nullptr);
            m_pipeline_manager->release_point_compute_pipeline().run(compute_pass, workgroup_counts);
        }

        WGPUCommandBufferDescriptor cmd_buffer_descriptor {};
        cmd_buffer_descriptor.label = "release points compute command buffer";
        WGPUCommandBuffer command = wgpuCommandEncoderFinish(encoder.handle(), &cmd_buffer_descriptor);
        wgpuQueueSubmit(m_queue, 1, &command);
        wgpuCommandBufferRelease(command);
    }
    wgpuQueueOnSubmittedWorkDone(
        m_queue,
        []([[maybe_unused]] WGPUQueueWorkDoneStatus status, void* user_data) {
            ComputeReleasePointsNode* _this = reinterpret_cast<ComputeReleasePointsNode*>(user_data);
            _this->run_completed(); // emits signal run_finished()
        },
        this);
}

} // namespace webgpu_engine::compute::nodes
