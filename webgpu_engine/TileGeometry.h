/*****************************************************************************
 * weBIGeo
 * Copyright (C) 2023 Adam Celerek
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

#pragma once

#include <memory>

#include "Buffer.h"
#include "PipelineManager.h"
#include "webgpu_engine/compute/GpuTileId.h"
#include <QObject>
#include <nucleus/tile/DrawListGenerator.h>
#include <nucleus/tile/types.h>
#include <webgpu/raii/BindGroup.h>
#include <webgpu/raii/BindGroupLayout.h>
#include <webgpu/raii/TextureWithSampler.h>
#include <webgpu/webgpu.h>

namespace camera {
class Definition;
}

namespace webgpu_engine {

// TODO find better name
class TileIdToTextureLayerMap {

public:
    TileIdToTextureLayerMap() { }

    TileIdToTextureLayerMap(size_t capacity)
        : m_occupancy(capacity, false)
    {
    }

    void set_capacity(size_t capacity)
    {
        assert(capacity >= m_occupancy.size()); // currently shrinking not supported
        m_occupancy.resize(capacity, false);
    }

    [[nodiscard]] size_t capacity() const { return m_occupancy.size(); }

    [[nodiscard]] size_t num_loaded() const { return m_tile_id_to_texture_layer.size(); }

    [[nodiscard]] bool has_space_left() const { return m_tile_id_to_texture_layer.size() < capacity(); }

    [[nodiscard]] bool contains(const nucleus::tile::Id& tile_id) const { return m_tile_id_to_texture_layer.contains(tile_id); }

    [[nodiscard]] size_t get_texture_layer(const nucleus::tile::Id& tile_id) const { return m_tile_id_to_texture_layer.at(tile_id); }

    [[nodiscard]] size_t get_next_free_texture_layer() const
    {
        assert(has_space_left());

        const auto found_at = std::find(std::begin(m_occupancy), std::end(m_occupancy), false);
        return found_at - std::begin(m_occupancy);
    }

    size_t insert(const nucleus::tile::Id& tile_id)
    {
        assert(has_space_left());
        assert(!m_tile_id_to_texture_layer.contains(tile_id));

        const auto index = get_next_free_texture_layer();
        m_occupancy[index] = true;
        m_tile_id_to_texture_layer.emplace(tile_id, index);
        return index;
    }

    size_t erase(const nucleus::tile::Id& tile_id)
    {
        assert(m_tile_id_to_texture_layer.contains(tile_id));

        const auto index = get_texture_layer(tile_id);
        m_occupancy[index] = false;
        m_tile_id_to_texture_layer.erase(tile_id);
        return index;
    }

private:
    std::vector<bool> m_occupancy;
    std::unordered_map<nucleus::tile::Id, size_t, nucleus::tile::Id::Hasher> m_tile_id_to_texture_layer;
};

class TileGeometry : public QObject {
    Q_OBJECT
public:
    explicit TileGeometry(QObject* parent = nullptr);
    void init(WGPUDevice device); // needs OpenGL context
    void draw(
        WGPURenderPassEncoder render_pass, const nucleus::camera::Definition& camera, const nucleus::tile::DrawListGenerator::TileSet& draw_tiles, bool sort_tiles, glm::dvec3 sort_position) const;

    const nucleus::tile::DrawListGenerator::TileSet generate_tilelist(const nucleus::camera::Definition& camera) const;
    const nucleus::tile::DrawListGenerator::TileSet cull(const nucleus::tile::DrawListGenerator::TileSet& tileset, const nucleus::camera::Frustum& frustum) const;

    void set_permissible_screen_space_error(float new_permissible_screen_space_error);

    void set_pipeline_manager(const PipelineManager& pipeline_manager);

    std::unique_ptr<webgpu::raii::BindGroup> create_bind_group(const webgpu::raii::TextureView& view, const webgpu::raii::Sampler& sampler) const;

    size_t capacity() const;

signals:
    void tiles_changed();

public slots:
    void update_gpu_quads_height(const std::vector<nucleus::tile::GpuGeometryQuad>& new_quads, const std::vector<nucleus::tile::Id>& deleted_quads);
    void update_gpu_quads_ortho(const std::vector<nucleus::tile::GpuTextureQuad>& new_quads, const std::vector<nucleus::tile::Id>& deleted_quads);
    void set_aabb_decorator(const nucleus::tile::utils::AabbDecoratorPtr& new_aabb_decorator);
    void set_quad_limit(unsigned new_limit);

private:
    void add_height_tile(const nucleus::tile::Id id, nucleus::tile::SrsAndHeightBounds bounds, const nucleus::Raster<uint16_t>& heights);
    void remove_height_tile(const nucleus::tile::Id id);
    void add_ortho_tile(const nucleus::tile::Id id, const nucleus::utils::ColourTexture& ortho);
    void remove_ortho_tile(const nucleus::tile::Id id);

    static constexpr auto N_EDGE_VERTICES = 65;
    static constexpr auto ORTHO_RESOLUTION = 256;
    static constexpr auto HEIGHTMAP_RESOLUTION = 65;

    size_t m_num_layers;
    TileIdToTextureLayerMap m_loaded_height_textures;
    TileIdToTextureLayerMap m_loaded_ortho_textures;
    std::unordered_map<nucleus::tile::Id, nucleus::tile::SrsBounds, nucleus::tile::Id::Hasher> m_loaded_bounds;

    nucleus::tile::DrawListGenerator m_draw_list_generator;
    const nucleus::tile::DrawListGenerator::TileSet m_last_draw_list; // buffer last generated draw list

    WGPUDevice m_device = 0;
    WGPUQueue m_queue = 0;
    const PipelineManager* m_pipeline_manager = nullptr;

    size_t m_index_buffer_size;
    std::unique_ptr<webgpu::raii::RawBuffer<uint16_t>> m_index_buffer;
    std::unique_ptr<webgpu::raii::RawBuffer<glm::vec4>> m_bounds_buffer;
    std::unique_ptr<webgpu::raii::RawBuffer<int32_t>> m_tileset_id_buffer;
    std::unique_ptr<webgpu::raii::RawBuffer<int32_t>> m_zoom_level_buffer;
    std::unique_ptr<webgpu::raii::RawBuffer<int32_t>> m_height_texture_layer_buffer;
    std::unique_ptr<webgpu::raii::RawBuffer<int32_t>> m_ortho_texture_layer_buffer;
    std::unique_ptr<Buffer<int32_t>> m_n_edge_vertices_buffer;
    std::unique_ptr<webgpu::raii::RawBuffer<compute::GpuTileId>> m_tile_id_buffer;

    std::unique_ptr<webgpu::raii::TextureWithSampler> m_heightmap_textures;
    std::unique_ptr<webgpu::raii::TextureWithSampler> m_ortho_textures;
    std::unique_ptr<webgpu::raii::BindGroup> m_tile_bind_group;
};

} // namespace webgpu_engine
