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
#include <QDebug>

using webgpu_engine::CloudGeometry;
namespace webgpu_engine {

CloudGeometry::CloudGeometry(uint32_t cloud_resolution_xz)
    : QObject { nullptr }
    , m_cloud_resolution_xz { cloud_resolution_xz }
{
}

void CloudGeometry::init(WGPUDevice device)
{
    m_device = device;
    m_queue = wgpuDeviceGetQueue(device);

    const auto cloud_resolution = glm::uvec2(m_cloud_resolution_xz);
    const auto num_layers = m_loaded_cloud_textures.size();

    // TODO mipmaps and compression
    WGPUTextureDescriptor cloud_texture_desc {};
    cloud_texture_desc.label = WGPUStringView { .data = "cloud texture", .length = WGPU_STRLEN };
    cloud_texture_desc.dimension = WGPUTextureDimension::WGPUTextureDimension_2D;
    // TODO: array layers might become larger than allowed by graphics API
    cloud_texture_desc.size = { uint32_t(cloud_resolution.x), uint32_t(cloud_resolution.y), uint32_t(num_layers) };
    cloud_texture_desc.mipLevelCount = 1;
    cloud_texture_desc.sampleCount = 1;
    cloud_texture_desc.format = WGPUTextureFormat::WGPUTextureFormat_RGBA8Unorm;
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
    cloud_sampler_desc.lodMaxClamp = 1.0f;
    cloud_sampler_desc.compare = WGPUCompareFunction::WGPUCompareFunction_Undefined;
    cloud_sampler_desc.maxAnisotropy = 1;

    m_cloud_textures = std::make_unique<webgpu::raii::TextureWithSampler>(m_device, cloud_texture_desc, cloud_sampler_desc);

    glm::dvec2 world_bounds_min = nucleus::srs::lat_long_to_world(BOUNDS_MIN);
    glm::dvec2 world_bounds_max = nucleus::srs::lat_long_to_world(BOUNDS_MAX);
    m_tile_coords_offset = nucleus::srs::world_xy_to_tile_id(world_bounds_min, ZOOM_MAX).coords;

    m_shader_params_ubo = std::make_unique<Buffer<ShaderParams>>(m_device, WGPUBufferUsage_CopyDst | WGPUBufferUsage_Uniform);
    m_shader_params_ubo->data = { .bounds_min = glm::vec4(world_bounds_min, 0.0, 0.0), .bounds_max = glm::vec4(world_bounds_max, 14000.0, 0.0) };
    m_shader_params_ubo->update_gpu_data(m_queue);

    // this represents a flattened 2d lookup table
    m_cloud_tile_info_buffer
        = std::make_unique<webgpu::raii::RawBuffer<TileInfo>>(m_device, WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst, TILE_COUNT_TOTAL);
    m_tile_infos.resize(TILE_COUNT_TOTAL);

    m_cloud_bind_group = std::make_unique<webgpu::raii::BindGroup>(m_device,
        m_pipeline_manager->cloud_bind_group_layout(),
        std::initializer_list<WGPUBindGroupEntry> { m_shader_params_ubo->raw_buffer().create_bind_group_entry(0),
            m_cloud_textures->texture_view().create_bind_group_entry(1),
            m_cloud_textures->sampler().create_bind_group_entry(2),
            m_cloud_tile_info_buffer->create_bind_group_entry(3) },
        "cloud bind group");
}

void CloudGeometry::draw(WGPURenderPassEncoder render_pass, const nucleus::camera::Definition& camera) const
{
    m_cloud_tile_info_buffer->write(m_queue, m_tile_infos.data(), m_tile_infos.size());

    wgpuRenderPassEncoderSetBindGroup(render_pass, 1, m_cloud_bind_group->handle(), 0, nullptr);
    wgpuRenderPassEncoderDraw(render_pass, 3, 1, 0, 0);
}

void CloudGeometry::set_tile_limit(unsigned int num_tiles) { m_loaded_cloud_textures.set_tile_limit(num_tiles); }

void CloudGeometry::set_pipeline_manager(const PipelineManager& pipeline_manager) { m_pipeline_manager = &pipeline_manager; }

void CloudGeometry::update_gpu_tiles_cloud(const std::vector<nucleus::tile::Id>& deleted_tiles, const std::vector<nucleus::tile::GpuTextureTile>& new_tiles)
{
    for (const auto& id : deleted_tiles) {
        m_loaded_cloud_textures.remove_tile(id);
    }
    for (const auto& tile : new_tiles) {
        // test for validity
        assert(tile.id.zoom_level < 100);
        assert(tile.texture);

        // find empty spot and upload texture
        const auto layer_index = m_loaded_cloud_textures.add_tile(tile.id);
        m_cloud_textures->texture().write(m_queue, tile.texture->front(), uint32_t(layer_index));

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
                size_t info_index = y * TILE_COUNTS.x + x; // compute_tile_info_index(x, y, ZOOM_MAX);
                m_tile_infos[info_index] = { .index = layer_index, .zoom = tile.id.zoom_level };
            }
        }
    }
}

} // namespace webgpu_engine
