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

#include "ComputeAreaOfInfluenceNode.h"

#include "nucleus/srs.h"
#include <QDebug>

namespace webgpu_engine::compute::nodes {

glm::uvec3 ComputeAreaOfInfluenceNode::SHADER_WORKGROUP_SIZE = { 1, 16, 16 };

ComputeAreaOfInfluenceNode::ComputeAreaOfInfluenceNode(
    const PipelineManager& pipeline_manager, WGPUDevice device, const glm::uvec2& output_resolution, size_t capacity, WGPUTextureFormat output_format)
    : Node({ data_type<const std::vector<tile::Id>*>(), data_type<GpuHashMap<tile::Id, uint32_t, GpuTileId>*>(), data_type<TileStorageTexture*>() },
          { data_type<GpuHashMap<tile::Id, uint32_t, GpuTileId>*>(), data_type<TileStorageTexture*>() })
    , m_pipeline_manager { &pipeline_manager }
    , m_device { device }
    , m_queue(wgpuDeviceGetQueue(m_device))
    , m_capacity { capacity }
    , m_tile_bounds(
          device, WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc, capacity, "area of influence compute, tile bounds buffer")
    , m_input_tile_ids(
          device, WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc, capacity, "area of influence compute, tile id buffer")
    , m_input_settings(device, WGPUBufferUsage_CopyDst | WGPUBufferUsage_Uniform)
    , m_output_tile_map(device, tile::Id { unsigned(-1), {} }, -1)
    , m_output_texture(device, output_resolution, capacity, output_format)
{
    m_output_tile_map.update_gpu_data();
}

void ComputeAreaOfInfluenceNode::set_coords(const glm::dvec2& coords) { m_coords = coords; }

void ComputeAreaOfInfluenceNode::run_impl()
{
    qDebug() << "running AreaOfInfluenceNode ...";

    // get tile ids to process
    const auto& tile_ids = *std::get<data_type<const std::vector<tile::Id>*>()>(get_input_data(0)); // list of tile ids to process
    const auto& hash_map = *std::get<data_type<GpuHashMap<tile::Id, uint32_t, GpuTileId>*>()>(get_input_data(1)); // hash map for normal lookup
    const auto& normal_textures = *std::get<data_type<TileStorageTexture*>()>(get_input_data(2)); // hash map for lookup

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

    // update input settings on GPU side
    glm::dvec2 world_pos = nucleus::srs::lat_long_to_world(m_coords);
    tile::Id tile_id = nucleus::srs::world_xy_to_tile_id({ world_pos.x, world_pos.y }, 18);
    glm::dvec2 uv = nucleus::srs::world_xy_to_tile_uv({ world_pos.x, world_pos.y }, 18);
    m_input_settings.data.coords = glm::vec4 { world_pos.x, world_pos.y, 0, 0 };
    m_input_settings.data.tile_id = glm::uvec4 { tile_id.coords.x, tile_id.coords.y, tile_id.zoom_level, 0 };
    m_input_settings.data.uv = glm::vec4 { uv.x, uv.y, 0.0, 0.0 };
    m_input_settings.update_gpu_data(m_queue);

    // create bind group
    // TODO re-create bind groups only when input handles change
    // TODO adapter shader code
    // TODO compute bounds in other node!
    WGPUBindGroupEntry input_tile_ids_entry = m_input_tile_ids.create_bind_group_entry(0);
    WGPUBindGroupEntry input_bounds_entry = m_tile_bounds.create_bind_group_entry(1);
    WGPUBindGroupEntry input_settings_buffer_entry = m_input_settings.raw_buffer().create_bind_group_entry(2);
    WGPUBindGroupEntry input_hash_map_key_buffer_entry = hash_map.key_buffer().create_bind_group_entry(3);
    WGPUBindGroupEntry input_hash_map_value_buffer_entry = hash_map.value_buffer().create_bind_group_entry(4);
    WGPUBindGroupEntry input_normals_texture_array_entry = normal_textures.texture().texture_view().create_bind_group_entry(5);
    WGPUBindGroupEntry input_normals_texture_sampler_entry = normal_textures.texture().sampler().create_bind_group_entry(6);
    WGPUBindGroupEntry output_texture_array_entry = m_output_texture.texture().texture_view().create_bind_group_entry(7);
    std::vector<WGPUBindGroupEntry> entries { input_tile_ids_entry, input_bounds_entry, input_settings_buffer_entry, input_hash_map_key_buffer_entry,
        input_hash_map_value_buffer_entry, input_normals_texture_array_entry, input_normals_texture_sampler_entry, output_texture_array_entry };
    webgpu::raii::BindGroup compute_bind_group(
        m_device, m_pipeline_manager->area_of_influence_bind_group_layout(), entries, "area of influence compute bind group");

    // bind GPU resources and run pipeline
    // the result is a texture array with the calculated overlays, and a hashmap that maps id to texture array index
    // the shader will only writes into texture array, the hashmap is written on cpu side
    {
        WGPUCommandEncoderDescriptor descriptor {};
        descriptor.label = "area of influence compute command encoder";
        webgpu::raii::CommandEncoder encoder(m_device, descriptor);

        {
            WGPUComputePassDescriptor compute_pass_desc {};
            compute_pass_desc.label = "area of influence compute pass";
            webgpu::raii::ComputePassEncoder compute_pass(encoder.handle(), compute_pass_desc);

            glm::uvec3 workgroup_counts
                = glm::ceil(glm::vec3(tile_ids.size(), m_output_texture.width(), m_output_texture.height()) / glm::vec3(SHADER_WORKGROUP_SIZE));
            wgpuComputePassEncoderSetBindGroup(compute_pass.handle(), 0, compute_bind_group.handle(), 0, nullptr);
            m_pipeline_manager->area_of_influence_compute_pipeline().run(compute_pass, workgroup_counts);
        }

        WGPUCommandBufferDescriptor cmd_buffer_descriptor {};
        cmd_buffer_descriptor.label = "area of influence compute command buffer";
        WGPUCommandBuffer command = wgpuCommandEncoderFinish(encoder.handle(), &cmd_buffer_descriptor);
        wgpuQueueSubmit(m_queue, 1, &command);
        wgpuCommandBufferRelease(command);
    }

    // write hashmap
    // since the compute pass stores textures at indices [0, num_tile_ids), we can just write those indices into the hashmap
    m_output_tile_map.clear();
    m_output_texture.clear();
    for (uint16_t i = 0; i < tile_ids.size(); i++) {
        m_output_texture.reserve(i);
        m_output_tile_map.store(tile_ids[i], i);
    }
    m_output_tile_map.update_gpu_data();

    wgpuQueueOnSubmittedWorkDone(
        m_queue,
        []([[maybe_unused]] WGPUQueueWorkDoneStatus status, void* user_data) {
            ComputeAreaOfInfluenceNode* _this = reinterpret_cast<ComputeAreaOfInfluenceNode*>(user_data);
            _this->run_finished(); // emits signal run_finished()
        },
        this);
}

Data ComputeAreaOfInfluenceNode::get_output_data_impl(SocketIndex output_index)
{
    switch (output_index) {
    case Output::OUTPUT_TILE_ID_TO_TEXTURE_ARRAY_INDEX_MAP:
        return { &m_output_tile_map };
    case Output::OUTPUT_TEXTURE_ARRAY:
        return { &m_output_texture };
    }
    exit(-1);
}

} // namespace webgpu_engine::compute::nodes