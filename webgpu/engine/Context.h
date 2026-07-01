/*****************************************************************************
 * weBIGeo
 * Copyright (C) 2024 Adam Celarek
 * Copyright (C) 2025 Patrick Komon
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

#include "UniformBufferObjects.h"
#include "cloud/CloudRenderer.h"
#include "nucleus/EngineContext.h"
#include "nucleus/tile/SchedulerDirector.h"
#include "nucleus/track/Manager.h"
#include "overlay/OverlayRenderer.h"
#include "sky/SkyRenderer.h"
#include "tile/TileSource.h"
#include "tile_mesh/TileMeshRenderer.h"
#include "track/TrackRenderer.h"
#include <QThread>
#include <memory>
#include <vector>
#include <webgpu/base/Context.h>

namespace nucleus::camera {
class Controller;
}
namespace nucleus::tile {
struct TileSourcePreset;
}

namespace webgpu_engine {

class Context : public nucleus::EngineContext {
    Q_OBJECT
public:
    Context(QObject* parent = nullptr);
    Context(Context const&) = delete;
    ~Context() override;
    void operator=(Context const&) = delete;

    TileMeshRenderer* tile_mesh_renderer() const;
    void set_tile_mesh_renderer(std::shared_ptr<TileMeshRenderer> new_tile_mesh_renderer);

    CloudRenderer* cloud_renderer() const;
    void set_cloud_renderer(std::shared_ptr<CloudRenderer> new_cloud_renderer);

    sky::SkyRenderer* sky_renderer() const;
    void set_sky_renderer(std::shared_ptr<sky::SkyRenderer> new_sky_renderer);

    OverlayRenderer* overlay_renderer() const;
    void set_overlay_renderer(std::shared_ptr<OverlayRenderer> new_overlay_renderer);

    TrackRenderer* track_renderer() const;
    void set_track_renderer(std::shared_ptr<TrackRenderer> new_track_renderer);

    /// Create an imagery tile source owned by this Context, register it with the scheduler director,
    /// and (if the Context is already alive) create its GPU array. Returns a non-owning pointer.
    TileSource* add_tile_source(const TileSource::Config& config);
    /// Remove a tile source if no overlay currently references it. Returns false (no-op) if it's
    /// still in use by a SlippyTileOverlay, or not found.
    bool remove_tile_source(TileSource* source);
    /// Find a tile source previously created from this preset (matched by preset.source_name), or
    /// create it via add_tile_source() on first use.
    TileSource* get_or_create_tile_source(const nucleus::tile::TileSourcePreset& preset);
    const std::vector<std::shared_ptr<TileSource>>& tile_sources() const { return m_tile_sources; }

    /// Forwards definition_changed to every current and future tile source's scheduler, so newly
    /// added sources (e.g. picked in the UI) refine with the camera without per-source app wiring.
    void set_camera_controller(nucleus::camera::Controller* controller);

    webgpu::Context& webgpu_ctx() { return *m_webgpu_ctx_ptr; }
    void set_webgpu_ctx(webgpu::Context& ctx);

    nucleus::track::Manager* track_manager() override;

    uboSharedConfig& shared_config();
    void request_redraw();

    // TODO: add after getting merge to work
    // TextureLayer* ortho_layer() const;
    // void set_ortho_layer(std::shared_ptr<TextureLayer> new_ortho_layer);

signals:
    void redraw_requested();

protected:
    void internal_initialise() override;
    void internal_destroy() override;

private:
    webgpu::Context* m_webgpu_ctx_ptr = nullptr;
    uboSharedConfig m_shared_config;
    std::shared_ptr<TileMeshRenderer> m_tile_mesh_renderer;
    std::shared_ptr<CloudRenderer> m_cloud_renderer;
    std::shared_ptr<sky::SkyRenderer> m_sky_renderer;
    std::shared_ptr<OverlayRenderer> m_overlay_renderer;
    std::shared_ptr<TrackRenderer> m_track_renderer;
    // std::shared_ptr<TextureLayer> m_ortho_layer;

    // Engine-owned imagery tile loading (see docs/slippy_tile_overlay_masterplan.md, Plan 2).
    std::unique_ptr<QThread> m_scheduler_thread;
    std::unique_ptr<nucleus::tile::SchedulerDirector> m_scheduler_director;
    std::vector<std::shared_ptr<TileSource>> m_tile_sources;
    nucleus::camera::Controller* m_camera_controller = nullptr;
};

} // namespace webgpu_engine
