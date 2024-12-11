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

glm::uvec3 BufferToTextureNode::SHADER_WORKGROUP_SIZE = { 16, 16, 1 };

const uint32_t BufferToTextureNode::MAX_TEXTURE_RESOLUTION = 8192;

BufferToTextureNode::BufferToTextureNode(const PipelineManager& pipeline_manager, WGPUDevice device)
    : webgpu_engine::compute::nodes::BufferToTextureNode(pipeline_manager, device, BufferToTextureSettings())
{
}

BufferToTextureNode::BufferToTextureNode(const PipelineManager& pipeline_manager, WGPUDevice device, const BufferToTextureSettings& settings)
    : Node(
          {
              InputSocket(*this, "raster dimensions", data_type<glm::uvec2>()),
              InputSocket(*this, "storage buffer", data_type<webgpu::raii::RawBuffer<uint32_t>*>()),
          },
          {
              OutputSocket(*this, "texture", data_type<const webgpu::raii::TextureWithSampler*>(), [this]() { return m_output_texture.get(); }),
          })
    , m_pipeline_manager { &pipeline_manager }
    , m_device { device }
    , m_queue { wgpuDeviceGetQueue(device) }
    , m_settings { settings }
    , m_settings_uniform(device, WGPUBufferUsage_CopyDst | WGPUBufferUsage_Uniform)
{
}

void BufferToTextureNode::run_impl()
{
    qDebug() << "running BufferToTextureNode ...";
    const auto input_raster_dimensions = std::get<data_type<glm::uvec2>()>(input_socket("raster dimensions").get_connected_data());
    const auto& input_storage_buffer = *std::get<data_type<webgpu::raii::RawBuffer<uint32_t>*>()>(input_socket("storage buffer").get_connected_data());

    m_settings_uniform.data.input_resolution = input_raster_dimensions;
    m_settings_uniform.update_gpu_data(m_queue);
    qDebug() << "input resolution: " << input_raster_dimensions.x << "x" << input_raster_dimensions.y;

    // assert input textures have same size, otherwise fail run
    if (input_raster_dimensions.x > MAX_TEXTURE_RESOLUTION || input_raster_dimensions.y > MAX_TEXTURE_RESOLUTION) {
        emit run_failed(NodeRunFailureInfo(*this,
            std::format(
                "cannot create texture: texture dimensions ({}x{}) exceed {}", input_raster_dimensions.x, input_raster_dimensions.y, MAX_TEXTURE_RESOLUTION)));
        return;
    }

    m_output_texture = create_texture(m_device, input_raster_dimensions.x, input_raster_dimensions.y, m_settings.format, m_settings.usage);

    // create bind group
    std::vector<WGPUBindGroupEntry> entries {
        m_settings_uniform.raw_buffer().create_bind_group_entry(0),
        input_storage_buffer.create_bind_group_entry(1),
        m_output_texture->texture_view().create_bind_group_entry(2),
    };
    webgpu::raii::BindGroup compute_bind_group(
        m_device, m_pipeline_manager->buffer_to_texture_bind_group_layout(), entries, "buffer to texture compute bind group");

    // bind GPU resources and run pipeline
    {
        WGPUCommandEncoderDescriptor descriptor {};
        descriptor.label = "buffer to texture compute command encoder";
        webgpu::raii::CommandEncoder encoder(m_device, descriptor);

        {
            WGPUComputePassDescriptor compute_pass_desc {};
            compute_pass_desc.label = "buffer to texture compute pass";
            webgpu::raii::ComputePassEncoder compute_pass(encoder.handle(), compute_pass_desc);

            glm::uvec3 workgroup_counts = glm::ceil(glm::vec3(input_raster_dimensions.x, input_raster_dimensions.y, 1) / glm::vec3(SHADER_WORKGROUP_SIZE));
            wgpuComputePassEncoderSetBindGroup(compute_pass.handle(), 0, compute_bind_group.handle(), 0, nullptr);
            m_pipeline_manager->buffer_to_texture_compute_pipeline().run(compute_pass, workgroup_counts);
        }

        WGPUCommandBufferDescriptor cmd_buffer_descriptor {};
        cmd_buffer_descriptor.label = "buffer to texture compute command buffer";
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

std::unique_ptr<webgpu::raii::TextureWithSampler> BufferToTextureNode::create_texture(
    WGPUDevice device, uint32_t width, uint32_t height, WGPUTextureFormat format, WGPUTextureUsage usage)
{
    // create output texture
    WGPUTextureDescriptor texture_desc {};
    texture_desc.label = "buffer to texture output texture";
    texture_desc.dimension = WGPUTextureDimension::WGPUTextureDimension_2D;
    texture_desc.size = { width, height, 1 };
    texture_desc.mipLevelCount = 1;
    texture_desc.sampleCount = 1;
    texture_desc.format = format;
    texture_desc.usage = usage;

    WGPUSamplerDescriptor sampler_desc {};
    sampler_desc.label = "buffer to texture sampler";
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

    return std::make_unique<webgpu::raii::TextureWithSampler>(device, texture_desc, sampler_desc);
}

} // namespace webgpu_engine::compute::nodes
