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

#include <Buffer.h>

namespace webgpu_engine::compute::nodes {

// TODO doc
class ComputeReleasePointsNode : public Node {
    Q_OBJECT

public:
    // also used as uniform
    struct ReleasePointsSettings {
        float min_slope_angle = 28.0f; // min slope angle [rad]
        float max_slope_angle = 60.0f; // max slope angle [rad]
        glm::uvec2 sampling_density; // sampling density in x and y direction
    };

public:
    static glm::uvec3 SHADER_WORKGROUP_SIZE; // TODO currently hardcoded in shader! can we somehow not hardcode it? maybe using overrides

    ComputeReleasePointsNode(const PipelineManager& pipeline_manager, WGPUDevice device, const glm::uvec2& output_resolution, size_t capacity);

    void set_settings(const ReleasePointsSettings& settings) { m_settings_uniform.data = settings; }

    const TileStorageTexture& texture_storage() const { return m_output_texture; }
    TileStorageTexture& texture_storage() { return m_output_texture; }

public slots:
    void run_impl() override;

private:
    const PipelineManager* m_pipeline_manager;
    WGPUDevice m_device;
    WGPUQueue m_queue;

    webgpu_engine::Buffer<ReleasePointsSettings> m_settings_uniform;

    // input
    webgpu::raii::RawBuffer<GpuTileId> m_input_tile_ids;

    // output
    TileStorageTexture m_output_texture; // texture per tile
};

} // namespace webgpu_engine::compute::nodes
