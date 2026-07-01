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

#pragma once

#include "webgpu/engine/tile/GpuTileTextureArray.h"
#include <QObject>
#include <QString>
#include <memory>
#include <nucleus/tile/TextureScheduler.h>
#include <nucleus/tile/TileLoadService.h>
#include <nucleus/tile/setup.h>
#include <nucleus/tile/types.h>
#include <nucleus/tile/utils.h>
#include <nucleus/utils/ColourTexture.h>
#include <webgpu/base/Context.h>

class QThread;

namespace webgpu_engine {

/// One imagery tile source: a TextureScheduler (+ TileLoadService) streaming RGBA8 tiles into a
/// GpuTileTextureArray. Owned by the engine Context.
///
/// Threading: the scheduler (and its load service) are moved onto the Context's scheduler thread,
/// while this QObject and its GPU array live on the render thread. `gpu_tiles_updated` is therefore
/// delivered to update_gpu_tiles() as a queued cross-thread call (same pattern the terrain mesh used
/// for its ortho array before this class existed).
class TileSource : public QObject {
    Q_OBJECT
public:
    struct Config {
        QString name;
        QString url;
        nucleus::tile::TileLoadService::UrlPattern pattern = nucleus::tile::TileLoadService::UrlPattern::ZYX_yPointingSouth;
        QString file_ending = ".jpeg";
        uint32_t resolution = 512;
        nucleus::tile::Scheduler::Settings settings = { .tile_resolution = 256, .max_zoom_level = 20, .gpu_quad_limit = 1024 };
        unsigned gpu_quad_limit = 256;
        unsigned tile_limit = 1024;
        nucleus::utils::ColourTexture::Format compression = nucleus::utils::ColourTexture::Format::Uncompressed_RGBA;
    };

    TileSource(const Config& config, const nucleus::tile::utils::AabbDecoratorPtr& aabb_decorator, QThread* scheduler_thread);
    ~TileSource() override;

    void init(webgpu::Context& ctx); // create the GPU array (render thread)
    void enable(); // set compression + enable the scheduler (dispatched onto the scheduler thread)
    /// Reset the scheduler and load service on the scheduler thread. Must be called while that thread
    /// is still alive (i.e. before the Context stops it).
    void teardown();

    // Runtime controls (safe to call from the render thread; dispatched onto the scheduler thread).
    void set_base_url(const QString& url);
    void clear_cache();
    void set_enabled(bool enabled);

    [[nodiscard]] const QString& name() const { return m_config.name; }
    [[nodiscard]] std::shared_ptr<nucleus::tile::TextureScheduler> scheduler() const { return m_holder.scheduler; }
    [[nodiscard]] nucleus::tile::TileLoadService* tile_load_service() const { return m_holder.tile_service.get(); }
    [[nodiscard]] GpuTileTextureArray& array() { return m_array; }
    [[nodiscard]] const GpuTileTextureArray& array() const { return m_array; }

public slots:
    void update_gpu_tiles(const std::vector<nucleus::tile::Id>& deleted_tiles, const std::vector<nucleus::tile::GpuTextureTile>& new_tiles);

private:
    Config m_config;
    nucleus::tile::setup::TextureSchedulerHolder m_holder;
    GpuTileTextureArray m_array;
    webgpu::Context* m_ctx = nullptr;
};

} // namespace webgpu_engine
