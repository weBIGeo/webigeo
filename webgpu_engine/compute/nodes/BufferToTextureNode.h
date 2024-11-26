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
#include "PipelineManager.h"

namespace webgpu_engine::compute::nodes {

// Takes a list of tile ids, a tile id-to-index hashmap and a int32 storage buffer.
// The storage buffer contains raster data per tile. The resolution has to be specified as output_resolution on construction.
//
// Data for tile_id, row_index (from top to bottom) and col_index (from left to right) is stored in the buffer at index
//
//     hashmap.get_index(tile_id) * tile_dimensions.x * tile_dimensions.y + row_index * tile_dimensions.x + col_index
//
// where tile_dimensions is the per-tile resolution.
//
// Each entry is expected to be between 0 and 2^32-1 (i.e. using full uint32).
// The entries are mapped to colors based on a color mapping function (defined in shader code).
//
// TODO: add settings struct, be able to change color mapping during runtime (without changing shader source and recompiling)
class BufferToTextureNode : public Node {
    Q_OBJECT

public:
    enum Input : SocketIndex {
        TILE_ID_LIST_TO_PROCESS = 0,
        TILE_ID_TO_TEXTURE_ARRAY_INDEX_MAP = 1,
        INPUT_STORAGE_BUFFER = 2,
    };
    enum Output : SocketIndex {
        OUTPUT_TEXTURE_ARRAY = 0,
    };

    static glm::uvec3 SHADER_WORKGROUP_SIZE; // TODO currently hardcoded in shader! can we somehow not hardcode it? maybe using overrides

    BufferToTextureNode(
        const PipelineManager& pipeline_manager, WGPUDevice device, const glm::uvec2& output_resolution, size_t capacity, WGPUTextureFormat output_format);

    const TileStorageTexture& texture_storage() const { return m_output_texture; }
    TileStorageTexture& texture_storage() { return m_output_texture; }

public slots:
    void run_impl() override;

private:
    const PipelineManager* m_pipeline_manager;
    WGPUDevice m_device;
    WGPUQueue m_queue;

    // input
    webgpu::raii::RawBuffer<GpuTileId> m_input_tile_ids;

    // output
    TileStorageTexture m_output_texture; // texture per tile
};

} // namespace webgpu_engine::compute::nodes
