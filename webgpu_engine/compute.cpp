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

#include "compute.h"

namespace webgpu_engine {

std::vector<tile::Id> RectangularTileRegion::get_tiles() const
{
    assert(min.x <= max.x);
    assert(min.y <= max.y);
    std::vector<tile::Id> tiles;
    tiles.reserve((min.x - max.x + 1) * (min.y - max.y + 1));
    for (unsigned x = min.x; x <= max.x; x++) {
        for (unsigned y = min.y; y <= max.y; y++) {
            tiles.emplace_back(tile::Id { zoom_level, { x, y }, scheme });
        }
    }
    return tiles;
}


ComputeController::ComputeController(WGPUDevice device, const PipelineManager& pipeline_manager)
    : m_pipeline_manager(&pipeline_manager)
    , m_device { device }
    , m_queue { wgpuDeviceGetQueue(m_device) }
    , m_tile_loader { std::make_unique<nucleus::tile_scheduler::TileLoadService>(
          "https://alpinemaps.cg.tuwien.ac.at/tiles/alpine_png/", nucleus::tile_scheduler::TileLoadService::UrlPattern::ZXY, ".png") }
    , m_input_tile_storage { std::make_unique<TextureArrayComputeTileStorage>(
          device, m_input_tile_resolution, m_max_num_tiles, WGPUTextureFormat_R16Uint, WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst) }
    , m_output_tile_storage { std::make_unique<TextureArrayComputeTileStorage>(device, m_output_tile_resolution, m_max_num_tiles, WGPUTextureFormat_RGBA8Unorm,
          WGPUTextureUsage_StorageBinding | WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst | WGPUTextureUsage_CopySrc) }
    , m_tile_request_timer("tile request", "cpu", 1, 1)
    , m_pipeline_run_timer("compute pipeline", "cpu", 1, 1)
{
    connect(m_tile_loader.get(), &nucleus::tile_scheduler::TileLoadService::load_finished, this, &ComputeController::on_single_tile_received);
    connect(this, &ComputeController::tiles_requested, this, [this]() { m_tile_request_timer.start(); });
    connect(this, &ComputeController::tiles_received, this, [this]() {
        m_tile_request_timer.stop();
        m_tile_request_timer.fetch_result();
    });
    connect(this, &ComputeController::pipeline_run_queued, this, [this]() { m_pipeline_run_timer.start(); });
    connect(this, &ComputeController::pipeline_done, this, [this]() {
        m_pipeline_run_timer.stop();
        m_pipeline_run_timer.fetch_result();
    });

    m_input_tile_storage->init();
    m_output_tile_storage->init();

    std::vector<WGPUBindGroupEntry> entries;
    std::vector<WGPUBindGroupEntry> input_entries = m_input_tile_storage->create_bind_group_entries({ 0, 1 });
    std::vector<WGPUBindGroupEntry> output_entries = m_output_tile_storage->create_bind_group_entries({ 2 });
    entries.insert(entries.end(), input_entries.begin(), input_entries.end());
    entries.insert(entries.end(), output_entries.begin(), output_entries.end());
    m_compute_bind_group = std::make_unique<raii::BindGroup>(device, pipeline_manager.compute_bind_group_layout(), entries, "compute controller bind group");
}

void ComputeController::request_tiles(const RectangularTileRegion& region)
{
    std::vector<tile::Id> tiles_in_region = region.get_tiles();
    assert(tiles_in_region.size() <= m_max_num_tiles);
    m_num_tiles_requested = tiles_in_region.size();
    std::cout << "requested " << m_num_tiles_requested << " tiles" << std::endl;
    m_num_tiles_received = 0;
    for (const auto& tile : tiles_in_region) {
        m_tile_loader->load(tile);
    }
    emit tiles_requested();
}

void ComputeController::run_pipeline()
{
    WGPUCommandEncoderDescriptor descriptor {};
    descriptor.label = "compute controller command encoder";
    raii::CommandEncoder encoder(m_device, descriptor);

    {
        WGPUComputePassDescriptor compute_pass_desc {};
        compute_pass_desc.label = "compute controller compute pass";
        raii::ComputePassEncoder compute_pass(encoder.handle(), compute_pass_desc);

        const glm::uvec3& workgroup_counts = { m_max_num_tiles, 1, 1 };
        wgpuComputePassEncoderSetBindGroup(compute_pass.handle(), 0, m_compute_bind_group->handle(), 0, nullptr);
        m_pipeline_manager->dummy_compute_pipeline().run(compute_pass, workgroup_counts);
    }

    WGPUCommandBufferDescriptor cmd_buffer_descriptor {};
    cmd_buffer_descriptor.label = "computr controller command buffer";
    WGPUCommandBuffer command = wgpuCommandEncoderFinish(encoder.handle(), &cmd_buffer_descriptor);
    wgpuQueueSubmit(m_queue, 1, &command);
    wgpuCommandBufferRelease(command);

    wgpuQueueOnSubmittedWorkDone(
        m_queue,
        []([[maybe_unused]] WGPUQueueWorkDoneStatus status, void* user_data) {
            ComputeController* _this = reinterpret_cast<ComputeController*>(user_data);
            _this->pipeline_done(); // emits signal pipeline_done()
        },
        this);
    emit pipeline_run_queued();
}

void ComputeController::write_output_tiles(const std::filesystem::path& dir) const
{
    std::filesystem::create_directories(dir);

    auto read_back_callback = [this, dir](size_t layer_index, std::shared_ptr<QByteArray> data) {
        QImage img((const uchar*)data->constData(), m_output_tile_resolution.x, m_output_tile_resolution.y, QImage::Format_RGBA8888);
        std::filesystem::path file_path = dir / std::format("tile_{}.png", layer_index);
        std::cout << "write to file " << file_path << std::endl;
        img.save(file_path.generic_string().c_str(), "PNG");
    };

    std::cout << "write to files" << std::endl;
    for (size_t i = 0; i < m_max_num_tiles; i++) {
        m_output_tile_storage->read_back_async(i, read_back_callback);
    }
}

float ComputeController::get_last_tile_request_timing() { return m_tile_request_timer.get_last_measurement(); }

float ComputeController::get_last_pipeline_run_timing() { return m_pipeline_run_timer.get_last_measurement(); }

void ComputeController::on_single_tile_received(const nucleus::tile_scheduler::tile_types::TileLayer& tile)
{
    std::cout << "received requested tile " << tile.id << std::endl;
    m_input_tile_storage->store(tile.id, tile.data);
    m_num_tiles_received++;
    if (m_num_tiles_received == m_num_tiles_requested) {
        emit tiles_received();
    }
}

} // namespace webgpu_engine
