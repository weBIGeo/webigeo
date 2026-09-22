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

#include "SlippyTileOverlayImGuiRenderer.h"

#include <IconsFontAwesome5.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <imgui.h>
#include <iterator>
#include <numeric>
#include <nucleus/tile/TileSourcePresets.h>
#include <webgpu/engine/Context.h>
#include <webgpu/engine/tile/TileSource.h>

namespace webgpu_app {

namespace {
int current_preset_index(const webgpu_engine::SlippyTileOverlay& overlay)
{
    const auto& presets = nucleus::tile::tile_source_presets::all();
    const auto* source = overlay.source();
    if (!source)
        return 0;
    for (int i = 0; i < static_cast<int>(presets.size()); ++i)
        if (source->name() == presets[static_cast<size_t>(i)].source_name)
            return i;
    return 0;
}

struct StatusStyle {
    const char* label;
    ImVec4 color;
    const char* tooltip;
};

// Short enough for the table column; the tooltip carries the actual explanation.
StatusStyle status_style(nucleus::tile::TileStatus status)
{
    using S = nucleus::tile::TileStatus;
    switch (status) {
    case S::Resident:
        return { "ok", ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "On the GPU, exact match." };
    case S::Uploading:
        return { "upload", ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "Downloaded and decoded, waiting for an upload slot (Max Ship / Update)." };
    case S::InFlight:
        return { "load", ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "Requested, waiting for the network." };
    case S::Queued:
        return { "queue", ImVec4(0.7f, 0.7f, 0.8f, 1.0f), "Planned, waiting for a free in-flight slot (Max In Flight)." };
    case S::BackingOff:
        return { "retry", ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "A network error is being retried with backoff." };
    case S::NoData:
        return { "404", ImVec4(0.6f, 0.55f, 0.55f, 1.0f), "This tile or an ancestor returned 404 -- it will never become resident; a parent is shown instead." };
    case S::Skipped:
        return { "skip", ImVec4(0.55f, 0.55f, 0.55f, 1.0f), "Deliberately not requested: too few pixels (Min Pixels) and a close enough ancestor is resident (Max Gap)." };
    case S::Unknown:
        break;
    }
    return { "-", ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Not part of the scheduler's last plan (an ancestor was requested instead)." };
}
} // namespace

SlippyTileOverlayImGuiRenderer::SlippyTileOverlayImGuiRenderer(webgpu_engine::SlippyTileOverlay& overlay, webgpu_engine::Context& context)
    : OverlayImGuiRenderer(overlay)
    , m_slippy_overlay(&overlay)
    , m_context(&context)
{
}

bool SlippyTileOverlayImGuiRenderer::render_custom_settings()
{
    const auto& presets = nucleus::tile::tile_source_presets::all();
    auto& s = m_slippy_overlay->settings;
    bool changed = false;

    int preset_idx = current_preset_index(*m_slippy_overlay);

    std::string combo_items;
    for (const auto& preset : presets)
        combo_items += preset.display_name.toStdString() + '\0';
    combo_items += '\0';

    if (ImGui::Combo("Source", &preset_idx, combo_items.c_str())) {
        const auto& preset = presets[static_cast<size_t>(preset_idx)];
        m_slippy_overlay->set_source(m_context->get_or_create_tile_source(preset));
        s.max_zoom = preset.max_possible_zoom;
        // s.tile_size is not set here: update_settings() takes the texels per tile from the new
        // source's GPU array (a quad source's layer holds 2x the preset's raw tile resolution).
        m_slippy_overlay->update_settings();
        changed = true;
    }

    if (ImGui::SliderFloat("Opacity", &s.opacity, 0.0f, 1.0f)) {
        m_slippy_overlay->update_settings();
        changed = true;
    }

    const uint32_t max_possible_zoom = presets[static_cast<size_t>(preset_idx)].max_possible_zoom;
    int max_zoom = static_cast<int>(s.max_zoom);
    if (ImGui::SliderInt("Max Zoom", &max_zoom, 1, static_cast<int>(max_possible_zoom))) {
        s.max_zoom = static_cast<uint32_t>(max_zoom);
        m_slippy_overlay->update_settings();
        changed = true;
    }

    // Same "Level of Detail" convention as AppPanel's terrain slider (higher = sharper): displayed
    // value is the inverse of the raw pixel_error_threshold. The threshold lives on the TileSource
    // (single source of truth), so overlays sharing a source show and control the same value.
    if (auto* source = m_slippy_overlay->source()) {
        float level_of_detail = 1.0f / source->pixel_error_threshold();
        if (ImGui::SliderFloat("Level of Detail", &level_of_detail, 0.1f, 2.0f, "%.1f")) {
            source->set_pixel_error_threshold(1.0f / level_of_detail);
            m_slippy_overlay->update_settings();
            changed = true;
        }
    }

    int debug_view = static_cast<int>(s.debug_view);
    if (ImGui::Combo("Debug View", &debug_view, "None\0Zoom Level\0Target Zoom Level\0Target Tile Id\0")) {
        s.debug_view = static_cast<webgpu_engine::SlippyTileOverlay::DebugView>(debug_view);
        m_slippy_overlay->update_settings();
        changed = true;
    }

    int zoom_selection_mode = static_cast<int>(s.zoom_selection_mode);
    if (ImGui::Combo("Zoom Selection", &zoom_selection_mode, "Per Pixel\0Per Tile\0")) {
        s.zoom_selection_mode = static_cast<webgpu_engine::SlippyTileOverlay::ZoomSelectionMode>(zoom_selection_mode);
        m_slippy_overlay->update_settings();
        changed = true;
    }

    if (ImGui::Checkbox("Blend Zoom Transitions", &s.blend_zoom_transitions)) {
        m_slippy_overlay->update_settings();
        changed = true;
    }
    ImGui::BeginDisabled(!s.blend_zoom_transitions);
    if (ImGui::SliderFloat("Blend Band", &s.zoom_blend_band, 0.01f, 1.0f)) {
        m_slippy_overlay->update_settings();
        changed = true;
    }
    ImGui::EndDisabled();

    // Wanted-tiles recording: 0 = off (fully disabled), 1 = every pixel, N = every Nth pixel in x and y.
    int wanted_tiles_stride = static_cast<int>(s.wanted_tiles_stride);
    if (ImGui::SliderInt("Wanted Tiles Stride", &wanted_tiles_stride, 0, 16)) {
        s.wanted_tiles_stride = static_cast<uint32_t>(wanted_tiles_stride);
        m_slippy_overlay->update_settings();
        changed = true;
    }
    if (ImGui::Button("Show Wanted Tiles"))
        m_show_wanted_tiles_window = true;
    ImGui::SameLine();
    ImGui::BeginDisabled(m_bench_running);
    if (ImGui::Button("Rebuild") && m_slippy_overlay->source()) {
        m_slippy_overlay->source()->clear_cache();
        start_rebuild_measurement();
        m_context->request_redraw();
        changed = true;
    }
    ImGui::EndDisabled();
    update_rebuild_measurement();
    if (const auto status = rebuild_status_text(); !status.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", status.c_str());
    }

    // Benchmark: n back-to-back rebuilds -> avg / min / max / standard deviation of the rebuild time.
    const bool can_benchmark = m_slippy_overlay->source() && s.wanted_tiles_stride > 0;
    if (m_bench_running) {
        if (ImGui::Button("Cancel Benchmark"))
            m_bench_running = false; // the rebuild in progress just finishes as a normal single rebuild
    } else {
        ImGui::BeginDisabled(!can_benchmark || m_rebuild_running);
        if (ImGui::Button("Benchmark")) {
            start_benchmark();
            changed = true;
        }
        ImGui::EndDisabled();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90.0f);
    ImGui::BeginDisabled(m_bench_running);
    ImGui::InputInt("runs", &m_bench_runs, 1, 10);
    ImGui::EndDisabled();
    m_bench_runs = std::clamp(m_bench_runs, 1, 1000);
    if (m_bench_running)
        ImGui::TextDisabled("Benchmark: run %d / %d", static_cast<int>(m_bench_samples_ms.size()) + 1, m_bench_total);
    changed |= render_wanted_tiles_window();

    // Temporary: lets any source's RGBA be reinterpreted as the snow-depth or normal-map encoding for testing.
    int data_mode = static_cast<int>(s.data_mode);
    if (ImGui::Combo("Data Mode", &data_mode, "RGBA\0Snow Avg\0Snow Avg Normals\0Normals\0Normals Overwrite\0")) {
        s.data_mode = static_cast<webgpu_engine::SlippyTileOverlay::DataMode>(data_mode);
        m_slippy_overlay->update_settings();
        changed = true;
    }

    return changed;
}

void SlippyTileOverlayImGuiRenderer::start_rebuild_measurement()
{
    m_rebuild_running = true;
    m_rebuild_timer_start = std::chrono::steady_clock::now();
    m_rebuild_last_change_ms = 0;
    m_rebuild_saw_missing = false;
    m_rebuild_last_resident = 0;
    m_rebuild_last_missing = 0;
    m_rebuild_result_ms = -1;
    m_rebuild_result_resident = 0;
    m_rebuild_result_missing = 0;
}

void SlippyTileOverlayImGuiRenderer::update_rebuild_measurement()
{
    if (!m_rebuild_running)
        return;
    const auto* source = m_slippy_overlay->source();
    // Without the wanted-tile readback there is no "everything the frame asked for" to wait for.
    if (!source || m_slippy_overlay->settings.wanted_tiles_stride == 0) {
        m_rebuild_running = false;
        m_bench_running = false;
        return;
    }

    const auto& tiles = m_slippy_overlay->wanted_tiles();
    const bool demand = source->scheduler_mode() == nucleus::tile::TileSchedulerMode::Demand;
    unsigned resident = 0;
    unsigned no_data = 0;
    for (const auto& t : tiles) {
        if (source->has_tile_data(t.id))
            ++resident;
        else if (demand && source->demand_tile_status(t.id) == nucleus::tile::TileStatus::NoData)
            ++no_data; // 404: waiting for it would make a rebuild on a holey source never finish
    }
    const unsigned missing = static_cast<unsigned>(tiles.size()) - resident - no_data;
    m_rebuild_saw_missing = m_rebuild_saw_missing || missing > 0;

    const qint64 elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - m_rebuild_timer_start).count();
    if (resident != m_rebuild_last_resident || missing != m_rebuild_last_missing) {
        m_rebuild_last_resident = resident;
        m_rebuild_last_missing = missing;
        m_rebuild_last_change_ms = elapsed;
    }

    // Done once a drawn frame found everything it asked for. clear_cache() is applied on the
    // scheduler thread, so a rebuild only counts as complete after we have actually seen tiles go
    // missing -- otherwise the still-stale first readback would finish it at 0 ms.
    const bool complete = m_rebuild_saw_missing && !tiles.empty() && missing == 0;
    if (complete || elapsed - m_rebuild_last_change_ms > k_rebuild_settle_ms) {
        m_rebuild_running = false;
        // On a stall (404s, or a wanted set larger than the texture array) report when the counts
        // last moved, not when we gave up waiting.
        m_rebuild_result_ms = complete ? elapsed : m_rebuild_last_change_ms;
        m_rebuild_result_resident = resident;
        m_rebuild_result_missing = missing;
        if (m_bench_running)
            finish_benchmark_run();
        return;
    }

    // The wanted list is only refreshed by drawing, and once the last tile has arrived nothing else
    // asks for another frame -- so drive the frames ourselves for the duration of the measurement.
    m_context->request_redraw();
}

void SlippyTileOverlayImGuiRenderer::start_benchmark()
{
    if (!m_slippy_overlay->source())
        return;
    m_bench_running = true;
    m_bench_total = m_bench_runs;
    m_bench_remaining = m_bench_total;
    m_bench_samples_ms.clear();
    m_bench_start_totals = nucleus::tile::TileLoadService::totals();
    m_bench_traffic = {};

    char lod[16];
    std::snprintf(lod, sizeof(lod), "%.1f", 1.0f / m_slippy_overlay->source()->pixel_error_threshold());
    const auto& preset = nucleus::tile::tile_source_presets::all()[static_cast<size_t>(current_preset_index(*m_slippy_overlay))];
    m_bench_title = "== " + preset.display_name.toStdString() + " (LOD " + lod + ") ==";
    start_next_benchmark_rebuild();
}

void SlippyTileOverlayImGuiRenderer::start_next_benchmark_rebuild()
{
    --m_bench_remaining;
    m_slippy_overlay->source()->clear_cache();
    start_rebuild_measurement();
    m_context->request_redraw();
}

void SlippyTileOverlayImGuiRenderer::finish_benchmark_run()
{
    m_bench_samples_ms.push_back(static_cast<double>(m_rebuild_result_ms));
    const auto totals = nucleus::tile::TileLoadService::totals();
    m_bench_traffic = { totals.requests - m_bench_start_totals.requests, totals.bytes - m_bench_start_totals.bytes };

    // Tile state at the end of the latest run (the camera is static, so every run ends alike).
    const auto* source = m_slippy_overlay->source();
    const auto& wanted = m_slippy_overlay->wanted_tiles();
    m_bench_tiles = {};
    m_bench_tiles.wanted = static_cast<unsigned>(wanted.size());
    for (const auto& t : wanted)
        if (source->has_tile_data(t.id))
            ++m_bench_tiles.wanted_resident;
    m_bench_tiles.gpu_resident = source->array().n_occupied();
    m_bench_tiles.quads = source->scheduler_mode() != nucleus::tile::TileSchedulerMode::Demand;
    if (m_bench_remaining > 0) {
        start_next_benchmark_rebuild();
    } else {
        m_bench_running = false;
        ImGui::SetClipboardText(benchmark_summary_text().c_str());
    }
}

std::string SlippyTileOverlayImGuiRenderer::benchmark_summary_text() const
{
    if (m_bench_samples_ms.empty())
        return {};
    const double n = static_cast<double>(m_bench_samples_ms.size());
    const double mean = std::accumulate(m_bench_samples_ms.begin(), m_bench_samples_ms.end(), 0.0) / n;
    const auto [min_it, max_it] = std::minmax_element(m_bench_samples_ms.begin(), m_bench_samples_ms.end());
    double squared_deviations = 0.0;
    for (const double ms : m_bench_samples_ms)
        squared_deviations += (ms - mean) * (ms - mean);
    const double stddev = n > 1.0 ? std::sqrt(squared_deviations / (n - 1.0)) : 0.0; // sample (n-1)

    // Traffic and requests are per run (total / runs). "Used" = GPU-resident tiles that are also in the wanted set.
    const char* unit = m_bench_tiles.quads ? "Quads" : "Tiles";
    const double used_percent = m_bench_tiles.gpu_resident > 0 ? 100.0 * m_bench_tiles.wanted_resident / m_bench_tiles.gpu_resident : 0.0;
    char buf[384];
    std::snprintf(buf, sizeof(buf),
        "Runs:      %zu\nTiming:    %.2fs +- %.2f (min %.2f, max %.2f)\nTraffic:   %.2fMB\nRequests:  %.0f\n%s:%*s%u / %u\nGPU %s: %u [%.0f%% used]",
        m_bench_samples_ms.size(), mean / 1000.0, stddev / 1000.0, *min_it / 1000.0, *max_it / 1000.0, static_cast<double>(m_bench_traffic.bytes) / n / 1e6,
        static_cast<double>(m_bench_traffic.requests) / n, unit, static_cast<int>(10 - std::strlen(unit)), "", m_bench_tiles.wanted_resident, m_bench_tiles.wanted,
        unit, m_bench_tiles.gpu_resident, used_percent);
    return m_bench_title + "\n" + buf;
}

std::string SlippyTileOverlayImGuiRenderer::rebuild_status_text() const
{
    char buf[96];
    if (m_rebuild_running) {
        const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - m_rebuild_timer_start).count();
        std::snprintf(buf, sizeof(buf), "%.1f s, %u missing...", elapsed_ms / 1000.0, m_rebuild_last_missing);
        return buf;
    }
    if (m_rebuild_result_ms < 0)
        return {};
    if (m_rebuild_result_missing > 0)
        std::snprintf(buf, sizeof(buf), "%.2f s, %u tiles (%u never arrived)", m_rebuild_result_ms / 1000.0, m_rebuild_result_resident, m_rebuild_result_missing);
    else
        std::snprintf(buf, sizeof(buf), "%.2f s, %u tiles", m_rebuild_result_ms / 1000.0, m_rebuild_result_resident);
    return buf;
}

bool SlippyTileOverlayImGuiRenderer::render_demand_scheduler_section()
{
    auto* source = m_slippy_overlay->source();
    if (!source || source->scheduler_mode() != nucleus::tile::TileSchedulerMode::Demand)
        return false;
    if (!ImGui::CollapsingHeader("Demand Scheduler"))
        return false;

    const auto& st = source->demand_stats();
    const double avg_update_ms = st.total_update_calls > 0 ? st.total_update_ms / double(st.total_update_calls) : 0.0;
    ImGui::Text("Update: %.2f ms (avg %.2f ms over %llu calls)", st.update_ms, avg_update_ms, static_cast<unsigned long long>(st.total_update_calls));
    ImGui::Text("Plan: %u wanted, %u to fetch, %u to ship", st.n_wanted, st.n_fetch_planned, st.n_ship_planned);
    ImGui::Text("Network: %u in flight, %u queued, %u backing off", st.n_in_flight, st.n_pending, st.n_backoff);
    ImGui::Text("Uploads: %u shipped, %u deferred", st.n_shipped, st.n_ship_deferred);
    if (st.n_ship_dropped_no_space > 0)
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "%u upload(s) dropped: no free array layer", st.n_ship_dropped_no_space);
    ImGui::Text("Evicted last tick: %u    RAM cache: %u tiles", st.n_evicted, st.n_ram);
    ImGui::Text("Totals: %llu sent, %llu ok, %llu 404, %llu net err, %llu aborted", static_cast<unsigned long long>(st.total_requested),
        static_cast<unsigned long long>(st.total_delivered), static_cast<unsigned long long>(st.total_not_found),
        static_cast<unsigned long long>(st.total_network_errors), static_cast<unsigned long long>(st.total_aborted));
    ImGui::SetItemTooltip("Since app start or the last Rebuild. After a restart '404' must stay at 0:\n"
                          "known-missing tiles come back as tombstones from the disk cache instead of being requested again.");

    ImGui::Separator();

    auto t = source->demand_tuning();
    bool changed = false;

    int max_gap = static_cast<int>(t.planner.max_gap);
    if (ImGui::SliderInt("Max Gap", &max_gap, 0, 6)) {
        t.planner.max_gap = static_cast<unsigned>(max_gap);
        changed = true;
    }
    ImGui::SetItemTooltip("How many ancestor levels the shader may fall back through before a closer\nfallback tile is requested alongside the wanted one.");

    int min_pixels = static_cast<int>(t.planner.min_pixels);
    if (ImGui::SliderInt("Min Pixels", &min_pixels, 0, 8192, "%d", ImGuiSliderFlags_Logarithmic)) {
        t.planner.min_pixels = static_cast<uint32_t>(min_pixels);
        changed = true;
    }
    ImGui::SetItemTooltip("Tiles covering fewer real screen pixels than this are not fetched as long as\na resident ancestor within Max Gap can stand in for them.");

    int anchor_zoom = static_cast<int>(t.planner.anchor_zoom);
    if (ImGui::SliderInt("Anchor Zoom", &anchor_zoom, 0, 16)) {
        t.planner.anchor_zoom = static_cast<unsigned>(anchor_zoom);
        changed = true;
    }
    ImGui::SetItemTooltip("Ancestors from this zoom down to the wanted tile are always kept resident,\nso there is coverage everywhere. Lower = more tiles held.");

    int order = static_cast<int>(t.planner.order);
    if (ImGui::Combo("Order", &order, "Zoom desc, then pixels\0Pixels x gap\0")) {
        t.planner.order = static_cast<nucleus::tile::TilePlanner::Order>(order);
        changed = true;
    }
    ImGui::SetItemTooltip("Request priority within each tier.");

    int max_in_flight = static_cast<int>(t.max_in_flight);
    if (ImGui::SliderInt("Max In Flight", &max_in_flight, 1, 1024, "%d", ImGuiSliderFlags_Logarithmic)) {
        t.max_in_flight = static_cast<unsigned>(max_in_flight);
        changed = true;
    }
    ImGui::SetItemTooltip("Concurrent HTTP requests. Higher fills the view faster but wastes more\nrequests on tiles the camera has already left (they get aborted).");

    int request_rate = static_cast<int>(t.request_rate);
    if (ImGui::SliderInt("Request Rate", &request_rate, 0, 2000, "%d", ImGuiSliderFlags_Logarithmic)) {
        t.request_rate = static_cast<unsigned>(request_rate);
        changed = true;
    }
    ImGui::SetItemTooltip("Max requests started per second (0 = unlimited), independent of how fast they finish.\nMax In Flight limits how many run at once, this limits how quickly new ones may start.\n"
                          "Tiles held back by it stay pending, so a replan can still reorder or drop them.");

    int max_ship = static_cast<int>(t.max_ship_per_update);
    if (ImGui::SliderInt("Max Ship / Update", &max_ship, 1, 256)) {
        t.max_ship_per_update = static_cast<unsigned>(max_ship);
        changed = true;
    }
    ImGui::SetItemTooltip("Tiles decoded and uploaded per 100 ms tick. Bounds scheduler-thread CPU;\ndeferred tiles are simply re-proposed on the next tick.");

    int gpu_limit = static_cast<int>(t.gpu_tile_limit);
    const int capacity = static_cast<int>(source->array().capacity());
    if (ImGui::SliderInt("GPU Tile Limit", &gpu_limit, 16, capacity)) {
        t.gpu_tile_limit = static_cast<unsigned>(gpu_limit);
        changed = true;
    }
    ImGui::SetItemTooltip("Array layers the scheduler may use. Can only be lowered below the array's\ncapacity (fixed at startup) -- useful to see how the view degrades under eviction pressure.");

    if (ImGui::SmallButton("Reset tuning")) {
        const auto max_zoom = t.planner.max_zoom; // comes from the preset, not a tunable
        t = {};
        t.planner.max_zoom = max_zoom;
        t.gpu_tile_limit = static_cast<unsigned>(capacity);
        changed = true;
    }

    if (changed) {
        source->set_demand_tuning(t);
        m_context->request_redraw();
    }
    return changed;
}

bool SlippyTileOverlayImGuiRenderer::render_wanted_tiles_window()
{
    if (!m_show_wanted_tiles_window)
        return m_slippy_overlay->set_highlight_tile(std::nullopt);

    std::optional<nucleus::tile::Id> hovered_tile;
    ImGui::SetNextWindowSize(ImVec2(360.0f, 420.0f), ImGuiCond_FirstUseEver);
    bool open = true;
    if (ImGui::Begin("Wanted Tiles###slippy_wanted_tiles", &open, ImGuiWindowFlags_NoSavedSettings)) {
        const auto& s = m_slippy_overlay->settings;
        const auto& tiles = m_slippy_overlay->wanted_tiles();
        const auto* source = m_slippy_overlay->source();

        // Residency (texture-array occupancy) is independent of wanted-tiles recording, so it's
        // shown even while recording is off.
        const unsigned resident_total = source ? source->array().n_occupied() : 0;
        const unsigned resident_capacity = source ? source->array().capacity() : 0;
        ImGui::Text("Resident: %u / %u", resident_total, resident_capacity);

        const bool demand = source && source->scheduler_mode() == nucleus::tile::TileSchedulerMode::Demand;
        render_demand_scheduler_section();

        if (s.wanted_tiles_stride == 0) {
            ImGui::TextDisabled("Requests: recording off (Wanted Tiles Stride = 0).");
        } else {
            unsigned resident_and_wanted = 0;
            for (const auto& t : tiles)
                if (source && source->has_tile_data(t.id))
                    ++resident_and_wanted;
            const unsigned resident_not_wanted = resident_total - resident_and_wanted;
            const unsigned wanted_not_resident = static_cast<unsigned>(tiles.size()) - resident_and_wanted;

            ImGui::Text("Resident & requested: %u", resident_and_wanted);
            ImGui::Text("Resident, not requested: %u", resident_not_wanted);
            ImGui::Text("Requested, not resident: %u", wanted_not_resident);
            // For Demand sources that number alone is not actionable: split it by what each tile is
            // waiting for. "404" and "skip" are steady states, everything else should drain to 0.
            if (demand && wanted_not_resident > 0) {
                unsigned by_status[8] = {};
                for (const auto& t : tiles)
                    if (!source->has_tile_data(t.id))
                        ++by_status[static_cast<size_t>(source->demand_tile_status(t.id))];
                ImGui::Indent();
                for (size_t i = 0; i < std::size(by_status); ++i) {
                    if (by_status[i] == 0)
                        continue;
                    const auto style = status_style(static_cast<nucleus::tile::TileStatus>(i));
                    ImGui::TextColored(style.color, "%s: %u", style.label, by_status[i]);
                    ImGui::SetItemTooltip("%s", style.tooltip);
                }
                ImGui::Unindent();
            }

            uint32_t total_pixels = 0;
            for (const auto& t : tiles)
                total_pixels += t.pixel_count;
            // Only every stride-th pixel in x and y is recorded; the planner is fed the counts scaled
            // back up to real screen pixels, which is also what Min Pixels is compared against.
            const uint32_t px_scale = s.wanted_tiles_stride * s.wanted_tiles_stride;
            ImGui::Text("%d tiles requested, %u recorded px = %llu screen px (stride %u)", static_cast<int>(tiles.size()), total_pixels,
                static_cast<unsigned long long>(total_pixels) * px_scale, s.wanted_tiles_stride);
            ImGui::SetItemTooltip("The shader records every stride-th pixel in x and y. 'Screen px' is the recorded count x stride^2,\n"
                                  "i.e. what the scheduler plans with (and what Min Pixels is compared against).");
            // The GPU-side set silently drops tiles once its hash table fills up.
            const uint32_t wanted_capacity = m_slippy_overlay->wanted_tiles_capacity();
            if (tiles.size() * 2 >= wanted_capacity)
                ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "Hash table %d%% full -- tiles may be missing.",
                    static_cast<int>(tiles.size() * 100 / wanted_capacity));
            ImGui::Separator();

            if (ImGui::BeginTable("wanted_tiles", 4, ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
                ImGui::TableSetupColumn("Tile (z/x/y)", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Rec. px", ImGuiTableColumnFlags_WidthFixed, 56.0f);
                ImGui::TableSetupColumn("Screen px", ImGuiTableColumnFlags_WidthFixed, 66.0f);
                ImGui::TableSetupColumn(demand ? "Status" : "Resident", ImGuiTableColumnFlags_WidthFixed, 56.0f);
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableHeadersRow();

                for (int i = 0; i < static_cast<int>(tiles.size()); ++i) {
                    const auto& t = tiles[static_cast<size_t>(i)];
                    const bool resident = source && source->has_tile_data(t.id);
                    ImGui::PushID(i);
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    // Full-row selectable, just to detect hover
                    char tile_label[48];
                    std::snprintf(tile_label, sizeof(tile_label), "%u/%u/%u", static_cast<unsigned>(t.id.zoom_level), static_cast<unsigned>(t.id.coords.x),
                        static_cast<unsigned>(t.id.coords.y));
                    ImGui::Selectable(tile_label, false, ImGuiSelectableFlags_SpanAllColumns);
                    if (ImGui::IsItemHovered())
                        hovered_tile = t.id;
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%u", t.pixel_count);
                    ImGui::TableSetColumnIndex(2);
                    // What the scheduler actually planned with -- compare this against Min Pixels.
                    ImGui::Text("%llu", static_cast<unsigned long long>(t.pixel_count) * px_scale);

                    ImGui::TableSetColumnIndex(3);
                    if (demand && !resident) {
                        // Why it isn't there yet -- or, for "404", why it never will be.
                        const auto style = status_style(source->demand_tile_status(t.id));
                        ImGui::TextColored(style.color, "%s", style.label);
                        ImGui::SetItemTooltip("%s", style.tooltip);
                    } else if (resident) {
                        ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), ICON_FA_CHECK);
                    } else {
                        ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.4f, 1.0f), ICON_FA_TIMES);
                    }
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
        }
    }
    ImGui::End();

    m_show_wanted_tiles_window = open;
    return m_slippy_overlay->set_highlight_tile(hovered_tile);
}

} // namespace webgpu_app
