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
    m_holder = nucleus::tile::setup::texture_scheduler(std::move(tile_service), aabb_decorator, scheduler_thread, config.settings);
    m_holder.scheduler->set_gpu_quad_limit(config.gpu_quad_limit);

    m_array.set_tile_limit(config.tile_limit);

    connect(m_holder.scheduler.get(), &nucleus::tile::TextureScheduler::gpu_tiles_updated, this, &TileSource::update_gpu_tiles);
}

TileSource::~TileSource() = default;

void TileSource::init(webgpu::Context& ctx)
{
    m_ctx = &ctx;
    m_array.init(ctx);
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
}

void TileSource::enable()
{
    auto* sched = m_holder.scheduler.get();
    const auto compression = m_config.compression;
    nucleus::utils::thread::async_call(sched, [sched, compression]() {
        sched->set_texture_compression_algorithm(compression);
        sched->set_enabled(true);
    });
}

void TileSource::teardown()
{
    // The scheduler and load service live on the scheduler thread; reset them there before the
    // Context stops that thread. Mirrors RenderingContext's teardown of its schedulers.
    nucleus::utils::thread::sync_call(m_holder.scheduler.get(), [this]() { m_holder.scheduler.reset(); });
    nucleus::utils::thread::sync_call(m_holder.tile_service.get(), [this]() { m_holder.tile_service.reset(); });
}

void TileSource::set_base_url(const QString& url)
{
    auto* svc = m_holder.tile_service.get();
    nucleus::utils::thread::async_call(svc, [svc, url]() { svc->set_base_url(url); });
}

void TileSource::clear_cache()
{
    auto* sched = m_holder.scheduler.get();
    nucleus::utils::thread::async_call(sched, [sched]() { sched->clear_full_cache(); });
}

void TileSource::set_enabled(bool enabled)
{
    auto* sched = m_holder.scheduler.get();
    nucleus::utils::thread::async_call(sched, [sched, enabled]() { sched->set_enabled(enabled); });
}

} // namespace webgpu_engine
