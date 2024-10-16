/*****************************************************************************
 * weBIGeo
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

#include "Node.h"
#include "webgpu_engine/PipelineManager.h"

namespace webgpu_engine::compute::nodes {

class DownsampleTilesNode : public Node {
    Q_OBJECT

public:
    enum Input : SocketIndex {
        TILE_ID_LIST_TO_PROCESS = 0,
        TILE_ID_TO_TEXTURE_ARRAY_INDEX_MAP = 1,
        TEXTURE_ARRAY = 2,
    };
    enum Output : SocketIndex {
        OUTPUT_TILE_ID_TO_TEXTURE_ARRAY_INDEX_MAP = 0,
        OUTPUT_TEXTURE_ARRAY = 1,
    };

    static glm::uvec3 SHADER_WORKGROUP_SIZE; // TODO currently hardcoded in shader! can we somehow not hardcode it? maybe using overrides

public:
    DownsampleTilesNode(const PipelineManager& pipeline_manager, WGPUDevice device, size_t capacity, size_t num_downsample_levels = 1);

    GpuHashMap<tile::Id, uint32_t, GpuTileId>& hash_map();

    TileStorageTexture& texture_storage();

public slots:
    void run_impl() override;

protected:
    Data get_output_data_impl(SocketIndex output_index) override;

private:
    static std::vector<tile::Id> get_tile_ids_for_downsampled_tiles(const std::vector<tile::Id>& original_tile_ids);
    void compute_downsampled_tiles(const std::vector<tile::Id>& tile_ids);

private:
    const PipelineManager* m_pipeline_manager;
    WGPUDevice m_device;
    WGPUQueue m_queue;

    size_t m_num_downsample_steps; // how many zoomlevels should be downsampled
    webgpu::raii::RawBuffer<GpuTileId> m_input_tile_ids; // tile ids of (to be calculated) downsampled tiles
    std::unique_ptr<TileStorageTexture> m_internal_storage_texture; // stores output of downsampling before it is copied back to input hashmap
    std::unique_ptr<webgpu::raii::BindGroup> m_compute_bind_group;
};

} // namespace webgpu_engine::compute::nodes
