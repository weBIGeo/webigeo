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
#include "nucleus/utils/terrain_mesh_index_generator.h"
#include <QDebug>

using webgpu_engine::CloudGeometry;

namespace {
template <typename T> int bufferLengthInBytes(const std::vector<T>& vec) { return int(vec.size() * sizeof(T)); }
} // namespace

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

    // create buffers for bounds, tile ids, zoom level, height and ortho texture buffers
    m_bounds_buffer = std::make_unique<webgpu::raii::RawBuffer<glm::vec4>>(m_device, WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst, num_layers);
    m_tileset_id_buffer = std::make_unique<webgpu::raii::RawBuffer<int32_t>>(m_device, WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst, num_layers);
    m_cloud_zoom_level_buffer = std::make_unique<webgpu::raii::RawBuffer<int32_t>>(m_device, WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst, num_layers);
    m_cloud_texture_layer_buffer = std::make_unique<webgpu::raii::RawBuffer<int32_t>>(m_device, WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst, num_layers);

    m_tile_id_buffer = std::make_unique<webgpu::raii::RawBuffer<compute::GpuTileId>>(m_device, WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst, num_layers);

    // TODO mipmaps and compression
    WGPUTextureDescriptor ortho_texture_desc {};
    ortho_texture_desc.label = WGPUStringView { .data = "cloud texture", .length = WGPU_STRLEN };
    ortho_texture_desc.dimension = WGPUTextureDimension::WGPUTextureDimension_2D;
    // TODO: array layers might become larger than allowed by graphics API
    ortho_texture_desc.size = { uint32_t(cloud_resolution.x), uint32_t(cloud_resolution.y), uint32_t(num_layers) };
    ortho_texture_desc.mipLevelCount = 1;
    ortho_texture_desc.sampleCount = 1;
    ortho_texture_desc.format = WGPUTextureFormat::WGPUTextureFormat_RGBA8Unorm;
    ortho_texture_desc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;

    WGPUSamplerDescriptor ortho_sampler_desc {};
    ortho_sampler_desc.label = WGPUStringView { .data = "cloud sampler", .length = WGPU_STRLEN };
    ortho_sampler_desc.addressModeU = WGPUAddressMode::WGPUAddressMode_ClampToEdge;
    ortho_sampler_desc.addressModeV = WGPUAddressMode::WGPUAddressMode_ClampToEdge;
    ortho_sampler_desc.addressModeW = WGPUAddressMode::WGPUAddressMode_ClampToEdge;
    ortho_sampler_desc.magFilter = WGPUFilterMode::WGPUFilterMode_Linear;
    ortho_sampler_desc.minFilter = WGPUFilterMode::WGPUFilterMode_Linear;
    ortho_sampler_desc.mipmapFilter = WGPUMipmapFilterMode::WGPUMipmapFilterMode_Linear;
    ortho_sampler_desc.lodMinClamp = 0.0f;
    ortho_sampler_desc.lodMaxClamp = 1.0f;
    ortho_sampler_desc.compare = WGPUCompareFunction::WGPUCompareFunction_Undefined;
    ortho_sampler_desc.maxAnisotropy = 1;

    m_cloud_textures = std::make_unique<webgpu::raii::TextureWithSampler>(m_device, ortho_texture_desc, ortho_sampler_desc);

    m_tile_bind_group = std::make_unique<webgpu::raii::BindGroup>(m_device,
        m_pipeline_manager->tile_bind_group_layout(),
        std::initializer_list<WGPUBindGroupEntry> {
            m_cloud_textures->texture_view().create_bind_group_entry(0),
            m_cloud_textures->sampler().create_bind_group_entry(1)},
        "tile bind group");
}

void CloudGeometry::draw(
    WGPURenderPassEncoder render_pass, const nucleus::camera::Definition& camera, const std::vector<nucleus::tile::TileBounds>& draw_tiles) const
{
    // set pipeline and draw call
    wgpuRenderPassEncoderSetPipeline(render_pass, m_pipeline_manager->render_tiles_pipeline().pipeline().handle());
    wgpuRenderPassEncoderDraw(render_pass, 3, 1, 0, 0);
}

void CloudGeometry::set_tile_limit(unsigned int num_tiles)
{
    m_loaded_cloud_textures.set_tile_limit(num_tiles);
}

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
    }
}

} // namespace webgpu_engine
