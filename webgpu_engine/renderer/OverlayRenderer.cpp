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

#include "OverlayRenderer.h"

#include <QFile>
#include <QString>
#include <QTextStream>
#include <webgpu/RenderResourceRegistry.h>
#include <webgpu/raii/BindGroup.h>
#include <webgpu/raii/BindGroupLayout.h>

namespace webgpu_engine {

OverlayRenderer::OverlayRenderer()
    : QObject { nullptr }
{
}

void OverlayRenderer::init(webgpu::Context& ctx)
{
    m_ctx = &ctx;

    auto& reg = ctx.resource_registry();
    reg.register_shader("height_lines_compute", "overlays/height_lines.wgsl");
    reg.register_bind_group_layout("overlay_renderer", [](WGPUDevice device) {
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

        WGPUBindGroupLayoutEntry output_entry {};
        output_entry.binding = 2;
        output_entry.visibility = WGPUShaderStage_Compute;
        output_entry.storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
        output_entry.storageTexture.format = WGPUTextureFormat_R32Uint;
        output_entry.storageTexture.viewDimension = WGPUTextureViewDimension_2D;

        return std::make_unique<webgpu::raii::BindGroupLayout>(device,
            std::vector<WGPUBindGroupLayoutEntry> { position_entry, normal_entry, output_entry },
            "overlay renderer bind group layout");
    });
    reg.register_pipeline([this](WGPUDevice device, const webgpu::RenderResourceRegistry& reg) {
        m_pipeline = std::make_unique<webgpu::raii::CombinedComputePipeline>(device,
            reg.shader("height_lines_compute"),
            std::vector<const webgpu::raii::BindGroupLayout*> {
                &reg.bind_group_layout("shared_config"),
                &reg.bind_group_layout("camera"),
                &reg.bind_group_layout("overlay_renderer"),
            },
            "height lines compute pipeline");
    });
}

void OverlayRenderer::resize(int w, int h)
{
    if (!m_ctx)
        return;

    WGPUTextureDescriptor texture_desc {};
    texture_desc.label = WGPUStringView { .data = "overlay renderer output texture", .length = WGPU_STRLEN };
    texture_desc.dimension = WGPUTextureDimension_2D;
    texture_desc.size = { uint32_t(w), uint32_t(h), 1 };
    texture_desc.mipLevelCount = 1;
    texture_desc.sampleCount = 1;
    texture_desc.format = WGPUTextureFormat_R32Uint;
    texture_desc.usage = WGPUTextureUsage_StorageBinding | WGPUTextureUsage_TextureBinding;

    WGPUSamplerDescriptor sampler_desc {};
    sampler_desc.label = WGPUStringView { .data = "overlay renderer sampler", .length = WGPU_STRLEN };
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

    m_output_texture = std::make_unique<webgpu::raii::TextureWithSampler>(m_ctx->device(), texture_desc, sampler_desc);
}

void OverlayRenderer::draw(const WGPUCommandEncoder& command_encoder,
    const webgpu::raii::TextureView& position_view,
    const webgpu::raii::TextureView& normal_view,
    const WGPUBindGroup& shared_config_bg,
    const WGPUBindGroup& camera_bg)
{
    webgpu::raii::BindGroup overlay_bind_group(m_ctx->device(),
        m_ctx->resource_registry().bind_group_layout("overlay_renderer"),
        std::vector<WGPUBindGroupEntry> {
            position_view.create_bind_group_entry(0),
            normal_view.create_bind_group_entry(1),
            m_output_texture->texture_view().create_bind_group_entry(2),
        },
        "overlay renderer bind group");

    WGPUComputePassDescriptor compute_pass_desc {};
    compute_pass_desc.label = WGPUStringView { .data = "height lines compute pass", .length = WGPU_STRLEN };
    webgpu::raii::ComputePassEncoder compute_pass(command_encoder, compute_pass_desc);

    wgpuComputePassEncoderSetBindGroup(compute_pass.handle(), 0, shared_config_bg, 0, nullptr);
    wgpuComputePassEncoderSetBindGroup(compute_pass.handle(), 1, camera_bg, 0, nullptr);
    wgpuComputePassEncoderSetBindGroup(compute_pass.handle(), 2, overlay_bind_group.handle(), 0, nullptr);

    const glm::uvec3 workgroup_counts = glm::ceil(
        glm::vec3(m_output_texture->texture().width(), m_output_texture->texture().height(), 1)
        / glm::vec3(16.0f, 16.0f, 1.0f));
    m_pipeline->run(compute_pass, workgroup_counts);
}

const webgpu::raii::TextureView* OverlayRenderer::result_view() const
{
    return m_output_texture ? &m_output_texture->texture_view() : nullptr;
}

tl::expected<radix::geometry::Aabb<2, double>, std::string> OverlayRenderer::load_aabb_from_file(const std::string& file_path)
{
    QFile aabb_file(QString::fromStdString(file_path));
    if (!aabb_file.open(QIODevice::ReadOnly)) {
        return tl::make_unexpected(std::format("Failed to open file {}", file_path));
    }
    QTextStream file_contents(&aabb_file);

    // parse extent file (very barebones rn, in the future we want to use geotiff anyway)
    // extent file contains the aabb of the aabb region (in world coordinates) the image overlay texture is associated with
    // each line contains exactly one floating point number (. as separator) with the following meaning:
    //   min_x
    //   min_y
    //   max_x
    //   max_y
    std::array<float, 4> contents;
    bool float_conversion_ok = false;
    for (size_t i = 0; i < contents.size(); i++) {
        QString line = file_contents.readLine();
        contents[i] = line.toFloat(&float_conversion_ok);
        if (!float_conversion_ok) {
            return tl::make_unexpected(std::format("Failed to parse file {}: Could not convert \"{}\" to float", file_path, line.toStdString()));
        }
    }

    if (contents[0] >= contents[2]) {
        return tl::make_unexpected(std::format("Failed to parse file {}: x_min ({}) must not be >= x_max ({})", file_path, contents[0], contents[2]));
    }

    if (contents[1] >= contents[3]) {
        return tl::make_unexpected(std::format("Failed to parse file {}: y_min ({}) must not be >= y_max ({})", file_path, contents[1], contents[3]));
    }

    return radix::geometry::Aabb<2, double> { { contents[0], contents[1] }, { contents[2], contents[3] } };
}

} // namespace webgpu_engine
