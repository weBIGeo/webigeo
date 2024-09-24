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
#include "webgpu_engine/Buffer.h"
#include "webgpu_engine/PipelineManager.h"

namespace webgpu_engine::compute::nodes {

/// GPU compute node, calling run executes code on the GPU
class ComputeSnowNode : public Node {
    Q_OBJECT

public:
    enum Input : SocketIndex { TILE_ID_LIST_TO_PROCESS = 0, TILE_ID_TO_TEXTURE_ARRAY_INDEX_MAP = 1, TEXTURE_ARRAY = 2 };
    enum Output : SocketIndex { OUTPUT_TILE_ID_TO_TEXTURE_ARRAY_INDEX_MAP = 0, OUTPUT_TEXTURE_ARRAY = 1 };

    static glm::uvec3 SHADER_WORKGROUP_SIZE; // TODO currently hardcoded in shader! can we somehow not hardcode it? maybe using overrides

    struct SnowSettings {
        glm::vec4 angle = {
            1, // snow enabled
            0, // angle lower limit// angle lower limit
            30, // angle upper limit
            0, // angle blend
        };

        glm::vec4 alt = {
            1000, // min altitude
            200, // variation
            200, // blend
            1, // specular
        };
    };

    ComputeSnowNode(
        const PipelineManager& pipeline_manager, WGPUDevice device, const glm::uvec2& output_resolution, size_t capacity, WGPUTextureFormat output_format);

    const GpuHashMap<tile::Id, uint32_t, GpuTileId>& hash_map() const { return m_output_tile_map; }
    const TileStorageTexture& texture_storage() const { return m_output_texture; }
    void set_snow_settings(const SnowSettings& snow_settings) { m_input_snow_settings.data = snow_settings; }

public slots:
    void run_impl() override;

protected:
    Data get_output_data_impl(SocketIndex output_index) override;

private:
    const PipelineManager* m_pipeline_manager;
    WGPUDevice m_device;
    WGPUQueue m_queue;
    size_t m_capacity;
    bool m_should_output_files;

    // calculated on cpu-side before each invocation
    webgpu::raii::RawBuffer<glm::vec4> m_tile_bounds; // aabb per tile

    // input
    webgpu::raii::RawBuffer<GpuTileId> m_input_tile_ids; // tile ids for which to calculate normals
    webgpu_engine::Buffer<SnowSettings> m_input_snow_settings; // settings for snow overlay

    // output
    GpuHashMap<tile::Id, uint32_t, GpuTileId> m_output_tile_map; // hash map
    TileStorageTexture m_output_texture; // texture per tile
};

} // namespace webgpu_engine::compute::nodes
