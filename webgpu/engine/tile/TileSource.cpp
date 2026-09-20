/*****************************************************************************
 * weBIGeo
 * Copyright (C) 2026 Gerald Kimmersdorfer
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

#include "webgpu/engine/tile/TileSource.h"

#include <QThread>
#include <algorithm>
#include <nucleus/utils/thread.h>

namespace {
WGPUSamplerDescriptor imagery_sampler_descriptor()
{
    WGPUSamplerDescriptor desc {};
    desc.label = WGPUStringView { .data = "tile source sampler", .length = WGPU_STRLEN };
    desc.addressModeU = WGPUAddressMode::WGPUAddressMode_ClampToEdge;
    desc.addressModeV = WGPUAddressMode::WGPUAddressMode_ClampToEdge;
    desc.addressModeW = WGPUAddressMode::WGPUAddressMode_ClampToEdge;
    desc.magFilter = WGPUFilterMode::WGPUFilterMode_Linear;
    desc.minFilter = WGPUFilterMode::WGPUFilterMode_Linear;
    desc.mipmapFilter = WGPUMipmapFilterMode::WGPUMipmapFilterMode_Linear;
    desc.lodMinClamp = 0.0f;
    desc.lodMaxClamp = 1.0f;
    desc.compare = WGPUCompareFunction::WGPUCompareFunction_Undefined;
    desc.maxAnisotropy = 1;
    return desc;
}
} // namespace

namespace webgpu_engine {

TileSource::TileSource(const Config& config, const nucleus::tile::utils::AabbDecoratorPtr& aabb_decorator, QThread* scheduler_thread)
    : m_config { config }
    , m_array(config.resolution, WGPUTextureFormat::WGPUTextureFormat_RGBA8Unorm, imagery_sampler_descriptor(), config.name.toStdString())
{
    auto tile_service = std::make_unique<nucleus::tile::TileLoadService>(config.url, config.pattern, config.file_ending);
    if (config.scheduler_mode == nucleus::tile::TileSchedulerMode::Demand) {
        m_demand_holder = nucleus::tile::setup::demand_scheduler(std::move(tile_service), scheduler_thread, config.demand_settings);
        connect(m_demand_holder.scheduler.get(), &nucleus::tile::DemandScheduler::gpu_tiles_updated, this, &TileSource::update_gpu_tiles);
        connect(m_demand_holder.scheduler.get(), &nucleus::tile::DemandScheduler::stats_updated, this, &TileSource::update_demand_stats);
        // Mirror of what the scheduler was constructed with; the scheduler itself already lives on its own thread.
        m_demand_tuning = { config.demand_settings.planner, config.demand_settings.max_in_flight, config.demand_settings.max_ship_per_update,
            config.demand_settings.gpu_tile_limit };
    } else {
        m_holder = nucleus::tile::setup::texture_scheduler(std::move(tile_service), aabb_decorator, scheduler_thread, config.settings);
        m_holder.scheduler->set_gpu_quad_limit(config.gpu_quad_limit);
        connect(m_holder.scheduler.get(), &nucleus::tile::TextureScheduler::gpu_tiles_updated, this, &TileSource::update_gpu_tiles);
    }

    m_array.set_tile_limit(config.tile_limit);

    // Seed the scheduler with the source's default threshold (keeps it off the camera fallback).
    set_pixel_error_threshold(m_pixel_error_threshold);
}

nucleus::tile::TileLoadService* TileSource::tile_load_service() const
{
    return m_config.scheduler_mode == nucleus::tile::TileSchedulerMode::Demand ? m_demand_holder.tile_service.get() : m_holder.tile_service.get();
}

void TileSource::submit_wanted(const void* owner, std::vector<nucleus::tile::WantedTile> wanted)
{
    auto* sched = m_demand_holder.scheduler.get();
    if (m_config.scheduler_mode != nucleus::tile::TileSchedulerMode::Demand || !sched)
        return;
    nucleus::utils::thread::async_call(sched, [sched, key = quintptr(owner), wanted = std::move(wanted)]() { sched->submit_wanted(key, wanted); });
}

void TileSource::update_demand_stats(const nucleus::tile::DemandScheduler::Stats& stats)
{
    m_demand_stats = stats;
    m_demand_tile_status.clear();
    m_demand_tile_status.reserve(stats.tile_status.size());
    for (const auto& [id, status] : stats.tile_status)
        m_demand_tile_status.emplace(id, status);
}

nucleus::tile::TileStatus TileSource::demand_tile_status(nucleus::tile::Id id) const
{
    const auto it = m_demand_tile_status.find(id);
    return it == m_demand_tile_status.end() ? nucleus::tile::TileStatus::Unknown : it->second;
}

void TileSource::set_demand_tuning(nucleus::tile::DemandScheduler::Tuning tuning)
{
    auto* sched = m_demand_holder.scheduler.get();
    if (m_config.scheduler_mode != nucleus::tile::TileSchedulerMode::Demand || !sched)
        return;
    // The array is sized once at init(), so the scheduler must never believe it has more layers than that.
    tuning.gpu_tile_limit = std::min(tuning.gpu_tile_limit, m_array.capacity());
    m_demand_tuning = tuning;
    nucleus::utils::thread::async_call(sched, [sched, tuning]() { sched->set_tuning(tuning); });
}

TileSource::~TileSource() = default;

namespace {
constexpr uint32_t DICT_SIZE = 256; // must match GpuArrayHelper::generate_dictionary()

std::unique_ptr<webgpu::raii::Texture> make_dictionary_texture(WGPUDevice device, WGPUTextureFormat format, const char* label)
{
    WGPUTextureDescriptor desc {};
    desc.label = WGPUStringView { .data = label, .length = WGPU_STRLEN };
    desc.dimension = WGPUTextureDimension::WGPUTextureDimension_2D;
    desc.size = { DICT_SIZE, DICT_SIZE, 1 };
    desc.mipLevelCount = 1;
    desc.sampleCount = 1;
    desc.format = format;
    desc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    return std::make_unique<webgpu::raii::Texture>(device, desc);
}
} // namespace

void TileSource::init(webgpu::Context& ctx)
{
    m_ctx = &ctx;
    m_array.init(ctx);

    m_dict = make_dictionary_texture(ctx.device(), WGPUTextureFormat::WGPUTextureFormat_RGBA32Uint, "tile source dict");
    m_dict_view = m_dict->create_view();

    upload_dictionary(); // seed with the (currently empty) dictionary so the texture holds valid data
}

void TileSource::upload_dictionary()
{
    if (!m_dict)
        return;
    const auto dict = m_array.generate_dictionary();
    m_dict->write(m_ctx->queue(), dict);
}

bool TileSource::has_tile_data(nucleus::tile::Id id) const
{
    return m_array.contains(id);
}

void TileSource::update_gpu_tiles(const std::vector<nucleus::tile::Id>& deleted_tiles, const std::vector<nucleus::tile::GpuTextureTile>& new_tiles)
{
    for (const auto& id : deleted_tiles)
        m_array.remove_tile(id);

    for (const auto& tile : new_tiles) {
        assert(tile.id.zoom_level < 100);
        assert(tile.texture);

        const auto layer_index = m_array.add_tile(tile.id);
        m_array.texture().write(m_ctx->queue(), tile.texture->front(), uint32_t(layer_index));
    }

    upload_dictionary(); // keep the GPU tile-id -> layer dictionary in sync with the array
    emit tiles_updated();
}

void TileSource::enable()
{
    const auto compression = m_config.compression;
    if (m_config.scheduler_mode == nucleus::tile::TileSchedulerMode::Demand) {
        auto* demand = m_demand_holder.scheduler.get();
        const auto name = m_config.name;
        nucleus::utils::thread::async_call(demand, [demand, compression, name]() {
            demand->set_texture_compression_algorithm(compression);
            // Named here on the scheduler thread (SchedulerDirector::check_in only takes quad schedulers): persistence needs the name.
            demand->set_name(name);
            demand->read_disk_cache();
            demand->set_enabled(true);
        });
        return;
    }

    auto* sched = m_holder.scheduler.get();
    nucleus::utils::thread::async_call(sched, [sched, compression]() {
        sched->set_texture_compression_algorithm(compression);
        sched->read_disk_cache();
        sched->set_enabled(true);
    });
}

void TileSource::teardown()
{
    // The scheduler and load service live on the scheduler thread; reset them there before the
    // Context stops that thread. Mirrors RenderingContext's teardown of its schedulers.
    if (m_config.scheduler_mode == nucleus::tile::TileSchedulerMode::Demand) {
        nucleus::utils::thread::sync_call(m_demand_holder.scheduler.get(), [this]() { m_demand_holder.scheduler.reset(); });
        nucleus::utils::thread::sync_call(m_demand_holder.tile_service.get(), [this]() { m_demand_holder.tile_service.reset(); });
        return;
    }
    nucleus::utils::thread::sync_call(m_holder.scheduler.get(), [this]() { m_holder.scheduler.reset(); });
    nucleus::utils::thread::sync_call(m_holder.tile_service.get(), [this]() { m_holder.tile_service.reset(); });
}

void TileSource::set_base_url(const QString& url)
{
    auto* svc = tile_load_service();
    nucleus::utils::thread::async_call(svc, [svc, url]() { svc->set_base_url(url); });
}

void TileSource::clear_cache()
{
    if (m_config.scheduler_mode == nucleus::tile::TileSchedulerMode::Demand) {
        auto* demand = m_demand_holder.scheduler.get();
        nucleus::utils::thread::async_call(demand, [demand]() { demand->clear_full_cache(); });
        return;
    }
    auto* sched = m_holder.scheduler.get();
    nucleus::utils::thread::async_call(sched, [sched]() {
        sched->clear_full_cache();
        // clear_full_cache doesn't schedule a new update; re-request what the current camera needs.
        sched->set_enabled(sched->enabled());
    });
}

void TileSource::set_enabled(bool enabled)
{
    if (m_config.scheduler_mode == nucleus::tile::TileSchedulerMode::Demand) {
        auto* demand = m_demand_holder.scheduler.get();
        nucleus::utils::thread::async_call(demand, [demand, enabled]() { demand->set_enabled(enabled); });
        return;
    }
    auto* sched = m_holder.scheduler.get();
    nucleus::utils::thread::async_call(sched, [sched, enabled]() { sched->set_enabled(enabled); });
}

void TileSource::set_pixel_error_threshold(float error_threshold_px)
{
    m_pixel_error_threshold = error_threshold_px;
    // Demand sources have no CPU refinement, the shader reads the threshold from this source directly.
    if (m_config.scheduler_mode == nucleus::tile::TileSchedulerMode::Demand)
        return;
    auto* sched = m_holder.scheduler.get();
    nucleus::utils::thread::async_call(sched, [sched, error_threshold_px]() { sched->set_pixel_error_threshold(error_threshold_px); });
}

} // namespace webgpu_engine
