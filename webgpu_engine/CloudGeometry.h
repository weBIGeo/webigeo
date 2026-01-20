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
#include <nucleus/tile/GpuArrayHelper.h>
#include <nucleus/tile/types.h>
#include <webgpu/raii/BindGroup.h>
#include <webgpu/raii/BindGroupLayout.h>
#include <webgpu/raii/TextureWithSampler.h>
#include <webgpu/webgpu.h>

namespace nucleus::camera {
class Definition;
}

namespace webgpu_engine {

class CloudGeometry : public QObject {
    Q_OBJECT
public:

    static constexpr uint32_t ZOOM_MIN = 6;
    static constexpr uint32_t ZOOM_MAX = 10;
    static constexpr glm::vec2 BOUNDS_MIN = {46.2, 9.4};
    static constexpr glm::vec2 BOUNDS_MAX = {49.2, 17.4};
    static constexpr glm::uvec2 TILE_COUNTS = {46/2, 26/2};
    static constexpr uint32_t TILE_COUNT_TOTAL = TILE_COUNTS.x * TILE_COUNTS.y;

    struct alignas(16) ShaderParams {
        glm::vec4 bounds_min;
        glm::vec4 bounds_max;
    };

    struct TileInfo {
        glm::uint32 index;
        glm::uint32 zoom;
    };

    explicit CloudGeometry(uint32_t cloud_resolution_xz);

    void init(WGPUDevice device);

    void draw(WGPURenderPassEncoder render_pass, const nucleus::camera::Definition& camera) const;

    void set_pipeline_manager(const PipelineManager& pipeline_manager);

    void set_tile_limit(unsigned new_limit);

signals:
    void tiles_changed();

public slots:
    void update_gpu_tiles_cloud(const std::vector<nucleus::tile::Id>& deleted_tiles, const std::vector<nucleus::tile::GpuTextureTile>& new_tiles);

private:

    // tile coordinates of the bounds min corner at max zoom level
    glm::uvec2 m_tile_coords_offset = {};

    uint32_t m_cloud_resolution_xz;
    nucleus::tile::GpuArrayHelper m_loaded_cloud_textures;

    WGPUDevice m_device = 0;
    WGPUQueue m_queue = 0;
    const PipelineManager* m_pipeline_manager = nullptr;

    std::unique_ptr<Buffer<ShaderParams>> m_shader_params_ubo;

    std::unique_ptr<webgpu::raii::RawBuffer<TileInfo>> m_cloud_tile_info_buffer;
    std::vector<TileInfo> m_tile_infos;

    std::unique_ptr<webgpu::raii::TextureWithSampler> m_cloud_textures;
    std::unique_ptr<webgpu::raii::BindGroup> m_cloud_bind_group;
};

} // namespace webgpu_engine
