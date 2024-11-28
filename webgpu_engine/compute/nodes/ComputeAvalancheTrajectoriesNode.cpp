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

#include "ComputeAvalancheTrajectoriesNode.h"

#include "nucleus/srs.h"
#include <QDebug>

namespace webgpu_engine::compute::nodes {

glm::uvec3 ComputeAvalancheTrajectoriesNode::SHADER_WORKGROUP_SIZE = { 1, 16, 16 };

ComputeAvalancheTrajectoriesNode::ComputeAvalancheTrajectoriesNode(
    const PipelineManager& pipeline_manager, WGPUDevice device, const glm::uvec2& output_resolution, size_t capacity)
    : Node(
          {
              InputSocket(*this, "tile ids", data_type<const std::vector<tile::Id>*>()),
              InputSocket(*this, "hash map", data_type<GpuHashMap<tile::Id, uint32_t, GpuTileId>*>()),
              InputSocket(*this, "normal textures", data_type<TileStorageTexture*>()),
              InputSocket(*this, "height textures", data_type<TileStorageTexture*>()),
              InputSocket(*this, "release point textures", data_type<TileStorageTexture*>()),
          },
          {
              OutputSocket(*this, "hash map", data_type<GpuHashMap<tile::Id, uint32_t, GpuTileId>*>(), [this]() { return &m_output_tile_map; }),
              OutputSocket(*this, "storage buffer", data_type<webgpu::raii::RawBuffer<uint32_t>*>(), [this]() { return &m_output_storage_buffer; }),
          })
    , m_pipeline_manager { &pipeline_manager }
    , m_device { device }
    , m_queue(wgpuDeviceGetQueue(m_device))
    , m_capacity { capacity }
    , m_output_resolution { output_resolution }
    , m_tile_bounds(
          device, WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc, capacity, "avalanche trajectories compute, tile bounds buffer")
    , m_input_tile_ids(
          device, WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc, capacity, "avalanche trajectories compute, tile id buffer")
    , m_settings_uniform(device, WGPUBufferUsage_CopyDst | WGPUBufferUsage_Uniform)
    , m_output_tile_map(device, tile::Id { unsigned(-1), {} }, -1)
    , m_output_storage_buffer(device, WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc,
          capacity * output_resolution.x * output_resolution.y, "avalanche trajectories compute output storage")
{
    m_settings_uniform.data.output_resolution = m_output_resolution;
    m_output_tile_map.update_gpu_data();
}

void ComputeAvalancheTrajectoriesNode::update_gpu_settings()
{
    m_settings_uniform.data.sampling_interval = glm::uvec2(m_output_resolution / m_settings.trigger_points.sampling_density);

    m_settings_uniform.data.num_steps = m_settings.simulation.num_steps;
    m_settings_uniform.data.step_length = m_settings.simulation.step_length;
    m_settings_uniform.data.source_zoomlevel = m_settings.simulation.zoomlevel;

    m_settings_uniform.data.physics_model_type = m_settings.simulation.active_model;
    m_settings_uniform.data.model1_linear_drag_coeff = m_settings.simulation.model1.slowdown_coefficient;
    m_settings_uniform.data.model1_downward_acceleration_coeff = m_settings.simulation.model1.speedup_coefficient;
    m_settings_uniform.data.model2_gravity = m_settings.simulation.model2.gravity;
    m_settings_uniform.data.model2_mass = m_settings.simulation.model2.mass;
    m_settings_uniform.data.model2_friction_coeff = m_settings.simulation.model2.friction_coeff;
    m_settings_uniform.data.model2_drag_coeff = m_settings.simulation.model2.drag_coeff;

    m_settings_uniform.data.trigger_point_min_slope_angle = glm::radians(m_settings.trigger_points.min_slope_angle);
    m_settings_uniform.data.trigger_point_max_slope_angle = glm::radians(m_settings.trigger_points.max_slope_angle);

    for (uint8_t i = 0; i < sizeof(m_settings.simulation.model_d8_with_weights.weights.size()); i++) {
        m_settings_uniform.data.model_d8_with_weights_weights[i] = m_settings.simulation.model_d8_with_weights.weights[i];
    }
    m_settings_uniform.data.model_d8_with_weights_center_height_offset = m_settings.simulation.model_d8_with_weights.center_height_offset;

    m_settings_uniform.data.runout_model_type = m_settings.simulation.active_runout_model;
    m_settings_uniform.data.runout_perla_my = m_settings.simulation.perla.my;
    m_settings_uniform.data.runout_perla_md = m_settings.simulation.perla.md;
    m_settings_uniform.data.runout_perla_l = m_settings.simulation.perla.l;
    m_settings_uniform.data.runout_perla_g = m_settings.simulation.perla.g;

    m_settings_uniform.data.source_zoomlevel = m_settings.simulation.zoomlevel;
    m_settings_uniform.update_gpu_data(m_queue);
}

void ComputeAvalancheTrajectoriesNode::run_impl()
{
    qDebug() << "running ComputeAvalancheTrajectoriesNode ...";

    // get tile ids to process
    const auto& tile_ids = *std::get<data_type<const std::vector<tile::Id>*>()>(input_socket("tile ids").get_connected_data());
    const auto& hash_map = *std::get<data_type<GpuHashMap<tile::Id, uint32_t, GpuTileId>*>()>(input_socket("hash map").get_connected_data());
    const auto& normal_textures = *std::get<data_type<TileStorageTexture*>()>(input_socket("normal textures").get_connected_data());
    const auto& height_textures = *std::get<data_type<TileStorageTexture*>()>(input_socket("height textures").get_connected_data());
    const auto& release_point_textures = *std::get<data_type<TileStorageTexture*>()>(input_socket("release point textures").get_connected_data());

    if (tile_ids.size() > m_capacity) {
        emit run_failed(NodeRunFailureInfo(*this,
            std::format("failed to store textures in GPU hash map: trying to store {} textures, but hash map capacity is {}", tile_ids.size(), m_capacity)));
        return;
    }

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
    update_gpu_settings();

    // write hashmap
    // since the compute pass stores textures at indices [0, num_tile_ids), we can just write those indices into the hashmap
    m_output_tile_map.clear();
    for (uint16_t i = 0; i < tile_ids.size(); i++) {
        m_output_tile_map.store(tile_ids[i], i);
    }
    m_output_tile_map.update_gpu_data();

    // create bind group
    // TODO re-create bind groups only when input handles change
    // TODO adapter shader code
    // TODO compute bounds in other node!
    WGPUBindGroupEntry input_tile_ids_entry = m_input_tile_ids.create_bind_group_entry(0);
    WGPUBindGroupEntry input_bounds_entry = m_tile_bounds.create_bind_group_entry(1);
    WGPUBindGroupEntry input_settings_buffer_entry = m_settings_uniform.raw_buffer().create_bind_group_entry(2);
    WGPUBindGroupEntry input_hash_map_key_buffer_entry = hash_map.key_buffer().create_bind_group_entry(3);
    WGPUBindGroupEntry input_hash_map_value_buffer_entry = hash_map.value_buffer().create_bind_group_entry(4);
    WGPUBindGroupEntry input_normals_texture_array_entry = normal_textures.texture().texture_view().create_bind_group_entry(5);
    WGPUBindGroupEntry input_normals_texture_sampler_entry = normal_textures.texture().sampler().create_bind_group_entry(6);
    WGPUBindGroupEntry input_heights_texture_array_entry = height_textures.texture().texture_view().create_bind_group_entry(7);
    WGPUBindGroupEntry input_heights_texture_sampler_entry = height_textures.texture().sampler().create_bind_group_entry(8);
    WGPUBindGroupEntry input_release_point_texture_array_entry = release_point_textures.texture().texture_view().create_bind_group_entry(9);
    WGPUBindGroupEntry output_hash_map_key_buffer_entry = m_output_tile_map.key_buffer().create_bind_group_entry(10);
    WGPUBindGroupEntry output_hash_map_value_buffer_entry = m_output_tile_map.value_buffer().create_bind_group_entry(11);
    WGPUBindGroupEntry output_storage_buffer_entry = m_output_storage_buffer.create_bind_group_entry(12);

    std::vector<WGPUBindGroupEntry> entries {
        input_tile_ids_entry,
        input_bounds_entry,
        input_settings_buffer_entry,
        input_hash_map_key_buffer_entry,
        input_hash_map_value_buffer_entry,
        input_normals_texture_array_entry,
        input_normals_texture_sampler_entry,
        input_heights_texture_array_entry,
        input_heights_texture_sampler_entry,
        input_release_point_texture_array_entry,
        output_hash_map_key_buffer_entry,
        output_hash_map_value_buffer_entry,
        output_storage_buffer_entry,
    };
    webgpu::raii::BindGroup compute_bind_group(
        m_device, m_pipeline_manager->avalanche_trajectories_bind_group_layout(), entries, "avalanche trajectories compute bind group");

    // bind GPU resources and run pipeline
    // the result is a texture array with the calculated overlays, and a hashmap that maps id to texture array index
    // the shader will only writes into texture array, the hashmap is written on cpu side
    {
        WGPUCommandEncoderDescriptor descriptor {};
        descriptor.label = "avalanche trajectories compute command encoder";
        webgpu::raii::CommandEncoder encoder(m_device, descriptor);

        // TODO idk if necessary?
        wgpuCommandEncoderClearBuffer(encoder.handle(), m_output_storage_buffer.handle(), 0, m_output_storage_buffer.size_in_byte());

        {
            WGPUComputePassDescriptor compute_pass_desc {};
            compute_pass_desc.label = "avalanche trajectories compute pass";
            webgpu::raii::ComputePassEncoder compute_pass(encoder.handle(), compute_pass_desc);

            glm::uvec3 workgroup_counts
                = glm::ceil(glm::vec3(tile_ids.size(), m_output_resolution.x, m_output_resolution.y) / glm::vec3(SHADER_WORKGROUP_SIZE));
            wgpuComputePassEncoderSetBindGroup(compute_pass.handle(), 0, compute_bind_group.handle(), 0, nullptr);
            m_pipeline_manager->avalanche_trajectories_compute_pipeline().run(compute_pass, workgroup_counts);
        }

        WGPUCommandBufferDescriptor cmd_buffer_descriptor {};
        cmd_buffer_descriptor.label = "avalanche trajectories compute command buffer";
        WGPUCommandBuffer command = wgpuCommandEncoderFinish(encoder.handle(), &cmd_buffer_descriptor);
        wgpuQueueSubmit(m_queue, 1, &command);
        wgpuCommandBufferRelease(command);
    }
    wgpuQueueOnSubmittedWorkDone(
        m_queue,
        []([[maybe_unused]] WGPUQueueWorkDoneStatus status, void* user_data) {
            ComputeAvalancheTrajectoriesNode* _this = reinterpret_cast<ComputeAvalancheTrajectoriesNode*>(user_data);
            _this->run_completed(); // emits signal run_finished()
        },
        this);
}

} // namespace webgpu_engine::compute::nodes
