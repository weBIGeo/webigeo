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

#include "UpsampleTexturesNode.h"

namespace webgpu_engine::compute::nodes {

glm::uvec3 UpsampleTexturesNode::SHADER_WORKGROUP_SIZE = { 1, 16, 16 };

UpsampleTexturesNode::UpsampleTexturesNode(const PipelineManager& pipeline_manager, WGPUDevice device, glm::uvec2 target_resolution, size_t capacity)
    : Node({ data_type<TileStorageTexture*>() }, { data_type<TileStorageTexture*>() })
    , m_pipeline_manager { &pipeline_manager }
    , m_device { device }
    , m_queue { wgpuDeviceGetQueue(m_device) }
    , m_target_resolution { target_resolution }
    , m_input_indices(std::make_unique<webgpu::raii::RawBuffer<uint32_t>>(
          m_device, WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc, capacity, "compute: upsampling texture, index buffer"))
    , m_output_storage_texture(std::make_unique<TileStorageTexture>(m_device, m_target_resolution, capacity, WGPUTextureFormat_RGBA8Unorm))
{
}

void UpsampleTexturesNode::run_impl()
{
    qDebug() << "running UpsampleTexturesNode ...";

    const auto& input_textures = *std::get<data_type<TileStorageTexture*>()>(get_input_data(Input::TEXTURE_ARRAY));
    const std::vector<uint32_t> input_used_indices = input_textures.used_layer_indices();

    qDebug() << "upsampling " << input_used_indices.size() << " textures from (" << input_textures.width() << "," << input_textures.height() << ") to ("
             << m_output_storage_texture->width() << "," << m_output_storage_texture->height() << ")";

    m_input_indices->write(m_queue, input_used_indices.data(), input_used_indices.size());

    m_output_storage_texture->clear();

    // (re)create bind group
    // TODO re-create bind groups only when input handles change
    WGPUBindGroupEntry input_indices_entry = m_input_indices->create_bind_group_entry(0);
    WGPUBindGroupEntry input_textures_entry = input_textures.texture().texture_view().create_bind_group_entry(1);
    WGPUBindGroupEntry input_sampler = input_textures.texture().sampler().create_bind_group_entry(2);
    WGPUBindGroupEntry output_textures_entry = m_output_storage_texture->texture().texture_view().create_bind_group_entry(3);
    std::vector<WGPUBindGroupEntry> entries { input_indices_entry, input_textures_entry, input_sampler, output_textures_entry };

    auto compute_bind_group = std::make_unique<webgpu::raii::BindGroup>(
        m_device, m_pipeline_manager->upsample_textures_compute_bind_group_layout(), entries, "compute: upsample textures bind group");

    // bind GPU resources and run pipeline
    {
        WGPUCommandEncoderDescriptor descriptor {};
        descriptor.label = "compute: upsample texture command encoder";
        webgpu::raii::CommandEncoder encoder(m_device, descriptor);

        {
            WGPUComputePassDescriptor compute_pass_desc {};
            compute_pass_desc.label = "compute: upsample texture compute pass";
            webgpu::raii::ComputePassEncoder compute_pass(encoder.handle(), compute_pass_desc);

            glm::uvec3 workgroup_counts = glm::ceil(
                glm::vec3(input_used_indices.size(), m_output_storage_texture->width(), m_output_storage_texture->height()) / glm::vec3(SHADER_WORKGROUP_SIZE));
            wgpuComputePassEncoderSetBindGroup(compute_pass.handle(), 0, compute_bind_group->handle(), 0, nullptr);
            m_pipeline_manager->upsample_textures_compute_pipeline().run(compute_pass, workgroup_counts);
        }

        WGPUCommandBufferDescriptor cmd_buffer_descriptor {};
        cmd_buffer_descriptor.label = "compute: upsampling texture command buffer";
        WGPUCommandBuffer command = wgpuCommandEncoderFinish(encoder.handle(), &cmd_buffer_descriptor);
        wgpuQueueSubmit(m_queue, 1, &command);
        wgpuCommandBufferRelease(command);
    }

    // mark output texture array slots as used
    for (const auto& index : input_used_indices) {
        m_output_storage_texture->reserve(index);
    }

    wgpuQueueOnSubmittedWorkDone(
        m_queue,
        []([[maybe_unused]] WGPUQueueWorkDoneStatus status, void* user_data) {
            UpsampleTexturesNode* _this = reinterpret_cast<UpsampleTexturesNode*>(user_data);
            emit _this->run_finished();
        },
        this);
}

webgpu_engine::compute::nodes::Data UpsampleTexturesNode::get_output_data_impl(SocketIndex output_index)
{
    if (output_index == Output::OUTPUT_TEXTURE_ARRAY) {
        return { m_output_storage_texture.get() };
    }
    exit(-1); // TODO log
}

} // namespace webgpu_engine::compute::nodes
