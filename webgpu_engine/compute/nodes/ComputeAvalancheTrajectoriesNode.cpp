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

#include <QDebug>

namespace webgpu_engine::compute::nodes {

glm::uvec3 ComputeAvalancheTrajectoriesNode::SHADER_WORKGROUP_SIZE = { 16, 16, 1 };

ComputeAvalancheTrajectoriesNode::ComputeAvalancheTrajectoriesNode(const PipelineManager& pipeline_manager, WGPUDevice device)
    : ComputeAvalancheTrajectoriesNode(pipeline_manager, device, AvalancheTrajectoriesSettings())
{
}

ComputeAvalancheTrajectoriesNode::ComputeAvalancheTrajectoriesNode(
    const PipelineManager& pipeline_manager, WGPUDevice device, const AvalancheTrajectoriesSettings& settings)
    : Node(
          {
              InputSocket(*this, "region aabb", data_type<const geometry::Aabb<2, double>*>()),
              InputSocket(*this, "normal texture", data_type<const webgpu::raii::TextureWithSampler*>()),
              InputSocket(*this, "height texture", data_type<const webgpu::raii::TextureWithSampler*>()),
              InputSocket(*this, "release point texture", data_type<const webgpu::raii::TextureWithSampler*>()),
          },
          {
              OutputSocket(*this, "storage buffer", data_type<webgpu::raii::RawBuffer<uint32_t>*>(), [this]() { return m_output_storage_buffer.get(); }),
              OutputSocket(*this, "raster dimensions", data_type<glm::uvec2>(), [this]() { return m_output_dimensions; }),
          })
    , m_pipeline_manager { &pipeline_manager }
    , m_device { device }
    , m_queue(wgpuDeviceGetQueue(m_device))
    , m_settings { settings }
    , m_settings_uniform(device, WGPUBufferUsage_CopyDst | WGPUBufferUsage_Uniform)
    , m_sampler(create_sampler(m_device))
{
}

void ComputeAvalancheTrajectoriesNode::update_gpu_settings()
{
    m_settings_uniform.data.num_steps = m_settings.num_steps;
    m_settings_uniform.data.step_length = m_settings.step_length;

    m_settings_uniform.data.physics_model_type = m_settings.active_model;
    m_settings_uniform.data.model1_linear_drag_coeff = m_settings.model1.slowdown_coefficient;
    m_settings_uniform.data.model1_downward_acceleration_coeff = m_settings.model1.speedup_coefficient;
    m_settings_uniform.data.model2_gravity = m_settings.model2.gravity;
    m_settings_uniform.data.model2_mass = m_settings.model2.mass;
    m_settings_uniform.data.model2_friction_coeff = m_settings.model2.friction_coeff;
    m_settings_uniform.data.model2_drag_coeff = m_settings.model2.drag_coeff;

    for (uint8_t i = 0; i < sizeof(m_settings.model_d8_with_weights.weights.size()); i++) {
        m_settings_uniform.data.model_d8_with_weights_weights[i] = m_settings.model_d8_with_weights.weights[i];
    }
    m_settings_uniform.data.model_d8_with_weights_center_height_offset = m_settings.model_d8_with_weights.center_height_offset;

    m_settings_uniform.data.runout_model_type = m_settings.active_runout_model;
    m_settings_uniform.data.runout_perla_my = m_settings.perla.my;
    m_settings_uniform.data.runout_perla_md = m_settings.perla.md;
    m_settings_uniform.data.runout_perla_l = m_settings.perla.l;
    m_settings_uniform.data.runout_perla_g = m_settings.perla.g;

    m_settings_uniform.update_gpu_data(m_queue);
}

void ComputeAvalancheTrajectoriesNode::run_impl()
{
    qDebug() << "running ComputeAvalancheTrajectoriesNode ...";

    const auto region_aabb = std::get<data_type<const geometry::Aabb<2, double>*>()>(input_socket("region aabb").get_connected_data());
    const auto& normal_texture = *std::get<data_type<const webgpu::raii::TextureWithSampler*>()>(input_socket("normal texture").get_connected_data());
    const auto& height_texture = *std::get<data_type<const webgpu::raii::TextureWithSampler*>()>(input_socket("height texture").get_connected_data());
    const auto& release_point_texture
        = *std::get<data_type<const webgpu::raii::TextureWithSampler*>()>(input_socket("release point texture").get_connected_data());

    const auto input_width = normal_texture.texture().width();
    const auto input_height = normal_texture.texture().height();

    // assert input textures have same size, otherwise fail run
    if (input_width != height_texture.texture().width() || input_height != height_texture.texture().height()
        || input_width != release_point_texture.texture().width() || input_height != release_point_texture.texture().height()) {
        emit run_failed(NodeRunFailureInfo(*this,
            std::format("failed to compute trajectories: input texture sizes must match (normals: {}x{}, heights: {}x{}, release points: {}x{})", input_width,
                input_height, height_texture.texture().width(), height_texture.texture().height(), release_point_texture.texture().width(),
                release_point_texture.texture().height())));
        return;
    }

    m_output_dimensions = glm::uvec2(input_width, input_height) * m_settings.resolution_multiplier;

    qDebug() << "input resolution: " << input_width << "x" << input_height;
    qDebug() << "output resolution: " << m_output_dimensions.x << "x" << m_output_dimensions.y;

    // create output storage buffer
    m_output_storage_buffer
        = std::make_unique<webgpu::raii::RawBuffer<uint32_t>>(m_device, WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc,
            m_output_dimensions.x * m_output_dimensions.y, "avalanche trajectories compute output storage");

    // update input settings on GPU side
    m_settings_uniform.data.output_resolution = m_output_dimensions;
    m_settings_uniform.data.region_size = glm::fvec2(region_aabb->size());
    update_gpu_settings();

    // create bind group
    std::vector<WGPUBindGroupEntry> entries {
        m_settings_uniform.raw_buffer().create_bind_group_entry(0),
        normal_texture.texture_view().create_bind_group_entry(1),
        height_texture.texture_view().create_bind_group_entry(2),
        release_point_texture.texture_view().create_bind_group_entry(3),
        m_sampler->create_bind_group_entry(4),
        m_output_storage_buffer->create_bind_group_entry(5),
    };

    webgpu::raii::BindGroup compute_bind_group(
        m_device, m_pipeline_manager->avalanche_trajectories_bind_group_layout(), entries, "avalanche trajectories compute bind group");

    // bind GPU resources and run pipeline
    {
        WGPUCommandEncoderDescriptor descriptor {};
        descriptor.label = "avalanche trajectories compute command encoder";
        webgpu::raii::CommandEncoder encoder(m_device, descriptor);

        {
            WGPUComputePassDescriptor compute_pass_desc {};
            compute_pass_desc.label = "avalanche trajectories compute pass";
            webgpu::raii::ComputePassEncoder compute_pass(encoder.handle(), compute_pass_desc);

            glm::uvec3 workgroup_counts = glm::ceil(glm::vec3(m_output_dimensions.x, m_output_dimensions.y, 1) / glm::vec3(SHADER_WORKGROUP_SIZE));
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

std::unique_ptr<webgpu::raii::Sampler> ComputeAvalancheTrajectoriesNode::create_sampler(WGPUDevice device)
{
    WGPUSamplerDescriptor sampler_desc {};
    sampler_desc.label = "compute trajectories sampler";
    sampler_desc.addressModeU = WGPUAddressMode::WGPUAddressMode_ClampToEdge;
    sampler_desc.addressModeV = WGPUAddressMode::WGPUAddressMode_ClampToEdge;
    sampler_desc.addressModeW = WGPUAddressMode::WGPUAddressMode_ClampToEdge;
    sampler_desc.magFilter = WGPUFilterMode::WGPUFilterMode_Nearest;
    sampler_desc.minFilter = WGPUFilterMode::WGPUFilterMode_Nearest;
    sampler_desc.mipmapFilter = WGPUMipmapFilterMode::WGPUMipmapFilterMode_Nearest;
    sampler_desc.lodMinClamp = 0.0f;
    sampler_desc.lodMaxClamp = 1.0f;
    sampler_desc.compare = WGPUCompareFunction::WGPUCompareFunction_Undefined;
    sampler_desc.maxAnisotropy = 1;
    return std::make_unique<webgpu::raii::Sampler>(device, sampler_desc);
}

} // namespace webgpu_engine::compute::nodes
