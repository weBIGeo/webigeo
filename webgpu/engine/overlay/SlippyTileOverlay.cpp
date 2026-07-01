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
#include <webgpu/base/RenderResourceRegistry.h>
#include <webgpu/base/raii/BindGroup.h>
#include <webgpu/base/raii/BindGroupLayout.h>

namespace webgpu_engine {

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
            WGPUBindGroupLayoutEntry position_entry {};
            position_entry.binding = 0;
            position_entry.visibility = WGPUShaderStage_Compute;
            position_entry.texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;
            position_entry.texture.viewDimension = WGPUTextureViewDimension_2D;

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

            return std::make_unique<webgpu::raii::BindGroupLayout>(device,
                std::vector<WGPUBindGroupLayoutEntry> { position_entry, settings_entry, tile_texture_entry, tile_sampler_entry, output_entry,
                    background_entry, dict_ids_entry, dict_layers_entry },
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

    m_settings_uniform = std::make_unique<webgpu::Buffer<GpuSettings>>(ctx.device(), WGPUBufferUsage_CopyDst | WGPUBufferUsage_Uniform);
    update_settings();
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
    m_settings_uniform->data.opacity = settings.opacity;
    m_settings_uniform->data.max_zoom = settings.max_zoom;
    m_settings_uniform->update_gpu_data(m_ctx->queue());
}

void SlippyTileOverlay::draw(const WGPUCommandEncoder& command_encoder,
    const webgpu::raii::TextureView& position_view,
    const webgpu::raii::TextureView& /*normal_view*/,
    const webgpu::raii::TextureView& /*overlay_view*/,
    const webgpu::raii::TextureView& /*depth_view*/,
    const WGPUBindGroup& shared_config_bg,
    const WGPUBindGroup& camera_bg,
    const webgpu::raii::TextureWithSampler& current_input,
    webgpu::raii::TextureWithSampler& target_output,
    glm::uvec2 output_size)
{
    if (!m_pipeline || !m_source)
        return;

    webgpu::raii::BindGroup bind_group(m_ctx->device(),
        m_ctx->resource_registry().bind_group_layout("slippy_tile_overlay"),
        std::vector<WGPUBindGroupEntry> {
            position_view.create_bind_group_entry(0),
            m_settings_uniform->raw_buffer().create_bind_group_entry(1),
            m_source->array().texture_view().create_bind_group_entry(2),
            m_source->array().sampler().create_bind_group_entry(3),
            target_output.texture_view().create_bind_group_entry(4),
            current_input.texture_view().create_bind_group_entry(5),
            m_source->dictionary_ids_view().create_bind_group_entry(6),
            m_source->dictionary_layers_view().create_bind_group_entry(7),
        },
        "slippy tile overlay bind group");

    WGPUComputePassDescriptor compute_pass_desc {};
    compute_pass_desc.label = WGPUStringView { .data = "slippy tile overlay compute pass", .length = WGPU_STRLEN };
    webgpu::raii::ComputePassEncoder compute_pass(command_encoder, compute_pass_desc);

    wgpuComputePassEncoderSetBindGroup(compute_pass.handle(), 0, shared_config_bg, 0, nullptr);
    wgpuComputePassEncoderSetBindGroup(compute_pass.handle(), 1, camera_bg, 0, nullptr);
    wgpuComputePassEncoderSetBindGroup(compute_pass.handle(), 2, bind_group.handle(), 0, nullptr);

    const glm::uvec3 workgroup_counts = glm::ceil(glm::vec3(float(output_size.x), float(output_size.y), 1.0f) / glm::vec3(16.0f, 16.0f, 1.0f));
    m_pipeline->run(compute_pass, workgroup_counts);
}

} // namespace webgpu_engine
