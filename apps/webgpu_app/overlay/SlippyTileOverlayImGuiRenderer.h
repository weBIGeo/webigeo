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

#include "OverlayImGuiRenderer.h"
#include <QElapsedTimer>
#include <nucleus/tile/TileLoadService.h>
#include <string>
#include <vector>
#include <webgpu/engine/overlay/SlippyTileOverlay.h>

namespace webgpu_engine {
class Context;
}

namespace webgpu_app {

class SlippyTileOverlayImGuiRenderer : public OverlayImGuiRenderer {
public:
    SlippyTileOverlayImGuiRenderer(webgpu_engine::SlippyTileOverlay& overlay, webgpu_engine::Context& context);

    std::string display_name() const override { return "Slippy Tile Overlay"; }
    bool render_custom_settings() override;

private:
    // Separate window listing the last wanted-tiles readback, opened from the settings panel.
    // Hovering a row highlights that tile in the overlay. Returns true if a redraw is needed.
    bool render_wanted_tiles_window();
    // Live stats + tuning knobs of the DemandScheduler behind the current source, inside that window.
    // No-op (returns false) for Quad sources. Returns true if a knob changed.
    bool render_demand_scheduler_section();

    // "Rebuild" stopwatch: wall-clock time from clearing the source until every tile the shader asked
    // for this frame is GPU-resident again. Comparable across scheduler modes because the wanted-tile
    // ids the overlay reads back are exactly the ids the dictionary is keyed by in both modes.
    void start_rebuild_measurement();
    void update_rebuild_measurement(); // called once per settings frame while a measurement runs
    [[nodiscard]] std::string rebuild_status_text() const;

    // "Benchmark": N rebuilds back to back, each one timed by the stopwatch above.
    void start_benchmark();
    void start_next_benchmark_rebuild();
    void finish_benchmark_run(); // records the rebuild that just completed, then starts the next or stops
    [[nodiscard]] std::string benchmark_summary_text() const; // avg / min / max / sample stddev, empty before the first result

    webgpu_engine::SlippyTileOverlay* m_slippy_overlay;
    webgpu_engine::Context* m_context;
    bool m_show_wanted_tiles_window = false;

    // Requires recording to be on (Wanted Tiles Stride > 0) -- without a wanted list there is nothing
    // to wait for. A rebuild that can't complete (404s, array too small to hold the whole wanted set)
    // is reported as "settled" once nothing changes for k_rebuild_settle_ms.
    static constexpr qint64 k_rebuild_settle_ms = 1500;
    bool m_rebuild_running = false;
    QElapsedTimer m_rebuild_timer; // since the Rebuild click
    qint64 m_rebuild_last_change_ms = 0; // timer value when the resident/missing counts last moved
    bool m_rebuild_saw_missing = false; // the clear is applied on the scheduler thread, so don't
                                        // accept the pre-clear readback as an instantly finished rebuild
    unsigned m_rebuild_last_resident = 0;
    unsigned m_rebuild_last_missing = 0;
    // Last finished measurement (-1 = none yet): duration, tiles resident and tiles still missing.
    qint64 m_rebuild_result_ms = -1;
    unsigned m_rebuild_result_resident = 0;
    unsigned m_rebuild_result_missing = 0;

    int m_bench_runs = 10; // the "runs" input
    bool m_bench_running = false;
    int m_bench_total = 0; // runs of the current benchmark
    int m_bench_remaining = 0; // rebuilds still to start
    struct BenchTiles {
        unsigned wanted = 0; // tiles the shader asked for
        unsigned wanted_resident = 0; // ... of which are on the GPU
        unsigned gpu_resident = 0; // everything on the GPU
        bool quads = false; // legacy source: its "tiles" are quads
    };
    BenchTiles m_bench_tiles;
    std::string m_bench_title; // "== <source> (LOD x.x) ==", taken at benchmark start
    nucleus::tile::TileLoadService::Totals m_bench_start_totals; // TileLoadService counters at benchmark start
    nucleus::tile::TileLoadService::Totals m_bench_traffic; // traffic since then, up to the last finished run
    std::vector<double> m_bench_samples_ms; // one entry per finished run
};

} // namespace webgpu_app
