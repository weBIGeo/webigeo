/*****************************************************************************
 * weBIGeo
 * Copyright (C) 2026 Gerald Kimmersdorfer
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

#include "HeightLinesOverlay.h"

#include <webgpu/RenderResourceRegistry.h>
#include <webgpu/raii/BindGroup.h>
#include <webgpu/raii/BindGroupLayout.h>

namespace webgpu_engine {

HeightLinesOverlay::HeightLinesOverlay()
    : Overlay()
{
}

void HeightLinesOverlay::init(webgpu::Context& ctx)
{
    m_ctx = &ctx;

    auto& reg = ctx.resource_registry();
    reg.register_shader("height_lines_compute", "overlays/height_lines.wgsl");
    reg.register_bind_group_layout("height_lines_overlay", [](WGPUDevice device) {
        WGPUBindGroupLayoutEntry position_entry {};
        position_entry.binding = 0;
        position_entry.visibility = WGPUShaderStage_Compute;
        position_entry.texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;
        position_entry.texture.viewDimension = WGPUTextureViewDimension_2D;

        WGPUBindGroupLayoutEntry normal_entry {};
        normal_entry.binding = 1;
        normal_entry.visibility = WGPUShaderStage_Compute;
        normal_entry.texture.sampleType = WGPUTextureSampleType_Uint;
        normal_entry.texture.viewDimension = WGPUTextureViewDimension_2D;

        WGPUBindGroupLayoutEntry settings_entry {};
        settings_entry.binding = 2;
        settings_entry.visibility = WGPUShaderStage_Compute;
        settings_entry.buffer.type = WGPUBufferBindingType_Uniform;

        WGPUBindGroupLayoutEntry output_entry {};
        output_entry.binding = 3;
        output_entry.visibility = WGPUShaderStage_Compute;
        output_entry.storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
        output_entry.storageTexture.format = WGPUTextureFormat_RGBA8Unorm;
        output_entry.storageTexture.viewDimension = WGPUTextureViewDimension_2D;

        WGPUBindGroupLayoutEntry prev_output_entry {};
        prev_output_entry.binding = 4;
        prev_output_entry.visibility = WGPUShaderStage_Compute;
        prev_output_entry.texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;
        prev_output_entry.texture.viewDimension = WGPUTextureViewDimension_2D;

        return std::make_unique<webgpu::raii::BindGroupLayout>(device,
            std::vector<WGPUBindGroupLayoutEntry> { position_entry, normal_entry, settings_entry, output_entry, prev_output_entry },
            "height lines overlay bind group layout");
    });
    reg.register_pipeline([this](WGPUDevice device, const webgpu::RenderResourceRegistry& reg) {
        m_pipeline = std::make_unique<webgpu::raii::CombinedComputePipeline>(device,
            reg.shader("height_lines_compute"),
            std::vector<const webgpu::raii::BindGroupLayout*> {
                &reg.bind_group_layout("shared_config"),
                &reg.bind_group_layout("camera"),
                &reg.bind_group_layout("height_lines_overlay"),
            },
            "height lines compute pipeline");
    });

    m_settings_uniform = std::make_unique<webgpu_engine::Buffer<Settings>>(ctx.device(), WGPUBufferUsage_CopyDst | WGPUBufferUsage_Uniform);
    m_settings_uniform->data = settings;
    m_settings_uniform->update_gpu_data(ctx.queue());
}

void HeightLinesOverlay::resize(glm::uvec2 size)
{
    if (!m_ctx)
        return;

    WGPUTextureDescriptor texture_desc {};
    texture_desc.label = WGPUStringView { .data = "height lines copy texture", .length = WGPU_STRLEN };
    texture_desc.dimension = WGPUTextureDimension_2D;
    texture_desc.size = { size.x, size.y, 1 };
    texture_desc.mipLevelCount = 1;
    texture_desc.sampleCount = 1;
    texture_desc.format = WGPUTextureFormat_RGBA8Unorm;
    texture_desc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;

    WGPUSamplerDescriptor sampler_desc {};
    sampler_desc.label = WGPUStringView { .data = "height lines copy sampler", .length = WGPU_STRLEN };
    sampler_desc.addressModeU = WGPUAddressMode_ClampToEdge;
    sampler_desc.addressModeV = WGPUAddressMode_ClampToEdge;
    sampler_desc.addressModeW = WGPUAddressMode_ClampToEdge;
    sampler_desc.magFilter = WGPUFilterMode_Nearest;
    sampler_desc.minFilter = WGPUFilterMode_Nearest;
    sampler_desc.mipmapFilter = WGPUMipmapFilterMode_Nearest;
    sampler_desc.lodMinClamp = 0.0f;
    sampler_desc.lodMaxClamp = 1.0f;
    sampler_desc.compare = WGPUCompareFunction_Undefined;
    sampler_desc.maxAnisotropy = 1;

    m_copy_texture = std::make_unique<webgpu::raii::TextureWithSampler>(m_ctx->device(), texture_desc, sampler_desc);
}

void HeightLinesOverlay::draw(const WGPUCommandEncoder& command_encoder,
    const webgpu::raii::TextureView& position_view,
    const webgpu::raii::TextureView& normal_view,
    const WGPUBindGroup& shared_config_bg,
    const WGPUBindGroup& camera_bg,
    webgpu::raii::TextureWithSampler& output,
    glm::uvec2 output_size)
{
    if (!m_copy_texture)
        return;

    // Copy current output → copy texture so the shader can read the previous overlay state
    WGPUTexelCopyTextureInfo src {};
    src.texture = output.texture().handle();
    src.mipLevel = 0;
    src.origin = { 0, 0, 0 };
    src.aspect = WGPUTextureAspect_All;

    WGPUTexelCopyTextureInfo dst {};
    dst.texture = m_copy_texture->texture().handle();
    dst.mipLevel = 0;
    dst.origin = { 0, 0, 0 };
    dst.aspect = WGPUTextureAspect_All;

    WGPUExtent3D extent { output_size.x, output_size.y, 1 };
    wgpuCommandEncoderCopyTextureToTexture(command_encoder, &src, &dst, &extent);

    webgpu::raii::BindGroup bind_group(m_ctx->device(),
        m_ctx->resource_registry().bind_group_layout("height_lines_overlay"),
        std::vector<WGPUBindGroupEntry> {
            position_view.create_bind_group_entry(0),
            normal_view.create_bind_group_entry(1),
            m_settings_uniform->raw_buffer().create_bind_group_entry(2),
            output.texture_view().create_bind_group_entry(3),
            m_copy_texture->texture_view().create_bind_group_entry(4),
        },
        "height lines overlay bind group");

    WGPUComputePassDescriptor compute_pass_desc {};
    compute_pass_desc.label = WGPUStringView { .data = "height lines compute pass", .length = WGPU_STRLEN };
    webgpu::raii::ComputePassEncoder compute_pass(command_encoder, compute_pass_desc);

    wgpuComputePassEncoderSetBindGroup(compute_pass.handle(), 0, shared_config_bg, 0, nullptr);
    wgpuComputePassEncoderSetBindGroup(compute_pass.handle(), 1, camera_bg, 0, nullptr);
    wgpuComputePassEncoderSetBindGroup(compute_pass.handle(), 2, bind_group.handle(), 0, nullptr);

    const glm::uvec3 workgroup_counts = glm::ceil(glm::vec3(float(output_size.x), float(output_size.y), 1.0f) / glm::vec3(16.0f, 16.0f, 1.0f));
    m_pipeline->run(compute_pass, workgroup_counts);
}

} // namespace webgpu_engine
