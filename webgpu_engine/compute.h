/*****************************************************************************
 * weBIGeo
 * Copyright (C) 2024 Gerald Kimmersdorfer
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

#include "PipelineManager.h"
#include "compute/GpuTileStorage.h"
#include "nucleus/tile_scheduler/TileLoadService.h"
#include "nucleus/tile_scheduler/tile_types.h"
#include "nucleus/timing/CpuTimer.h"
#include <QByteArray>
#include <QObject>
#include <vector>

namespace webgpu_engine {

struct RectangularTileRegion {
    glm::uvec2 min;
    glm::uvec2 max;
    unsigned int zoom_level;
    tile::Scheme scheme;

    std::vector<tile::Id> get_tiles() const;
};

class ComputeController : public QObject {
    Q_OBJECT

public:
    ComputeController(WGPUDevice device, const PipelineManager& pipeline_manager);
    ~ComputeController() = default;

    void request_tiles(const RectangularTileRegion& region);
    void run_pipeline();

    // write tile data to files only for debugging, writes next to app.exe
    void write_output_tiles(const std::filesystem::path& dir = ".") const;

    float get_last_tile_request_timing();
    float get_last_pipeline_run_timing();

public slots:
    void on_single_tile_received(const nucleus::tile_scheduler::tile_types::TileLayer& tile);

signals:
    void tiles_requested();
    void tiles_received();
    void pipeline_run_queued();
    void pipeline_done();

private:
    const size_t m_max_num_tiles = 256;
    const glm::uvec2 m_input_tile_resolution = { 65, 65 };
    const glm::uvec2 m_output_tile_resolution = { 256, 256 };

    size_t m_num_tiles_received = 0;
    size_t m_num_tiles_requested = 0;

    const PipelineManager* m_pipeline_manager;
    WGPUDevice m_device;
    WGPUQueue m_queue;
    std::unique_ptr<nucleus::tile_scheduler::TileLoadService> m_tile_loader;

    std::unique_ptr<raii::BindGroup> m_compute_bind_group;

    std::unique_ptr<ComputeTileStorage> m_input_tile_storage;
    std::unique_ptr<ComputeTileStorage> m_output_tile_storage;

    nucleus::timing::CpuTimer m_tile_request_timer;
    nucleus::timing::CpuTimer m_pipeline_run_timer;
};

} // namespace webgpu_engine
