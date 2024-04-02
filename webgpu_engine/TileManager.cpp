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

#include "TileManager.h"

#include "nucleus/camera/Definition.h"
#include "nucleus/utils/terrain_mesh_index_generator.h"

using webgpu_engine::TileManager;
using webgpu_engine::TileSet;

namespace {
template <typename T> int bufferLengthInBytes(const std::vector<T>& vec) { return int(vec.size() * sizeof(T)); }
} // namespace

namespace webgpu_engine {

TileManager::TileManager(QObject* parent)
    : QObject { parent }
{
}

void TileManager::init(WGPUDevice device, WGPUQueue queue)
{
    using nucleus::utils::terrain_mesh_index_generator::surface_quads_with_curtains;

    // create index buffer, vertex buffers and uniform buffer
    const std::vector<uint16_t> indices = surface_quads_with_curtains<uint16_t>(N_EDGE_VERTICES);
    m_index_buffer = std::make_unique<raii::RawBuffer<uint16_t>>(device, WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst, indices.size());
    m_index_buffer->write(queue, indices.data(), indices.size());
    m_index_buffer_size = indices.size();
    m_bounds_buffer = std::make_unique<raii::RawBuffer<glm::vec4>>(device, WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst, m_loaded_tiles.size());
    m_tileset_id_buffer = std::make_unique<raii::RawBuffer<int32_t>>(device, WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst, m_loaded_tiles.size());
    m_zoom_level_buffer = std::make_unique<raii::RawBuffer<int32_t>>(device, WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst, m_loaded_tiles.size());
    m_texture_layer_buffer = std::make_unique<raii::RawBuffer<int32_t>>(device, WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst, m_loaded_tiles.size());
    m_n_edge_vertices_buffer = std::make_unique<raii::Buffer<int32_t>>(device, WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst);
    m_n_edge_vertices_buffer->data = N_EDGE_VERTICES;
    m_n_edge_vertices_buffer->update_gpu_data(queue);

    WGPUTextureDescriptor height_texture_desc {};
    height_texture_desc.label = "height texture";
    height_texture_desc.dimension = WGPUTextureDimension::WGPUTextureDimension_2D;
    height_texture_desc.size = { HEIGHTMAP_RESOLUTION, HEIGHTMAP_RESOLUTION, uint32_t(m_loaded_tiles.size()) };
    height_texture_desc.mipLevelCount = 1;
    height_texture_desc.sampleCount = 1;
    height_texture_desc.format = WGPUTextureFormat::WGPUTextureFormat_R16Uint;
    height_texture_desc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;

    WGPUSamplerDescriptor height_sampler_desc {};
    height_sampler_desc.label = "height sampler";
    height_sampler_desc.addressModeU = WGPUAddressMode::WGPUAddressMode_ClampToEdge;
    height_sampler_desc.addressModeV = WGPUAddressMode::WGPUAddressMode_ClampToEdge;
    height_sampler_desc.addressModeW = WGPUAddressMode::WGPUAddressMode_ClampToEdge;
    height_sampler_desc.magFilter = WGPUFilterMode::WGPUFilterMode_Linear;
    height_sampler_desc.minFilter = WGPUFilterMode::WGPUFilterMode_Linear;
    height_sampler_desc.mipmapFilter = WGPUMipmapFilterMode::WGPUMipmapFilterMode_Linear;
    height_sampler_desc.lodMinClamp = 0.0f;
    height_sampler_desc.lodMaxClamp = 1.0f;
    height_sampler_desc.compare = WGPUCompareFunction::WGPUCompareFunction_Undefined;
    height_sampler_desc.maxAnisotropy = 1;

    m_heightmap_textures = std::make_unique<raii::TextureWithSampler>(device, height_texture_desc, height_sampler_desc);

    // TODO mipmaps and compression
    WGPUTextureDescriptor ortho_texture_desc {};
    ortho_texture_desc.label = "ortho texture";
    ortho_texture_desc.dimension = WGPUTextureDimension::WGPUTextureDimension_2D;
    // TODO: array layers might become larger than allowed by graphics API
    ortho_texture_desc.size = { ORTHO_RESOLUTION, ORTHO_RESOLUTION, uint32_t(m_loaded_tiles.size()) };
    ortho_texture_desc.mipLevelCount = 1;
    ortho_texture_desc.sampleCount = 1;
    ortho_texture_desc.format = WGPUTextureFormat::WGPUTextureFormat_RGBA8Unorm;
    ortho_texture_desc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;

    WGPUSamplerDescriptor ortho_sampler_desc {};
    ortho_sampler_desc.label = "ortho sampler";
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

    m_ortho_textures = std::make_unique<raii::TextureWithSampler>(device, ortho_texture_desc, ortho_sampler_desc);

    m_tile_bind_group_info = std::make_unique<raii::BindGroupWithLayout>("tile bind group");
    m_tile_bind_group_info->add_entry(0, *m_n_edge_vertices_buffer, WGPUShaderStage_Vertex);
    m_tile_bind_group_info->add_entry(1, m_heightmap_textures->texture_view(), WGPUShaderStage_Vertex, WGPUTextureSampleType_Uint);
    m_tile_bind_group_info->add_entry(2, m_heightmap_textures->sampler(), WGPUShaderStage_Vertex, WGPUSamplerBindingType_Filtering);
    m_tile_bind_group_info->add_entry(3, m_ortho_textures->texture_view(), WGPUShaderStage_Fragment, WGPUTextureSampleType_Float);
    m_tile_bind_group_info->add_entry(4, m_ortho_textures->sampler(), WGPUShaderStage_Fragment, WGPUSamplerBindingType_Filtering);
    m_tile_bind_group_info->init(device);

    m_device = device;
    m_queue = queue;
}

bool compareTileSetPair(std::pair<float, const TileSet*> t1, std::pair<float, const TileSet*> t2)
{
    return (t1.first < t2.first);
}

const nucleus::tile_scheduler::DrawListGenerator::TileSet TileManager::generate_tilelist(const nucleus::camera::Definition& camera) const {
    return m_draw_list_generator.generate_for(camera);
}

const nucleus::tile_scheduler::DrawListGenerator::TileSet TileManager::cull(const nucleus::tile_scheduler::DrawListGenerator::TileSet& tileset, const nucleus::camera::Frustum& frustum) const {
    return m_draw_list_generator.cull(tileset, frustum);
}

void TileManager::draw(WGPURenderPipeline render_pipeline, WGPURenderPassEncoder render_pass, const nucleus::camera::Definition& camera,
    const nucleus::tile_scheduler::DrawListGenerator::TileSet& draw_tiles, bool sort_tiles, glm::dvec3 sort_position) const
{
    // Sort depending on distance to sort_position
    std::vector<std::pair<float, const TileSet*>> tile_list;
    for (const auto& tileset : m_gpu_tiles) {
        float dist = 0.0;
        if (!draw_tiles.contains(tileset.tile_id))
            continue;
        if (sort_tiles) {
            glm::vec2 pos_wrt = glm::vec2(tileset.bounds.min.x - sort_position.x, tileset.bounds.min.y - sort_position.y);
            dist = glm::length(pos_wrt);
        }
        tile_list.push_back(std::pair<float, const TileSet*>(dist, &tileset));
    }
    if (sort_tiles)
        std::sort(tile_list.begin(), tile_list.end(), compareTileSetPair);

    std::vector<glm::vec4> bounds;
    bounds.reserve(tile_list.size());

    std::vector<int32_t> tileset_id;
    tileset_id.reserve(tile_list.size());

    std::vector<int32_t> zoom_level;
    zoom_level.reserve(tile_list.size());

    std::vector<int32_t> texture_layer;
    texture_layer.reserve(tile_list.size());

    for (const auto& tileset : tile_list) {
        bounds.emplace_back(tileset.second->bounds.min.x - camera.position().x, tileset.second->bounds.min.y - camera.position().y,
            tileset.second->bounds.max.x - camera.position().x, tileset.second->bounds.max.y - camera.position().y);
        tileset_id.emplace_back(tileset.second->tile_id.coords[0] + tileset.second->tile_id.coords[1]);
        zoom_level.emplace_back(tileset.second->tile_id.zoom_level);
        texture_layer.emplace_back(tileset.second->texture_layer);
    }

    // write updated vertex buffers
    m_bounds_buffer->write(m_queue, bounds.data(), bounds.size());
    m_tileset_id_buffer->write(m_queue, tileset_id.data(), tileset_id.size());
    m_zoom_level_buffer->write(m_queue, zoom_level.data(), zoom_level.size());
    m_texture_layer_buffer->write(m_queue, texture_layer.data(), texture_layer.size());

    // set bind group for uniforms, textures and samplers
    m_tile_bind_group_info->bind(render_pass, 2);

    // set index buffer and vertex buffers
    wgpuRenderPassEncoderSetIndexBuffer(render_pass, m_index_buffer->handle(), WGPUIndexFormat_Uint16, 0, m_index_buffer->size_in_byte());
    wgpuRenderPassEncoderSetVertexBuffer(render_pass, 0, m_bounds_buffer->handle(), 0, m_bounds_buffer->size_in_byte());
    wgpuRenderPassEncoderSetVertexBuffer(render_pass, 1, m_texture_layer_buffer->handle(), 0, m_texture_layer_buffer->size_in_byte());
    wgpuRenderPassEncoderSetVertexBuffer(render_pass, 2, m_tileset_id_buffer->handle(), 0, m_tileset_id_buffer->size_in_byte());
    wgpuRenderPassEncoderSetVertexBuffer(render_pass, 3, m_zoom_level_buffer->handle(), 0, m_zoom_level_buffer->size_in_byte());

    // set pipeline and draw call
    wgpuRenderPassEncoderSetPipeline(render_pass, render_pipeline);
    wgpuRenderPassEncoderDrawIndexed(render_pass, m_index_buffer_size, tile_list.size(), 0, 0, 0);
}

void TileManager::remove_tile(const tile::Id& tile_id)
{
    const auto t = std::find(m_loaded_tiles.begin(), m_loaded_tiles.end(), tile_id);
    assert(t != m_loaded_tiles.end()); // removing a tile that's not here. likely there is a race.
    *t = tile::Id { unsigned(-1), {} };
    m_draw_list_generator.remove_tile(tile_id);

    // clear slot
    // or remove from list and free resources
    const auto found_tile = std::find_if(m_gpu_tiles.begin(), m_gpu_tiles.end(), [&tile_id](const TileSet& tileset) { return tileset.tile_id == tile_id; });
    if (found_tile != m_gpu_tiles.end())
        m_gpu_tiles.erase(found_tile);

    emit tiles_changed();
}

void TileManager::set_aabb_decorator(const nucleus::tile_scheduler::utils::AabbDecoratorPtr& new_aabb_decorator)
{
    m_draw_list_generator.set_aabb_decorator(new_aabb_decorator);
}

void TileManager::set_quad_limit(unsigned int new_limit)
{
    m_loaded_tiles.resize(new_limit * 4);
    std::fill(m_loaded_tiles.begin(), m_loaded_tiles.end(), tile::Id { unsigned(-1), {} });
}

void TileManager::add_tile(
    const tile::Id& id, tile::SrsAndHeightBounds bounds, const nucleus::utils::ColourTexture& ortho_texture, const nucleus::Raster<uint16_t>& height_map)
{
    TileSet tileset;
    tileset.tile_id = id;
    tileset.bounds = tile::SrsBounds(bounds);

    // find empty spot and upload texture
    const auto t = std::find(m_loaded_tiles.begin(), m_loaded_tiles.end(), tile::Id { unsigned(-1), {} });
    assert(t != m_loaded_tiles.end());
    *t = id;
    const auto layer_index = unsigned(t - m_loaded_tiles.begin());
    tileset.texture_layer = layer_index;

    m_ortho_textures->texture().write(m_queue, ortho_texture, layer_index);
    m_heightmap_textures->texture().write(m_queue, height_map, layer_index);

    // add to m_gpu_tiles
    m_gpu_tiles.push_back(tileset);
    m_draw_list_generator.add_tile(id);

    emit tiles_changed();
}

void TileManager::set_permissible_screen_space_error(float new_permissible_screen_space_error)
{
    m_draw_list_generator.set_permissible_screen_space_error(new_permissible_screen_space_error);
}

const raii::BindGroupWithLayout& TileManager::tile_bind_group() const { return *m_tile_bind_group_info; }

void TileManager::update_gpu_quads(const std::vector<nucleus::tile_scheduler::tile_types::GpuTileQuad>& new_quads, const std::vector<tile::Id>& deleted_quads)
{
    for (const auto& quad : deleted_quads) {
        for (const auto& id : quad.children()) {
            remove_tile(id);
        }
    }
    for (const auto& quad : new_quads) {
        for (const auto& tile : quad.tiles) {
            // test for validity
            assert(tile.id.zoom_level < 100);
            assert(tile.height);
            assert(tile.ortho);
            add_tile(tile.id, tile.bounds, *tile.ortho, *tile.height);
        }
    }
}

} // namespace webgpu_engine
