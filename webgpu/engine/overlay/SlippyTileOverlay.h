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

#include "Overlay.h"
#include <memory>
#include <webgpu/base/Buffer.h>
#include <webgpu/base/raii/CombinedComputePipeline.h>
#include <webgpu/base/raii/RawBuffer.h>

namespace webgpu_engine {

class TileSource;

/// Screen-space compute overlay that paints a tile source (e.g. ortho imagery) onto the terrain.
/// Per pixel it reads the gbuffer's exact render-tile local uv + frame-local render-tile id +
/// isotropic mip footprint (tile_ref), recovers the render tile's actual id via a small per-frame
/// buffer (frame_local_id -> packed tile id), derives a per-pixel target zoom directly from the
/// footprint, and jumps straight to that zoom via calc_tile_id_and_uv_for_zoom_level, falling back
/// to a resident ancestor on a miss (source's GPU dictionary, walking up -- the scheduler's
/// ancestors-always-resident invariant means that's the only fallback direction ever needed).
/// Samples the resolved array layer (no mipmaps on the array; the zoom choice above already is the
/// "mip" selection) and blends premultiplied over the ping-pong background. Default z_index < 0
/// (pre-shading), so the compose pass folds it into albedo before lighting. See docs/masterplan.md
/// Plan 3.
class SlippyTileOverlay : public Overlay {
public:
    // Debug visualizations that replace the sampled imagery color entirely (opaque), for inspecting
    // what the tile-resolution walk actually picked per pixel.
    enum class DebugView : uint32_t {
        None = 0,
        ZoomLevel = 1, // color-codes the resolved (resident) tile's zoom level, see zoom_level_color() in the shader
        TargetZoomLevel = 2, // color-codes the *ideal* target zoom (before residency fallback), i.e. ignoring which tiles actually exist
    };

    // How the per-pixel target zoom is chosen (see docs/masterplan.md Plan 3):
    //   PerPixel -- derived from the rasterizer-computed footprint (derivatives), varies within a
    //               single render tile (correctly accounts for oblique/foreshortened viewing angles).
    //   PerTile  -- one CPU-computed value for the whole render tile (SlippyTileOverlay::draw,
    //               camera-distance-based, same shape as the mesh's own SSE LOD criterion), uniform
    //               across every pixel of that tile.
    enum class ZoomSelectionMode : uint32_t { PerPixel = 0, PerTile = 1 };

    struct Settings {
        float opacity = 1.0f;
        uint32_t max_zoom = 20; // ceiling for the resolved per-pixel target zoom
        uint32_t tile_size = 256; // source's tile resolution, used by the per-pixel target-zoom estimate
        float pixel_error_threshold = 2.0f; // per-overlay SSE threshold (mirrors nucleus::camera::Definition's default)
        DebugView debug_view = DebugView::None;
        ZoomSelectionMode zoom_selection_mode = ZoomSelectionMode::PerPixel;
    };

    explicit SlippyTileOverlay(TileSource* source);

    void init(Context& ctx) override;
    void update_settings();

    [[nodiscard]] TileSource* source() const { return m_source; }
    void set_source(TileSource* source);
    void draw(const WGPUCommandEncoder& command_encoder,
        const webgpu::raii::TextureView& normal_view,
        const webgpu::raii::TextureView& depth_view,
        const webgpu::raii::TextureView& tile_ref_view,
        const std::vector<nucleus::tile::TileBounds>& frame_tile_ids,
        const nucleus::camera::Definition& camera,
        const WGPUBindGroup& shared_config_bg,
        const WGPUBindGroup& camera_bg,
        const webgpu::raii::TextureWithSampler& current_input,
        webgpu::raii::TextureWithSampler& target_output,
        glm::uvec2 output_size) override;

    Settings settings;

private:
    struct GpuSettings {
        float opacity = 1.0f;
        uint32_t max_zoom = 20;
        uint32_t tile_size = 256;
        float pixel_error_threshold = 2.0f;
        uint32_t debug_view = 0;
        uint32_t zoom_selection_mode = 0;
        uint32_t _pad0 = 0;
        uint32_t _pad1 = 0;
    };

    webgpu::Context* m_ctx = nullptr;
    TileSource* m_source = nullptr;
    std::unique_ptr<webgpu::raii::CombinedComputePipeline> m_pipeline;
    std::unique_ptr<webgpu::Buffer<GpuSettings>> m_settings_uniform;
    // frame_local_id -> packed tile id (nucleus::srs::pack format), rebuilt every draw() call from
    // frame_tile_ids; lets the compute shader recover the render tile's actual id (tile_ref itself
    // only carries the 16-bit frame_local_id, not raw x/y/zoom -- see docs/masterplan.md Plan 3).
    std::unique_ptr<webgpu::raii::RawBuffer<glm::u32vec2>> m_frame_tile_ids_buffer;
    // frame_local_id -> a single target zoom for the whole render tile; only read by the shader when
    // settings.zoom_selection_mode == PerTile, but always uploaded so the UI toggle is instant.
    std::unique_ptr<webgpu::raii::RawBuffer<uint32_t>> m_frame_target_zoom_buffer;
};

} // namespace webgpu_engine
