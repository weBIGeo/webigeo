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

#include "SlippyTileOverlay.h"

#include "webgpu/engine/Context.h"
#include "webgpu/engine/tile/TileSource.h"
#include <algorithm>
#include <nucleus/srs.h>
#include <webgpu/base/RenderResourceRegistry.h>
#include <webgpu/base/raii/BindGroup.h>
#include <webgpu/base/raii/BindGroupLayout.h>

namespace webgpu_engine {

namespace {
    constexpr uint32_t k_max_frame_tiles = 1024;
} // namespace

SlippyTileOverlay::SlippyTileOverlay(TileSource* source)
    : Overlay()
    , m_source(source)
{
    z_index = -1; // pre-shading: compose folds this into albedo before lighting
    name = source ? source->name().toStdString() : std::string("Slippy Tiles");
}

void SlippyTileOverlay::init(Context& context)
{
    webgpu::Context& ctx = context.webgpu_ctx();
    m_ctx = &ctx;

    auto& reg = ctx.resource_registry();
    if (!reg.has_shader("slippy_tile_overlay_compute"))
        reg.register_shader("slippy_tile_overlay_compute", "webgpu_engine::overlays/slippy_tile_overlay");
    if (!reg.has_bind_group_layout("slippy_tile_overlay"))
        reg.register_bind_group_layout("slippy_tile_overlay", [](WGPUDevice device) {
            WGPUBindGroupLayoutEntry depth_entry {};
            depth_entry.binding = 0;
            depth_entry.visibility = WGPUShaderStage_Compute;
            depth_entry.texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;
            depth_entry.texture.viewDimension = WGPUTextureViewDimension_2D;

            WGPUBindGroupLayoutEntry settings_entry {};
            settings_entry.binding = 1;
            settings_entry.visibility = WGPUShaderStage_Compute;
            settings_entry.buffer.type = WGPUBufferBindingType_Uniform;

            WGPUBindGroupLayoutEntry tile_texture_entry {};
            tile_texture_entry.binding = 2;
            tile_texture_entry.visibility = WGPUShaderStage_Compute;
            tile_texture_entry.texture.sampleType = WGPUTextureSampleType_Float;
            tile_texture_entry.texture.viewDimension = WGPUTextureViewDimension_2DArray;

            WGPUBindGroupLayoutEntry tile_sampler_entry {};
            tile_sampler_entry.binding = 3;
            tile_sampler_entry.visibility = WGPUShaderStage_Compute;
            tile_sampler_entry.sampler.type = WGPUSamplerBindingType_Filtering;

            WGPUBindGroupLayoutEntry output_entry {};
            output_entry.binding = 4;
            output_entry.visibility = WGPUShaderStage_Compute;
            output_entry.storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
            output_entry.storageTexture.format = WGPUTextureFormat_RGBA8Unorm;
            output_entry.storageTexture.viewDimension = WGPUTextureViewDimension_2D;

            WGPUBindGroupLayoutEntry background_entry {};
            background_entry.binding = 5;
            background_entry.visibility = WGPUShaderStage_Compute;
            background_entry.texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;
            background_entry.texture.viewDimension = WGPUTextureViewDimension_2D;

            WGPUBindGroupLayoutEntry dict_ids_entry {};
            dict_ids_entry.binding = 6;
            dict_ids_entry.visibility = WGPUShaderStage_Compute;
            dict_ids_entry.texture.sampleType = WGPUTextureSampleType_Uint;
            dict_ids_entry.texture.viewDimension = WGPUTextureViewDimension_2D;

            WGPUBindGroupLayoutEntry dict_layers_entry {};
            dict_layers_entry.binding = 7;
            dict_layers_entry.visibility = WGPUShaderStage_Compute;
            dict_layers_entry.texture.sampleType = WGPUTextureSampleType_Uint;
            dict_layers_entry.texture.viewDimension = WGPUTextureViewDimension_2D;

            WGPUBindGroupLayoutEntry tile_ref_entry {};
            tile_ref_entry.binding = 8;
            tile_ref_entry.visibility = WGPUShaderStage_Compute;
            tile_ref_entry.texture.sampleType = WGPUTextureSampleType_Uint;
            tile_ref_entry.texture.viewDimension = WGPUTextureViewDimension_2D;

            WGPUBindGroupLayoutEntry frame_tile_ids_entry {};
            frame_tile_ids_entry.binding = 9;
            frame_tile_ids_entry.visibility = WGPUShaderStage_Compute;
            frame_tile_ids_entry.buffer.type = WGPUBufferBindingType_ReadOnlyStorage;

            // Temporary: gbuffer normals, used by DataMode::SnowAvgNormals's slope-based masking.
            WGPUBindGroupLayoutEntry normal_entry {};
            normal_entry.binding = 11;
            normal_entry.visibility = WGPUShaderStage_Compute;
            normal_entry.texture.sampleType = WGPUTextureSampleType_Uint;
            normal_entry.texture.viewDimension = WGPUTextureViewDimension_2D;

            return std::make_unique<webgpu::raii::BindGroupLayout>(device,
                std::vector<WGPUBindGroupLayoutEntry> { depth_entry, settings_entry, tile_texture_entry, tile_sampler_entry, output_entry,
                    background_entry, dict_ids_entry, dict_layers_entry, tile_ref_entry, frame_tile_ids_entry,
                    normal_entry },
                "slippy tile overlay bind group layout");
        });

    reg.register_pipeline([this](WGPUDevice device, const webgpu::RenderResourceRegistry& reg) {
        m_pipeline = std::make_unique<webgpu::raii::CombinedComputePipeline>(device,
            reg.shader("slippy_tile_overlay_compute"),
            std::vector<const webgpu::raii::BindGroupLayout*> {
                &reg.bind_group_layout("shared_config"),
                &reg.bind_group_layout("camera"),
                &reg.bind_group_layout("slippy_tile_overlay"),
            },
            "slippy tile overlay compute pipeline");
    });

    // DataMode::NormalsOverwrite: a dedicated render pass overwrites the gbuffer normal (RG16Uint) directly --
    // WebGPU can't storage-write that format from a compute shader, so this reuses the exact same
    // tile-resolution WGSL (resolve_tile_sample, spliced in via ///use) but through a fragment
    // entry point instead, with its own smaller bind-group layout (fragment-visible subset of the
    // compute layout's bindings; no ping-pong/output/normal-mask bindings needed here).
    if (!reg.has_shader("slippy_tile_overlay_gbuffer_write"))
        reg.register_shader("slippy_tile_overlay_gbuffer_write", "webgpu_engine::overlays/slippy_tile_gbuffer_normals_pass");
    if (!reg.has_bind_group_layout("slippy_tile_overlay_gbuffer_write"))
        reg.register_bind_group_layout("slippy_tile_overlay_gbuffer_write", [](WGPUDevice device) {
            WGPUBindGroupLayoutEntry depth_entry {};
            depth_entry.binding = 0;
            depth_entry.visibility = WGPUShaderStage_Fragment;
            depth_entry.texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;
            depth_entry.texture.viewDimension = WGPUTextureViewDimension_2D;

            WGPUBindGroupLayoutEntry settings_entry {};
            settings_entry.binding = 1;
            settings_entry.visibility = WGPUShaderStage_Fragment;
            settings_entry.buffer.type = WGPUBufferBindingType_Uniform;

            WGPUBindGroupLayoutEntry tile_texture_entry {};
            tile_texture_entry.binding = 2;
            tile_texture_entry.visibility = WGPUShaderStage_Fragment;
            tile_texture_entry.texture.sampleType = WGPUTextureSampleType_Float;
            tile_texture_entry.texture.viewDimension = WGPUTextureViewDimension_2DArray;

            WGPUBindGroupLayoutEntry tile_sampler_entry {};
            tile_sampler_entry.binding = 3;
            tile_sampler_entry.visibility = WGPUShaderStage_Fragment;
            tile_sampler_entry.sampler.type = WGPUSamplerBindingType_Filtering;

            WGPUBindGroupLayoutEntry dict_ids_entry {};
            dict_ids_entry.binding = 6;
            dict_ids_entry.visibility = WGPUShaderStage_Fragment;
            dict_ids_entry.texture.sampleType = WGPUTextureSampleType_Uint;
            dict_ids_entry.texture.viewDimension = WGPUTextureViewDimension_2D;

            WGPUBindGroupLayoutEntry dict_layers_entry {};
            dict_layers_entry.binding = 7;
            dict_layers_entry.visibility = WGPUShaderStage_Fragment;
            dict_layers_entry.texture.sampleType = WGPUTextureSampleType_Uint;
            dict_layers_entry.texture.viewDimension = WGPUTextureViewDimension_2D;

            WGPUBindGroupLayoutEntry tile_ref_entry {};
            tile_ref_entry.binding = 8;
            tile_ref_entry.visibility = WGPUShaderStage_Fragment;
            tile_ref_entry.texture.sampleType = WGPUTextureSampleType_Uint;
            tile_ref_entry.texture.viewDimension = WGPUTextureViewDimension_2D;

            WGPUBindGroupLayoutEntry frame_tile_ids_entry {};
            frame_tile_ids_entry.binding = 9;
            frame_tile_ids_entry.visibility = WGPUShaderStage_Fragment;
            frame_tile_ids_entry.buffer.type = WGPUBufferBindingType_ReadOnlyStorage;

            return std::make_unique<webgpu::raii::BindGroupLayout>(device,
                std::vector<WGPUBindGroupLayoutEntry> { depth_entry, settings_entry, tile_texture_entry, tile_sampler_entry, dict_ids_entry,
                    dict_layers_entry, tile_ref_entry, frame_tile_ids_entry },
                "slippy tile overlay gbuffer write bind group layout");
        });

    reg.register_pipeline([this](WGPUDevice device, const webgpu::RenderResourceRegistry& reg) {
        webgpu::FramebufferFormat format {};
        format.depth_format = WGPUTextureFormat_Undefined;
        format.color_formats.emplace_back(WGPUTextureFormat_RG16Uint); // matches the gbuffer normal attachment
        m_gbuffer_write_pipeline = std::make_unique<webgpu::raii::GenericRenderPipeline>(device,
            reg.shader("slippy_tile_overlay_gbuffer_write"),
            reg.shader("slippy_tile_overlay_gbuffer_write"),
            std::vector<webgpu::util::SingleVertexBufferInfo> {},
            format,
            std::vector<const webgpu::raii::BindGroupLayout*> {
                &reg.bind_group_layout("shared_config"),
                &reg.bind_group_layout("camera"),
                &reg.bind_group_layout("slippy_tile_overlay_gbuffer_write"),
            });
    });

    m_settings_uniform = std::make_unique<webgpu::Buffer<GpuSettings>>(ctx.device(), WGPUBufferUsage_CopyDst | WGPUBufferUsage_Uniform);
    update_settings();

    m_frame_tile_ids_buffer = std::make_unique<webgpu::raii::RawBuffer<glm::u32vec2>>(
        ctx.device(), WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst, k_max_frame_tiles, "slippy tile overlay frame tile ids");
}

void SlippyTileOverlay::set_source(TileSource* source)
{
    m_source = source;
    name = source ? source->name().toStdString() : std::string("Slippy Tiles");
}

void SlippyTileOverlay::update_settings()
{
    if (!m_settings_uniform)
        return;
    // The LOD threshold is owned by the source
    const float pixel_error_threshold = m_source ? m_source->pixel_error_threshold() : settings.pixel_error_threshold;
    m_settings_uniform->data.opacity = settings.opacity;
    m_settings_uniform->data.max_zoom = settings.max_zoom;
    m_settings_uniform->data.tile_size = settings.tile_size;
    m_settings_uniform->data.pixel_error_threshold = pixel_error_threshold;
    m_settings_uniform->data.debug_view = static_cast<uint32_t>(settings.debug_view);
    m_settings_uniform->data.zoom_selection_mode = static_cast<uint32_t>(settings.zoom_selection_mode);
    m_settings_uniform->data.data_mode = static_cast<uint32_t>(settings.data_mode);
    m_settings_uniform->data.blend_zoom_transitions = settings.blend_zoom_transitions ? 1u : 0u;
    m_settings_uniform->data.zoom_blend_band = settings.zoom_blend_band;
    m_settings_uniform->update_gpu_data(m_ctx->queue());
}

void SlippyTileOverlay::draw(const WGPUCommandEncoder& command_encoder,
    const OverlayContext& octx,
    const webgpu::raii::TextureWithSampler& current_input,
    webgpu::raii::TextureWithSampler& target_output,
    glm::uvec2 output_size)
{
    if (!m_pipeline || !m_source)
        return;

    const size_t n_tiles = std::min(octx.frame_tile_ids.size(), size_t(k_max_frame_tiles));
    std::vector<glm::u32vec2> packed_ids(n_tiles);
    const float pixel_error_threshold = m_source->pixel_error_threshold(); // source is authoritative (see update_settings)
    // Keeps the sampling uniform in sync when another overlay sharing this source changed it
    if (m_settings_uniform->data.pixel_error_threshold != pixel_error_threshold) {
        m_settings_uniform->data.pixel_error_threshold = pixel_error_threshold;
        m_settings_uniform->update_gpu_data(m_ctx->queue());
    }
    for (size_t i = 0; i < n_tiles; ++i)
        packed_ids[i] = nucleus::srs::pack(octx.frame_tile_ids[i].id);
    if (!packed_ids.empty())
        m_frame_tile_ids_buffer->write(m_ctx->queue(), packed_ids.data(), packed_ids.size());

    webgpu::raii::BindGroup bind_group(m_ctx->device(),
        m_ctx->resource_registry().bind_group_layout("slippy_tile_overlay"),
        std::vector<WGPUBindGroupEntry> {
            octx.depth_view.create_bind_group_entry(0),
            m_settings_uniform->raw_buffer().create_bind_group_entry(1),
            m_source->array().texture_view().create_bind_group_entry(2),
            m_source->array().sampler().create_bind_group_entry(3),
            target_output.texture_view().create_bind_group_entry(4),
            current_input.texture_view().create_bind_group_entry(5),
            m_source->dictionary_ids_view().create_bind_group_entry(6),
            m_source->dictionary_layers_view().create_bind_group_entry(7),
            octx.tile_ref_view.create_bind_group_entry(8),
            m_frame_tile_ids_buffer->create_bind_group_entry(9),
            octx.normal_view.create_bind_group_entry(11),
        },
        "slippy tile overlay bind group");

    WGPUComputePassDescriptor compute_pass_desc {};
    compute_pass_desc.label = WGPUStringView { .data = "slippy tile overlay compute pass", .length = WGPU_STRLEN };
    webgpu::raii::ComputePassEncoder compute_pass(command_encoder, compute_pass_desc);

    wgpuComputePassEncoderSetBindGroup(compute_pass.handle(), 0, octx.shared_config_bg, 0, nullptr);
    wgpuComputePassEncoderSetBindGroup(compute_pass.handle(), 1, octx.camera_bg, 0, nullptr);
    wgpuComputePassEncoderSetBindGroup(compute_pass.handle(), 2, bind_group.handle(), 0, nullptr);

    const glm::uvec3 workgroup_counts = glm::ceil(glm::vec3(float(output_size.x), float(output_size.y), 1.0f) / glm::vec3(16.0f, 16.0f, 1.0f));
    m_pipeline->run(compute_pass, workgroup_counts);
}

void SlippyTileOverlay::write_normals_to_gbuffer(const WGPUCommandEncoder& command_encoder,
    const webgpu::raii::TextureView& normal_view,
    const webgpu::raii::TextureView& depth_view,
    const webgpu::raii::TextureView& tile_ref_view,
    const WGPUBindGroup& shared_config_bg,
    const WGPUBindGroup& camera_bg)
{
    if (!m_gbuffer_write_pipeline || !m_source || settings.data_mode != DataMode::NormalsOverwrite)
        return;

    // Reuses this frame's already-uploaded m_frame_tile_ids_buffer -- this is called right after
    // draw() in the same frame (see OverlayRenderer::write_gbuffer_normals).
    webgpu::raii::BindGroup bind_group(m_ctx->device(),
        m_ctx->resource_registry().bind_group_layout("slippy_tile_overlay_gbuffer_write"),
        std::vector<WGPUBindGroupEntry> {
            depth_view.create_bind_group_entry(0),
            m_settings_uniform->raw_buffer().create_bind_group_entry(1),
            m_source->array().texture_view().create_bind_group_entry(2),
            m_source->array().sampler().create_bind_group_entry(3),
            m_source->dictionary_ids_view().create_bind_group_entry(6),
            m_source->dictionary_layers_view().create_bind_group_entry(7),
            tile_ref_view.create_bind_group_entry(8),
            m_frame_tile_ids_buffer->create_bind_group_entry(9),
        },
        "slippy tile overlay gbuffer write bind group");

    WGPURenderPassColorAttachment color_attachment {};
    color_attachment.view = normal_view.handle();
    color_attachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    color_attachment.loadOp = WGPULoadOp_Load; // only overwrite pixels this pass's fragment shader actually writes
    color_attachment.storeOp = WGPUStoreOp_Store;

    WGPURenderPassDescriptor pass_desc {};
    pass_desc.label = WGPUStringView { .data = "slippy tile overlay gbuffer write pass", .length = WGPU_STRLEN };
    pass_desc.colorAttachmentCount = 1;
    pass_desc.colorAttachments = &color_attachment;

    webgpu::raii::RenderPassEncoder render_pass(command_encoder, pass_desc);
    wgpuRenderPassEncoderSetPipeline(render_pass.handle(), m_gbuffer_write_pipeline->pipeline().handle());
    wgpuRenderPassEncoderSetBindGroup(render_pass.handle(), 0, shared_config_bg, 0, nullptr);
    wgpuRenderPassEncoderSetBindGroup(render_pass.handle(), 1, camera_bg, 0, nullptr);
    wgpuRenderPassEncoderSetBindGroup(render_pass.handle(), 2, bind_group.handle(), 0, nullptr);
    wgpuRenderPassEncoderDraw(render_pass.handle(), 3, 1, 0, 0);
}

} // namespace webgpu_engine
