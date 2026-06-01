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

#include "TextureOverlay.h"

#include "webgpu_engine/gpu_utils.h"
#include <nucleus/utils/image_loader.h>
#include <optional>
#include <webgpu/RenderResourceRegistry.h>
#include <webgpu/raii/BindGroup.h>
#include <webgpu/raii/BindGroupLayout.h>
#include <webgpu/raii/RenderPassEncoder.h>
#include <webgpu/raii/Texture.h>

namespace webgpu_engine {

TextureOverlay::TextureOverlay()
    : Overlay()
{
}

void TextureOverlay::load_image(const QString& path)
{
    m_image_path = path;
    if (m_post_recreate_called)
        upload_texture(*m_ctx);
}

void TextureOverlay::set_aabb(glm::dvec2 min, glm::dvec2 max)
{
    m_aabb_min_d = min;
    m_aabb_max_d = max;
    if (m_settings_uniform)
        update_gpu_settings();
}

void TextureOverlay::init(webgpu::Context& ctx)
{
    m_ctx = &ctx;

    auto& reg = ctx.resource_registry();
    // Shader and bind group layout are shared across all instances of this overlay type;
    // only register them once (multiple instances would otherwise re-register the same name).
    if (!reg.has_shader("texture_overlay_render"))
        reg.register_shader("texture_overlay_render", "overlays/texture_overlay.wgsl");
    if (!reg.has_bind_group_layout("texture_overlay"))
        reg.register_bind_group_layout("texture_overlay", [](WGPUDevice device) {
            WGPUBindGroupLayoutEntry position_entry {};
            position_entry.binding = 0;
            position_entry.visibility = WGPUShaderStage_Fragment;
            position_entry.texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;
            position_entry.texture.viewDimension = WGPUTextureViewDimension_2D;

            WGPUBindGroupLayoutEntry settings_entry {};
            settings_entry.binding = 1;
            settings_entry.visibility = WGPUShaderStage_Fragment;
            settings_entry.buffer.type = WGPUBufferBindingType_Uniform;

            WGPUBindGroupLayoutEntry overlay_texture_entry {};
            overlay_texture_entry.binding = 2;
            overlay_texture_entry.visibility = WGPUShaderStage_Fragment;
            overlay_texture_entry.texture.sampleType = WGPUTextureSampleType_Float;
            overlay_texture_entry.texture.viewDimension = WGPUTextureViewDimension_2D;

            WGPUBindGroupLayoutEntry overlay_sampler_entry {};
            overlay_sampler_entry.binding = 3;
            overlay_sampler_entry.visibility = WGPUShaderStage_Fragment;
            overlay_sampler_entry.sampler.type = WGPUSamplerBindingType_Filtering;

            return std::make_unique<webgpu::raii::BindGroupLayout>(device,
                std::vector<WGPUBindGroupLayoutEntry> { position_entry, settings_entry, overlay_texture_entry, overlay_sampler_entry },
                "texture overlay bind group layout");
        });
    reg.register_pipeline([this](WGPUDevice device, const webgpu::RenderResourceRegistry& reg) {
        webgpu::FramebufferFormat format {};
        format.depth_format = WGPUTextureFormat_Undefined;
        format.color_formats = { WGPUTextureFormat_RGBA8Unorm };

        WGPUBlendState blend {};
        blend.color.operation = WGPUBlendOperation_Add;
        blend.color.srcFactor = WGPUBlendFactor_One; // source is premultiplied
        blend.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
        blend.alpha.operation = WGPUBlendOperation_Add;
        blend.alpha.srcFactor = WGPUBlendFactor_One;
        blend.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;

        m_pipeline = std::make_unique<webgpu::raii::GenericRenderPipeline>(device,
            reg.shader("texture_overlay_render"),
            reg.shader("texture_overlay_render"),
            std::vector<webgpu::util::SingleVertexBufferInfo> {},
            format,
            std::vector<const webgpu::raii::BindGroupLayout*> {
                &reg.bind_group_layout("shared_config"),
                &reg.bind_group_layout("camera"),
                &reg.bind_group_layout("texture_overlay"),
            },
            std::vector<std::optional<WGPUBlendState>> { blend });
    });

    m_settings_uniform = std::make_unique<webgpu_engine::Buffer<GpuSettings>>(ctx.device(), WGPUBufferUsage_CopyDst | WGPUBufferUsage_Uniform);
    update_gpu_settings();
}

void TextureOverlay::post_recreate_all(webgpu::Context& ctx)
{
    m_post_recreate_called = true;
    if (!m_image_path.isEmpty())
        upload_texture(ctx);
}

void TextureOverlay::upload_texture(webgpu::Context& ctx)
{
    const auto image = nucleus::utils::image_loader::rgba8(m_image_path).value();
    const bool mipmaps = settings.use_mipmaps;
    const auto mip_levels = mipmaps ? webgpu::raii::Texture::max_mip_level_count(glm::uvec2(image.width(), image.height())) : 1u;

    const auto filter = settings.filter_mode == FilterMode::Linear ? WGPUFilterMode_Linear : WGPUFilterMode_Nearest;
    const auto mip_filter = (mipmaps && settings.filter_mode == FilterMode::Linear) ? WGPUMipmapFilterMode_Linear : WGPUMipmapFilterMode_Nearest;

    WGPUTextureDescriptor texture_desc {};
    texture_desc.label = WGPUStringView { .data = "texture overlay input texture", .length = WGPU_STRLEN };
    texture_desc.dimension = WGPUTextureDimension_2D;
    texture_desc.size = { uint32_t(image.width()), uint32_t(image.height()), 1 };
    texture_desc.mipLevelCount = mip_levels;
    texture_desc.sampleCount = 1;
    texture_desc.format = WGPUTextureFormat_RGBA8Unorm;
    texture_desc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst | WGPUTextureUsage_StorageBinding;

    WGPUSamplerDescriptor sampler_desc {};
    sampler_desc.label = WGPUStringView { .data = "texture overlay sampler", .length = WGPU_STRLEN };
    sampler_desc.addressModeU = WGPUAddressMode_ClampToEdge;
    sampler_desc.addressModeV = WGPUAddressMode_ClampToEdge;
    sampler_desc.addressModeW = WGPUAddressMode_ClampToEdge;
    sampler_desc.magFilter = filter;
    sampler_desc.minFilter = filter;
    sampler_desc.mipmapFilter = mip_filter;
    sampler_desc.lodMinClamp = 0.0f;
    sampler_desc.lodMaxClamp = float(mip_levels);
    sampler_desc.compare = WGPUCompareFunction_Undefined;
    sampler_desc.maxAnisotropy = 1;

    m_overlay_texture = std::make_unique<webgpu::raii::TextureWithSampler>(ctx.device(), texture_desc, sampler_desc);
    m_overlay_texture->texture().write(ctx.queue(), image);
    if (mipmaps)
        compute_mipmaps_for_texture(ctx, &m_overlay_texture->texture());
}

void TextureOverlay::update_gpu_settings()
{
    m_settings_uniform->data.aabb_min = glm::vec2(m_aabb_min_d);
    m_settings_uniform->data.aabb_size = glm::vec2(m_aabb_max_d - m_aabb_min_d);
    m_settings_uniform->data.opacity = settings.opacity;
    m_settings_uniform->update_gpu_data(m_ctx->queue());
}

void TextureOverlay::draw(const WGPUCommandEncoder& command_encoder,
    const webgpu::raii::TextureView& position_view,
    const webgpu::raii::TextureView& /*normal_view*/,
    const WGPUBindGroup& shared_config_bg,
    const WGPUBindGroup& camera_bg,
    webgpu::raii::TextureWithSampler& output,
    glm::uvec2 /*output_size*/)
{
    if (!m_overlay_texture || !m_pipeline)
        return;

    webgpu::raii::BindGroup bind_group(m_ctx->device(),
        m_ctx->resource_registry().bind_group_layout("texture_overlay"),
        std::vector<WGPUBindGroupEntry> {
            position_view.create_bind_group_entry(0),
            m_settings_uniform->raw_buffer().create_bind_group_entry(1),
            m_overlay_texture->texture_view().create_bind_group_entry(2),
            m_overlay_texture->sampler().create_bind_group_entry(3),
        },
        "texture overlay bind group");

    WGPURenderPassColorAttachment color_attachment {};
    color_attachment.view = output.texture_view().handle();
    color_attachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    color_attachment.loadOp = WGPULoadOp_Load;
    color_attachment.storeOp = WGPUStoreOp_Store;

    WGPURenderPassDescriptor render_pass_desc {};
    render_pass_desc.label = WGPUStringView { .data = "texture overlay render pass", .length = WGPU_STRLEN };
    render_pass_desc.colorAttachmentCount = 1;
    render_pass_desc.colorAttachments = &color_attachment;

    webgpu::raii::RenderPassEncoder render_pass(command_encoder, render_pass_desc);

    wgpuRenderPassEncoderSetPipeline(render_pass.handle(), m_pipeline->pipeline().handle());
    wgpuRenderPassEncoderSetBindGroup(render_pass.handle(), 0, shared_config_bg, 0, nullptr);
    wgpuRenderPassEncoderSetBindGroup(render_pass.handle(), 1, camera_bg, 0, nullptr);
    wgpuRenderPassEncoderSetBindGroup(render_pass.handle(), 2, bind_group.handle(), 0, nullptr);
    wgpuRenderPassEncoderDraw(render_pass.handle(), 3, 1, 0, 0);
}

} // namespace webgpu_engine
