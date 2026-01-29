/*****************************************************************************
 * weBIGeo
 * Copyright (C) 2023 Adam Celarek
 * Copyright (C) 2023 Gerald Kimmersdorfer
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

#include "CloudGeometry.h"

#include "nucleus/camera/Definition.h"
#include "nucleus/srs.h"
#include "nucleus/utils/terrain_mesh_index_generator.h"

using webgpu_engine::CloudGeometry;
namespace webgpu_engine {

CloudGeometry::CloudGeometry()
    : QObject { nullptr }
{
}

void CloudGeometry::init(WGPUDevice device)
{
    m_device = device;
    m_queue = wgpuDeviceGetQueue(device);
    // TODO mipmaps and compression
    WGPUTextureDescriptor cloud_texture_desc {};
    cloud_texture_desc.label = WGPUStringView { .data = "cloud texture", .length = WGPU_STRLEN };
    cloud_texture_desc.dimension = WGPUTextureDimension::WGPUTextureDimension_3D;
    // TODO: array layers might become larger than allowed by graphics API
    cloud_texture_desc.size = { TILE_RESOLUTION_XY * ATLAS_SCALE_XY, TILE_RESOLUTION_XY * ATLAS_SCALE_XY, TILE_RESOLUTION_Z * ATLAS_SCALE_Z };
    cloud_texture_desc.mipLevelCount = static_cast<uint32_t>(std::ceil(std::log2(TILE_RESOLUTION_XY)) - std::log2(16) + 1);
    cloud_texture_desc.sampleCount = 1;
    cloud_texture_desc.format = WGPUTextureFormat::WGPUTextureFormat_BC4RUnorm;
    cloud_texture_desc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;

    WGPUSamplerDescriptor cloud_sampler_desc {};
    cloud_sampler_desc.label = WGPUStringView { .data = "cloud sampler", .length = WGPU_STRLEN };
    cloud_sampler_desc.addressModeU = WGPUAddressMode::WGPUAddressMode_ClampToEdge;
    cloud_sampler_desc.addressModeV = WGPUAddressMode::WGPUAddressMode_ClampToEdge;
    cloud_sampler_desc.addressModeW = WGPUAddressMode::WGPUAddressMode_ClampToEdge;
    cloud_sampler_desc.magFilter = WGPUFilterMode::WGPUFilterMode_Linear;
    cloud_sampler_desc.minFilter = WGPUFilterMode::WGPUFilterMode_Linear;
    cloud_sampler_desc.mipmapFilter = WGPUMipmapFilterMode::WGPUMipmapFilterMode_Linear;
    cloud_sampler_desc.lodMinClamp = 0.0f;
    cloud_sampler_desc.lodMaxClamp = static_cast<float>(cloud_texture_desc.mipLevelCount);
    cloud_sampler_desc.compare = WGPUCompareFunction::WGPUCompareFunction_Undefined;
    cloud_sampler_desc.maxAnisotropy = 1;

    m_cloud_texture_atlas = std::make_unique<webgpu::raii::TextureWithSampler>(m_device, cloud_texture_desc, cloud_sampler_desc);

    glm::dvec2 world_bounds_min = nucleus::srs::lat_long_to_world(BOUNDS_MIN);
    glm::dvec2 world_bounds_max = nucleus::srs::lat_long_to_world(BOUNDS_MAX);
    m_tile_coords_offset = nucleus::srs::world_xy_to_tile_id(world_bounds_min, ZOOM_MAX).coords;
    glm::dvec2 world_bounds_min_aligned = nucleus::srs::tile_id_to_world_xy(m_tile_coords_offset, ZOOM_MAX);
    glm::dvec2 world_bounds_max_aligned = nucleus::srs::tile_id_to_world_xy(m_tile_coords_offset + TILE_COUNTS, ZOOM_MAX);

    m_render_shader_params_ubo = std::make_unique<Buffer<ShaderParamsRender>>(m_device, WGPUBufferUsage_CopyDst | WGPUBufferUsage_Uniform);
    // TODO: Use aligned or not?
    m_render_shader_params_ubo->data.bounds_min = glm::vec4(world_bounds_min, 0.0, 0.0);
    m_render_shader_params_ubo->data.bounds_max = glm::vec4(world_bounds_max, 14000.0, 0.0);
    m_render_shader_params_ubo->data.start_distance = 0.0;
    m_render_shader_params_ubo->data.start_step_size = 20.0;
    m_render_shader_params_ubo->data.end_distance = 50000.0;
    m_render_shader_params_ubo->data.end_step_size = 1000.0;
    m_render_shader_params_ubo->data.extinction_multiplier = 1.0;;
    m_render_shader_params_ubo->data.detail_strength = 1.0;;
    m_upscale_shader_params_ubo = std::make_unique<Buffer<ShaderParamsUpscale>>(m_device, WGPUBufferUsage_CopyDst | WGPUBufferUsage_Uniform);

    // this represents a flattened 2d lookup table
    m_cloud_tile_info_buffer
        = std::make_unique<webgpu::raii::RawBuffer<TileInfo>>(m_device, WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst, TILE_COUNT_TOTAL);
    m_tile_infos.resize(TILE_COUNT_TOTAL);

    m_linear_sampler = std::make_unique<webgpu::raii::Sampler>(m_device,
        WGPUSamplerDescriptor {
            .label = WGPUStringView { .data = "clouds upscale linear sampler", .length = WGPU_STRLEN },
            .addressModeU = WGPUAddressMode_ClampToEdge,
            .addressModeV = WGPUAddressMode_ClampToEdge,
            .magFilter = WGPUFilterMode_Linear,
            .minFilter = WGPUFilterMode_Linear,
            .mipmapFilter = WGPUMipmapFilterMode_Nearest,
            .lodMinClamp = 0.0f,
            .lodMaxClamp = 0.0f,
            .compare = WGPUCompareFunction::WGPUCompareFunction_Undefined,
            .maxAnisotropy = 1,
        });
}

struct FrameJitterData {
    glm::mat4 jittered_projection;
    glm::mat4 jittered_view_proj;
    glm::vec2 jitter_offset;
    uint32_t frame_index;
};

glm::vec2 generate_jitter_simple_4x(uint32_t frame_index, glm::uvec2 output_resolution)
{
    const glm::vec2 pattern[4] = {
        glm::vec2(-0.25f, -0.25f),
        glm::vec2(+0.25f, -0.25f),
        glm::vec2(-0.25f, +0.25f),
        glm::vec2(+0.25f, +0.25f),
    };

    uint32_t pattern_index = frame_index % 4;
    glm::vec2 jitter = pattern[pattern_index];

    // Convert to NDC space
    jitter.x /= static_cast<float>(output_resolution.x);
    jitter.y /= static_cast<float>(output_resolution.y);

    return jitter;
}

float halton(uint32_t index, uint32_t base) {
    float result = 0.0f;
    float f = 1.0f;
    uint32_t i = index;

    while (i > 0) {
        f = f / static_cast<float>(base);
        result = result + f * static_cast<float>(i % base);
        i = i / base;
    }

    return result;
}

glm::vec2 generate_jitter_halton(uint32_t frame_index, glm::uvec2 output_resolution) {
    // Halton(2,3) sequence is optimal for 2D sampling
    // Use frame_index + 1 because Halton(0) = 0
    uint32_t sample_index = (frame_index % 4) + 1;

    float jitter_x = halton(sample_index, 2) - 0.5f;  // [-0.5, 0.5]
    float jitter_y = halton(sample_index, 3) - 0.5f;  // [-0.5, 0.5]

    // Convert to NDC space (normalized to render resolution)
    jitter_x /= static_cast<float>(output_resolution.x);
    jitter_y /= static_cast<float>(output_resolution.y);

    return glm::vec2(jitter_x, jitter_y);
}

glm::mat4 jitter_projection_matrix(const glm::mat4& projection, glm::vec2 jitter)
{
    // Jitter is in NDC space [-0.5, 0.5] mapped to pixels
    // Need to convert to NDC: multiply by 2 (since NDC is [-1, 1])
    glm::mat4 jittered = projection;

    // Modify the translation components (last column, rows 0 and 1)
    // These correspond to the x and y offset in clip space
    jittered[2][0] += jitter.x * 2.0f; // X offset
    jittered[2][1] += jitter.y * 2.0f; // Y offset

    return jittered;
}

inline int ceil_div(int x, int y) { return (x + y - 1) / y; }
inline unsigned ceil_div(unsigned x, unsigned y) { return (x + y - 1) / y; }

void CloudGeometry::resize(int w, int h)
{
    m_output_lo_resolution = { w / 2, h / 2 };
    m_output_hi_resolution = { w, h };

    m_upscale_shader_params_ubo->data.low_res_texel_size = 1.0f / glm::vec2(m_output_lo_resolution);
    m_upscale_shader_params_ubo->data.high_res_texel_size = 1.0f / glm::vec2(m_output_hi_resolution);

    m_clouds_lo_color_texture = std::make_unique<webgpu::raii::Texture>(m_device,
        WGPUTextureDescriptor {
            .label = WGPUStringView { .data = "clouds_lo_color", .length = WGPU_STRLEN },
            .usage = WGPUTextureUsage_StorageBinding | WGPUTextureUsage_TextureBinding,
            .dimension = WGPUTextureDimension_2D,
            .size = { .width = w / 2u, .height = h / 2u, .depthOrArrayLayers = 1 },
            .format = WGPUTextureFormat_RGBA16Float,
            .mipLevelCount = 1,
            .sampleCount = 1,
        });
    m_clouds_lo_color_texture_view = m_clouds_lo_color_texture->create_view();
    m_clouds_lo_depth_texture = std::make_unique<webgpu::raii::Texture>(m_device,
        WGPUTextureDescriptor {
            .label = WGPUStringView { .data = "clouds_lo_depth", .length = WGPU_STRLEN },
            .usage = WGPUTextureUsage_StorageBinding | WGPUTextureUsage_TextureBinding,
            .dimension = WGPUTextureDimension_2D,
            .size = { .width = w / 2u, .height = h / 2u, .depthOrArrayLayers = 1 },
            .format = WGPUTextureFormat_R32Float,
            .mipLevelCount = 1,
            .sampleCount = 1,
        });
    m_clouds_lo_depth_texture_view = m_clouds_lo_depth_texture->create_view();

    m_clouds_hi_color_texture = std::make_unique<webgpu::raii::Texture>(m_device,
        WGPUTextureDescriptor {
            .label = WGPUStringView { .data = "clouds_hi_color", .length = WGPU_STRLEN },
            .usage = WGPUTextureUsage_StorageBinding | WGPUTextureUsage_TextureBinding,
            .dimension = WGPUTextureDimension_2D,
            .size = { .width = static_cast<uint32_t>(w), .height = static_cast<uint32_t>(h), .depthOrArrayLayers = 1 },
            .format = WGPUTextureFormat_RGBA16Float,
            .mipLevelCount = 1,
            .sampleCount = 1,
        });
    m_clouds_hi_color_texture_view = m_clouds_hi_color_texture->create_view();
    m_clouds_hi_depth_texture = std::make_unique<webgpu::raii::Texture>(m_device,
        WGPUTextureDescriptor {
            .label = WGPUStringView { .data = "clouds_hi_depth", .length = WGPU_STRLEN },
            .usage = WGPUTextureUsage_StorageBinding | WGPUTextureUsage_TextureBinding,
            .dimension = WGPUTextureDimension_2D,
            .size = { .width = static_cast<uint32_t>(w), .height = static_cast<uint32_t>(h), .depthOrArrayLayers = 1 },
            .format = WGPUTextureFormat_R32Float,
            .mipLevelCount = 1,
            .sampleCount = 1,
        });
    m_clouds_hi_depth_texture_view = m_clouds_hi_depth_texture->create_view();

    m_render_clouds_bind_group = std::make_unique<webgpu::raii::BindGroup>(m_device,
        m_pipeline_manager->render_clouds_bind_group_layout(),
        std::initializer_list<WGPUBindGroupEntry> {
            m_render_shader_params_ubo->raw_buffer().create_bind_group_entry(0),
            m_cloud_texture_atlas->texture_view().create_bind_group_entry(1),
            m_cloud_texture_atlas->sampler().create_bind_group_entry(2),
            m_cloud_tile_info_buffer->create_bind_group_entry(3),
            m_clouds_lo_color_texture_view->create_bind_group_entry(4),
            m_clouds_lo_depth_texture_view->create_bind_group_entry(5),
        },
        "render clouds bind group");

    m_upscale_clouds_bind_group = std::make_unique<webgpu::raii::BindGroup>(m_device,
        m_pipeline_manager->upscale_clouds_bind_group_layout(),
        std::initializer_list<WGPUBindGroupEntry> {
            m_upscale_shader_params_ubo->raw_buffer().create_bind_group_entry(0),
            m_clouds_lo_color_texture_view->create_bind_group_entry(1),
            m_clouds_lo_depth_texture_view->create_bind_group_entry(2),
            m_linear_sampler->create_bind_group_entry(3),
            m_clouds_hi_color_texture_view->create_bind_group_entry(4),
            m_clouds_hi_depth_texture_view->create_bind_group_entry(5),
        },
        "upscale clouds bind group");
}

void CloudGeometry::draw(const WGPUCommandEncoder& command_encoder, const WGPUBindGroup& depth_texture_bind_group, const nucleus::camera::Definition& camera)
{

    // auto jitter_offset = generate_jitter_halton(m_frame_index, m_output_lo_resolution);
    auto jitter_offset = generate_jitter_simple_4x(m_frame_index, m_output_hi_resolution);
    auto unjittered_projection = camera.projection_matrix();
    auto jittered_projection = jitter_projection_matrix(unjittered_projection, jitter_offset);

    {
        WGPUComputePassDescriptor compute_pass_desc {};
        compute_pass_desc.label = WGPUStringView { .data = "cloud render pass", .length = WGPU_STRLEN };
        webgpu::raii::ComputePassEncoder compute_pass(command_encoder, compute_pass_desc);

        m_render_shader_params_ubo->data.camera = {
            .view_matrix = camera.local_view_matrix(),
            .proj_matrix = jittered_projection,
            .inv_view_matrix = glm::inverse(camera.local_view_matrix()),
            .inv_proj_matrix = glm::inverse(jittered_projection),
            .position = glm::vec4(camera.position(), 0.0f),
        };
        m_render_shader_params_ubo->data.frame_index = m_frame_index;
        m_render_shader_params_ubo->update_gpu_data(m_queue);

        m_cloud_tile_info_buffer->write(m_queue, m_tile_infos.data(), m_tile_infos.size());

        wgpuComputePassEncoderSetPipeline(compute_pass.handle(), m_pipeline_manager->render_clouds_pipeline().handle());
        wgpuComputePassEncoderSetBindGroup(compute_pass.handle(), 0, m_render_clouds_bind_group->handle(), 0, nullptr);
        wgpuComputePassEncoderSetBindGroup(compute_pass.handle(), 1, depth_texture_bind_group, 0, nullptr);

        wgpuComputePassEncoderDispatchWorkgroups(compute_pass.handle(), ceil_div(m_output_lo_resolution.x, 8u), ceil_div(m_output_lo_resolution.y, 8u), 1);
    }

    {
        WGPUComputePassDescriptor compute_pass_desc {};
        compute_pass_desc.label = WGPUStringView { .data = "cloud upscale pass", .length = WGPU_STRLEN };
        webgpu::raii::ComputePassEncoder compute_pass(command_encoder, compute_pass_desc);

        m_upscale_shader_params_ubo->data.previous_camera = m_upscale_shader_params_ubo->data.current_camera;
        m_upscale_shader_params_ubo->data.current_camera = {
            .view_matrix = camera.local_view_matrix(),
            .proj_matrix = unjittered_projection,
            .inv_view_matrix = glm::inverse(camera.local_view_matrix()),
            .inv_proj_matrix = glm::inverse(unjittered_projection),
            .position = glm::vec4(camera.position(), 0.0f),
        };
        m_upscale_shader_params_ubo->data.prev_jitter = m_upscale_shader_params_ubo->data.jitter;
        m_upscale_shader_params_ubo->data.jitter = jitter_offset;
        m_upscale_shader_params_ubo->update_gpu_data(m_queue);

        wgpuComputePassEncoderSetPipeline(compute_pass.handle(), m_pipeline_manager->upscale_clouds_pipeline().handle());
        wgpuComputePassEncoderSetBindGroup(compute_pass.handle(), 0, m_upscale_clouds_bind_group->handle(), 0, nullptr);

        wgpuComputePassEncoderDispatchWorkgroups(compute_pass.handle(), ceil_div(m_output_hi_resolution.x, 8u), ceil_div(m_output_hi_resolution.y, 8u), 1);
    }

    m_frame_index++;
}

void CloudGeometry::set_tile_limit(unsigned int num_tiles) { m_loaded_cloud_textures.set_tile_limit(num_tiles); }

void CloudGeometry::set_pipeline_manager(const PipelineManager& pipeline_manager) { m_pipeline_manager = &pipeline_manager; }

void CloudGeometry::update_gpu_tiles_cloud(const std::vector<nucleus::tile::Id>& deleted_tiles, const std::vector<nucleus::tile::GpuTexture3DTile>& new_tiles)
{
    std::lock_guard lock(m_mutex);
    for (const auto& id : deleted_tiles) {
        m_loaded_cloud_textures.remove_tile(id);
    }
    for (const auto& tile : new_tiles) {
        // test for validity
        assert(tile.id.zoom_level < 100);
        assert(tile.texture);

        // find empty spot and upload texture
        const auto layer_index = m_loaded_cloud_textures.add_tile(tile.id);

        uint32_t atlas_x = layer_index & 3u;
        uint32_t atlas_y = (layer_index >> 2) & 3u;
        uint32_t atlas_z = (layer_index >> 4) & 3u;
        assert(atlas_x < ATLAS_SCALE_XY);
        assert(atlas_y < ATLAS_SCALE_XY);
        assert(atlas_z < ATLAS_SCALE_Z);
        // Note: z is "up" in texture space
        for (int i = 0; i < tile.texture->size(); ++i) {
            const auto& level = tile.texture->at(i);
            glm::uvec3 atlas_offset = { atlas_x * level.width(), atlas_y * level.height(), atlas_z * level.depth() };
            m_cloud_texture_atlas->texture().write(m_queue, level, atlas_offset, i);
        }

        // convert to coords at max zoom level
        uint32_t d_z = ZOOM_MAX - tile.id.zoom_level;
        int32_t x_start = (tile.id.coords.x << d_z) - m_tile_coords_offset.x;
        int32_t y_start = (tile.id.coords.y << d_z) - m_tile_coords_offset.y;
        int32_t size = 1 << d_z;

        for (int32_t dy = 0; dy < size; dy++) {
            for (int32_t dx = 0; dx < size; dx++) {
                int32_t x = x_start + dx;
                int32_t y = y_start + dy;
                if (x < 0 || x >= TILE_COUNTS.x || y < 0 || y >= TILE_COUNTS.y)
                    continue;
                size_t info_index = y * TILE_COUNTS.x + x;
                m_tile_infos[info_index] = { .index = layer_index, .zoom = tile.id.zoom_level };
            }
        }
    }
}

} // namespace webgpu_engine
