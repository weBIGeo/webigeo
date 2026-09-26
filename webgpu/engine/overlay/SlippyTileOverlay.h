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
#include <optional>
#include <vector>
#include <webgpu/base/Buffer.h>
#include <webgpu/base/raii/CombinedComputePipeline.h>
#include <webgpu/base/raii/Pipeline.h>
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
        TargetTileId = 3, // color-codes the *ideal* target tile id itself (before residency fallback), see tile_id_debug_color() in the shader
    };

    // How the per-pixel target zoom is chosen (see resolve_tile_sample in slippy_tile_overlay.wgsl):
    //   PerPixel -- derived from the rasterizer-computed footprint (derivatives), varies within a
    //               single render tile (correctly accounts for oblique/foreshortened viewing angles).
    //               Handles anti-aliasing best -- the default.
    //   PerTile  -- computed per pixel from that pixel's own true distance to the camera (not an
    //               aggregate over the whole render tile), matching the shape of
    //               nucleus::tile::utils::refineFunctor/AabbDecorator's SSE LOD criterion. Fully
    //               decoupled from the underlying render/geometry tile's own granularity, so it
    //               behaves like an independent scheduler -- but since it's a continuous per-pixel
    //               value rounded independently, its zoom transitions are circular iso-distance
    //               rings from the camera that can cut through the middle of a single destination
    //               tile, rather than the grid-aligned rectangles the real hierarchical scheduler
    //               produces (see closed_form_target_zoom's NOTE in slippy_tile_overlay.wgsl for why
    //               that isn't corrected here).
    enum class ZoomSelectionMode : uint32_t { PerPixel = 0, PerTile = 1 };

    // Temporary: how to interpret the sampled tile RGBA, set explicitly via the UI (independent of
    // which source is selected -- see slippy_tile_overlay.wgsl's DATA_MODE_* consts).
    enum class DataMode : uint32_t {
        Rgba = 0, // plain imagery
        SnowAvg = 1, // snow-depth encoding (R channel), white ramping in over [0, 20cm] avg depth
        SnowAvgNormals = 2, // SnowAvg, additionally masked by surface steepness (gbuffer normal)
        Normals = 3,
        NormalsOverwrite = 4,
        // Sun-exposure encoding: R = high byte, G = low byte of a 16-bit big-endian mean
        // (Wh/m^2/day, quantized over [0, ENERGY_MAX], 0xFFFF = nodata), amber ramp. B (std) is
        // part of the format but not visualized yet. See decode_sun_energy_mean in
        // slippy_tile_overlay.wgsl for the format constants.
        SunEnergy = 5,
    };

    struct Settings {
        float opacity = 1.0f;
        uint32_t max_zoom = 20; // ceiling for the resolved per-pixel target zoom
        uint32_t min_zoom = 0; // floor for the resolved per-pixel target zoom (e.g. ~3 for sources with no coarser tiles)
        // Fallback texels-per-tile for the target-zoom estimate when no source is set; otherwise the
        // source's GPU array is authoritative (see update_settings).
        uint32_t tile_size = 256;
        float pixel_error_threshold = 2.0f; // fallback SSE threshold when no source is set; otherwise the TileSource owns it
        DebugView debug_view = DebugView::None;
        ZoomSelectionMode zoom_selection_mode = ZoomSelectionMode::PerPixel;
        DataMode data_mode = DataMode::Rgba;
        // Cross-fades between neighboring zoom levels near a transition instead of popping. Excluded
        // from DataMode::Normals and DataMode::NormalsOverwrite, which can't blend two encoded
        // normals with a plain color lerp. Off by default -- opt-in for comparison against the
        // existing hard cutover.
        bool blend_zoom_transitions = false;
        // Width (in zoom units) of the cross-fade band centered on each integer zoom boundary; see
        // resolve_tile_sample in slippy_tile_overlay.wgsl.
        float zoom_blend_band = 0.3f;
        // Per-frame "wanted tiles" recording (see record_wanted_tile in slippy_tile_overlay.wgsl):
        // every stride-th pixel in x and y atomically records its *ideal* target tile + a pixel count
        // into a small GPU hash set that is read back to the CPU. 0 = off (the feature is then fully
        // disabled: no atomics in the shader, no per-frame clear, no readback), 1 = every pixel (max
        // atomic contention), 4 = 1/16th of the pixels (counts scale accordingly).
        uint32_t wanted_tiles_stride = 4;
    };

    // One tile of the last wanted-tiles readback: an *ideal* target tile some visible pixels asked
    // for (resident or not) and how many of the recorded (i.e. every stride-th) pixels wanted it.
    using WantedTile = nucleus::tile::WantedTile;

    explicit SlippyTileOverlay(TileSource* source);
    ~SlippyTileOverlay() override;

    void init(Context& ctx) override;
    void update_settings();

    [[nodiscard]] TileSource* source() const { return m_source; }
    void set_source(TileSource* source);
    void draw(const WGPUCommandEncoder& command_encoder,
        const OverlayContext& octx,
        const webgpu::raii::TextureWithSampler& current_input,
        webgpu::raii::TextureWithSampler& target_output,
        glm::uvec2 output_size) override;

    // Last read-back wanted-tiles snapshot, sorted by descending pixel_count. Empty while
    // settings.wanted_tiles_stride == 0. Only updated from the render thread (the map callback), so
    // read it from there.
    [[nodiscard]] const std::vector<WantedTile>& wanted_tiles() const { return m_wanted_tiles; }
    // Slot count of the GPU hash set; once wanted_tiles().size() approaches it, tiles get dropped.
    [[nodiscard]] uint32_t wanted_tiles_capacity() const { return k_wanted_tile_slots; }

    // Tints all pixels whose ideal target is this tile (nullopt = off). Returns true if it changed,
    // i.e. a redraw is needed.
    bool set_highlight_tile(const std::optional<nucleus::tile::Id>& id);

    Settings settings;

private:
    void write_normals_to_gbuffer(const WGPUCommandEncoder& command_encoder, const OverlayContext& octx);

    struct GpuSettings {
        float opacity = 1.0f;
        uint32_t max_zoom = 20;
        uint32_t min_zoom = 0;
        uint32_t tile_size = 256;
        float pixel_error_threshold = 2.0f;
        uint32_t debug_view = 0;
        uint32_t zoom_selection_mode = 0;
        uint32_t data_mode = 0; // see DataMode
        uint32_t blend_zoom_transitions = 0;
        float zoom_blend_band = 0.3f;
        uint32_t wanted_tiles_stride = 4;
        uint32_t highlight_x = 0;
        uint32_t highlight_y = 0;
        uint32_t highlight_zoom = 0xFFFFFFFFu; // none
    };

    // One slot of the GPU wanted-tiles hash set; must match WantedTileSlot in slippy_tile_overlay.wgsl.
    // Keys are stored inverted (~nucleus::srs::pack) so that a zeroed buffer reads as all-empty.
    struct WantedTileSlot {
        uint32_t key_lo = 0;
        uint32_t key_hi = 0;
        uint32_t count = 0;
    };
    static constexpr uint32_t k_wanted_tile_slots = 4096; // must match WANTED_TILE_SLOTS in the shader (power of two)

    // Turns a read-back raw hash table into m_wanted_tiles (unpacked, sorted by descending count).
    void update_wanted_tiles(const std::vector<WantedTileSlot>& table); // NB: "slots" is a Qt keyword macro

    // Hands the current wanted tiles to the source's scheduler (Demand sources only, Quad sources ignore it).
    // Counts are scaled to real screen pixels (x stride^2). Does nothing if the list is unchanged.
    void feed_source();
    // Withdraws this overlay's demand from its source (no-op if it never fed anything).
    void clear_source_feed();

    webgpu::Context* m_ctx = nullptr;
    Context* m_engine_ctx = nullptr;
    TileSource* m_source = nullptr;
    std::vector<WantedTile> m_fed_tiles; // last list handed to the source, to skip identical feeds
    bool m_has_fed = false;
    std::unique_ptr<webgpu::raii::CombinedComputePipeline> m_pipeline;
    std::unique_ptr<webgpu::raii::GenericRenderPipeline> m_gbuffer_write_pipeline;
    std::unique_ptr<webgpu::Buffer<GpuSettings>> m_settings_uniform;
    // frame_local_id -> packed tile id (nucleus::srs::pack format), rebuilt every draw() call from
    // frame_tile_ids; lets the compute shader recover the render tile's actual id (tile_ref itself
    // only carries the 16-bit frame_local_id, not raw x/y/zoom -- see docs/masterplan.md Plan 3).
    std::unique_ptr<webgpu::raii::RawBuffer<glm::u32vec2>> m_frame_tile_ids_buffer;
    // Wanted-tiles hash set written by the compute pass (cleared every draw) + one MapRead staging
    // buffer it is copied into every draw(). The copy is encoded in draw();
    // the map is issued at the start of the *next* draw() (after that command buffer was submitted --
    // mapping a buffer with a pending copy would be a validation error). Drop-if-behind: a copy is
    // only encoded while the staging buffer is unmapped and no map is pending.
    std::unique_ptr<webgpu::raii::RawBuffer<WantedTileSlot>> m_wanted_tiles_buffer;
    std::unique_ptr<webgpu::raii::RawBuffer<WantedTileSlot>> m_wanted_tiles_staging;
    bool m_wanted_tiles_map_pending = false;
    std::vector<WantedTile> m_wanted_tiles; // last readback, see wanted_tiles()
};

} // namespace webgpu_engine
