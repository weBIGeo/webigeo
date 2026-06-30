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

#include "TextureToRgba8Node.h"

#include <QJsonObject>
#include <webgpu/base/raii/Texture.h>
#include <webgpu/base/raii/TextureWithSampler.h>

namespace webgpu_compute::nodes {

namespace {

enum class SampleType { Float, Uint, Sint };

SampleType sample_type_for_format(WGPUTextureFormat fmt)
{
    switch (fmt) {
    case WGPUTextureFormat_R16Uint:
    case WGPUTextureFormat_RG16Uint:
    case WGPUTextureFormat_RGBA16Uint:
    case WGPUTextureFormat_R32Uint:
    case WGPUTextureFormat_RG32Uint:
    case WGPUTextureFormat_RGBA32Uint:
        return SampleType::Uint;
    case WGPUTextureFormat_R16Sint:
    case WGPUTextureFormat_RG16Sint:
    case WGPUTextureFormat_RGBA16Sint:
    case WGPUTextureFormat_R32Sint:
    case WGPUTextureFormat_RG32Sint:
    case WGPUTextureFormat_RGBA32Sint:
        return SampleType::Sint;
    default:
        return SampleType::Float;
    }
}

int num_channels_for_format(WGPUTextureFormat fmt)
{
    switch (fmt) {
    case WGPUTextureFormat_R8Unorm:
    case WGPUTextureFormat_R16Uint:
    case WGPUTextureFormat_R16Sint:
    case WGPUTextureFormat_R32Uint:
    case WGPUTextureFormat_R32Sint:
    case WGPUTextureFormat_R32Float:
        return 1;
    case WGPUTextureFormat_RG8Unorm:
    case WGPUTextureFormat_RG16Uint:
    case WGPUTextureFormat_RG16Sint:
    case WGPUTextureFormat_RG32Uint:
    case WGPUTextureFormat_RG32Sint:
    case WGPUTextureFormat_RG32Float:
        return 2;
    default:
        return 4;
    }
}

WGPUTextureSampleType wgpu_sample_type(SampleType t)
{
    switch (t) {
    case SampleType::Uint: return WGPUTextureSampleType_Uint;
    case SampleType::Sint: return WGPUTextureSampleType_Sint;
    default: return WGPUTextureSampleType_Float;
    }
}

void register_variant(webgpu::RenderResourceRegistry& reg,
    const char* key, const char* shader_path, WGPUTextureSampleType sample_type,
    std::unique_ptr<webgpu::raii::CombinedComputePipeline>* pipeline_out)
{
    if (!reg.has_shader(key))
        reg.register_shader(key, shader_path);

    if (!reg.has_bind_group_layout(key)) {
        reg.register_bind_group_layout(key, [sample_type](WGPUDevice dev) {
            WGPUBindGroupLayoutEntry e0 {};
            e0.binding = 0;
            e0.visibility = WGPUShaderStage_Compute;
            e0.buffer.type = WGPUBufferBindingType_Uniform;

            WGPUBindGroupLayoutEntry e1 {};
            e1.binding = 1;
            e1.visibility = WGPUShaderStage_Compute;
            e1.texture.sampleType = sample_type;
            e1.texture.viewDimension = WGPUTextureViewDimension_2D;
            e1.texture.multisampled = false;

            WGPUBindGroupLayoutEntry e2 {};
            e2.binding = 2;
            e2.visibility = WGPUShaderStage_Compute;
            e2.storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
            e2.storageTexture.format = WGPUTextureFormat_RGBA8Unorm;
            e2.storageTexture.viewDimension = WGPUTextureViewDimension_2D;

            return std::make_unique<webgpu::raii::BindGroupLayout>(
                dev, std::vector<WGPUBindGroupLayoutEntry> { e0, e1, e2 }, "texture_to_rgba8 bind group layout");
        });
    }

    reg.register_pipeline([key, pipeline_out](WGPUDevice device, const webgpu::RenderResourceRegistry& reg) {
        *pipeline_out = std::make_unique<webgpu::raii::CombinedComputePipeline>(
            device, reg.shader(key), std::vector<const webgpu::raii::BindGroupLayout*> { &reg.bind_group_layout(key) });
    });
}

} // namespace

TextureToRgba8Node::TextureToRgba8Node(webgpu::Context& ctx)
    : TextureToRgba8Node(ctx, Settings())
{
}

TextureToRgba8Node::TextureToRgba8Node(webgpu::Context& ctx, const Settings& settings)
    : Node({ InputSocket(*this, "texture", data_type<const webgpu::raii::TextureWithSampler*>()) },
          { OutputSocket(*this, "rgba8_texture", data_type<const webgpu::raii::TextureWithSampler*>(),
              [this]() { return m_output_texture.get(); }) })
    , m_ctx(&ctx)
    , m_settings(settings)
    , m_uniform(ctx.device(), WGPUBufferUsage_CopyDst | WGPUBufferUsage_Uniform)
{
    auto& reg = ctx.resource_registry();
    register_variant(reg, "texture_to_rgba8_f32_compute", "webgpu_compute::texture_to_rgba8_f32_compute",
        WGPUTextureSampleType_Float, &m_pipeline_f32);
    register_variant(reg, "texture_to_rgba8_u32_compute", "webgpu_compute::texture_to_rgba8_u32_compute",
        WGPUTextureSampleType_Uint, &m_pipeline_u32);
    register_variant(reg, "texture_to_rgba8_i32_compute", "webgpu_compute::texture_to_rgba8_i32_compute",
        WGPUTextureSampleType_Sint, &m_pipeline_i32);
}

void TextureToRgba8Node::set_settings(const Settings& s) { m_settings = s; }

void TextureToRgba8Node::run_impl()
{
    if (!input_socket("texture").is_socket_connected()) {
        fail_run("TextureToRgba8Node: 'texture' input not connected");
        return;
    }

    const auto* input = std::get<data_type<const webgpu::raii::TextureWithSampler*>()>(input_socket("texture").get_connected_data());
    if (!input) {
        fail_run("TextureToRgba8Node: input texture is null");
        return;
    }

    const WGPUTextureFormat fmt = input->texture().descriptor().format;
    const SampleType st = sample_type_for_format(fmt);
    const int n_channels = num_channels_for_format(fmt);
    const uint32_t width = input->texture().descriptor().size.width;
    const uint32_t height = input->texture().descriptor().size.height;

    webgpu::raii::CombinedComputePipeline* pipeline = nullptr;
    const char* layout_key = nullptr;
    switch (st) {
    case SampleType::Float: pipeline = m_pipeline_f32.get(); layout_key = "texture_to_rgba8_f32_compute"; break;
    case SampleType::Uint:  pipeline = m_pipeline_u32.get(); layout_key = "texture_to_rgba8_u32_compute"; break;
    case SampleType::Sint:  pipeline = m_pipeline_i32.get(); layout_key = "texture_to_rgba8_i32_compute"; break;
    }
    if (!pipeline) {
        fail_run("TextureToRgba8Node: pipeline not ready");
        return;
    }

    // Update uniform
    m_uniform.data.value_min = m_settings.value_range.x;
    m_uniform.data.value_max = m_settings.value_range.y;
    m_uniform.data.num_channels = static_cast<uint32_t>(n_channels);
    m_uniform.update_gpu_data(m_ctx->queue());

    // Create output texture (RGBA8Unorm, StorageBinding + TextureBinding)
    WGPUTextureDescriptor tex_desc {};
    tex_desc.label = WGPUStringView { .data = "texture_to_rgba8 output", .length = WGPU_STRLEN };
    tex_desc.dimension = WGPUTextureDimension_2D;
    tex_desc.size = { width, height, 1 };
    tex_desc.mipLevelCount = 1;
    tex_desc.sampleCount = 1;
    tex_desc.format = WGPUTextureFormat_RGBA8Unorm;
    tex_desc.usage = static_cast<WGPUTextureUsage>(WGPUTextureUsage_StorageBinding | WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopySrc);

    WGPUSamplerDescriptor smp_desc {};
    smp_desc.label = WGPUStringView { .data = "texture_to_rgba8 sampler", .length = WGPU_STRLEN };
    smp_desc.addressModeU = WGPUAddressMode_ClampToEdge;
    smp_desc.addressModeV = WGPUAddressMode_ClampToEdge;
    smp_desc.addressModeW = WGPUAddressMode_ClampToEdge;
    smp_desc.magFilter = WGPUFilterMode_Linear;
    smp_desc.minFilter = WGPUFilterMode_Linear;
    smp_desc.mipmapFilter = WGPUMipmapFilterMode_Nearest;
    smp_desc.lodMinClamp = 0.0f;
    smp_desc.lodMaxClamp = 1.0f;
    smp_desc.compare = WGPUCompareFunction_Undefined;
    smp_desc.maxAnisotropy = 1;

    m_output_texture = std::make_unique<webgpu::raii::TextureWithSampler>(m_ctx->device(), tex_desc, smp_desc);

    // Storage view (mipLevelCount must be 1 for write-only storage)
    auto out_view_desc = m_output_texture->texture().default_texture_view_descriptor();
    out_view_desc.mipLevelCount = 1;
    auto out_view = m_output_texture->texture().create_view(out_view_desc);

    // Input texture view (default — all mips, just for textureLoad at mip 0)
    auto in_view = input->texture().create_view();

    // Create bind group
    std::vector<WGPUBindGroupEntry> entries {
        m_uniform.raw_buffer().create_bind_group_entry(0),
        in_view->create_bind_group_entry(1),
        out_view->create_bind_group_entry(2),
    };
    webgpu::raii::BindGroup bind_group(
        m_ctx->device(), m_ctx->resource_registry().bind_group_layout(layout_key), entries, "texture_to_rgba8 bind group");

    // Dispatch
    WGPUCommandEncoderDescriptor enc_desc {};
    enc_desc.label = WGPUStringView { .data = "texture_to_rgba8 encoder", .length = WGPU_STRLEN };
    webgpu::raii::CommandEncoder encoder(m_ctx->device(), enc_desc);

    {
        WGPUComputePassDescriptor pass_desc {};
        pass_desc.label = WGPUStringView { .data = "texture_to_rgba8 compute pass", .length = WGPU_STRLEN };
        webgpu::raii::ComputePassEncoder pass(encoder.handle(), pass_desc);
        wgpuComputePassEncoderSetBindGroup(pass.handle(), 0, bind_group.handle(), 0, nullptr);
        const glm::uvec3 wg = glm::ceil(glm::vec3(width, height, 1) / glm::vec3(16, 16, 1));
        pipeline->run(pass, wg);
    }

    WGPUCommandBufferDescriptor cmd_desc {};
    cmd_desc.label = WGPUStringView { .data = "texture_to_rgba8 command buffer", .length = WGPU_STRLEN };
    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder.handle(), &cmd_desc);
    wgpuQueueSubmit(m_ctx->queue(), 1, &cmd);
    wgpuCommandBufferRelease(cmd);

    wgpuQueueOnSubmittedWorkDone(m_ctx->queue(),
        WGPUQueueWorkDoneCallbackInfo {
            .nextInChain = nullptr,
            .mode = WGPUCallbackMode_AllowProcessEvents,
            .callback = [](WGPUQueueWorkDoneStatus, WGPUStringView, void* userdata, void*) {
                reinterpret_cast<TextureToRgba8Node*>(userdata)->complete_run();
            },
            .userdata1 = this,
            .userdata2 = nullptr,
        });
}

void TextureToRgba8Node::serialize_settings(QJsonObject& out) const
{
    out["value_min"] = m_settings.value_range.x;
    out["value_max"] = m_settings.value_range.y;
}

void TextureToRgba8Node::deserialize_settings(const QJsonObject& in)
{
    if (in.contains("value_min"))
        m_settings.value_range.x = static_cast<float>(in["value_min"].toDouble(m_settings.value_range.x));
    if (in.contains("value_max"))
        m_settings.value_range.y = static_cast<float>(in["value_max"].toDouble(m_settings.value_range.y));
}

} // namespace webgpu_compute::nodes
