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
#include <cstdio>
#include <imgui.h>
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
    if (ImGui::Button("Rebuild") && m_slippy_overlay->source()) {
        m_slippy_overlay->source()->clear_cache();
        start_rebuild_measurement();
        m_context->request_redraw();
        changed = true;
    }
    ImGui::SetItemTooltip("Clears everything this tile source holds (GPU tiles, RAM cache, 404 tombstones, disk cache)\n"
                          "and fetches it again, timing how long that takes. Affects all overlays using this source.");
    update_rebuild_measurement();
    if (const auto status = rebuild_status_text(); !status.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", status.c_str());
    }
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
    m_rebuild_timer.start();
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
        return;
    }

    const auto& tiles = m_slippy_overlay->wanted_tiles();
    unsigned resident = 0;
    for (const auto& t : tiles)
        if (source->has_tile_data(t.id))
            ++resident;
    const unsigned missing = static_cast<unsigned>(tiles.size()) - resident;
    m_rebuild_saw_missing = m_rebuild_saw_missing || missing > 0;

    const qint64 elapsed = m_rebuild_timer.elapsed();
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
        return;
    }

    // The wanted list is only refreshed by drawing, and once the last tile has arrived nothing else
    // asks for another frame -- so drive the frames ourselves for the duration of the measurement.
    m_context->request_redraw();
}

std::string SlippyTileOverlayImGuiRenderer::rebuild_status_text() const
{
    char buf[96];
    if (m_rebuild_running) {
        std::snprintf(buf, sizeof(buf), "%.1f s, %u missing...", m_rebuild_timer.elapsed() / 1000.0, m_rebuild_last_missing);
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
        const auto* load_service = source ? source->tile_load_service() : nullptr;

        // Residency (texture-array occupancy) is independent of wanted-tiles recording, so it's
        // shown even while recording is off.
        const unsigned resident_total = source ? source->array().n_occupied() : 0;
        const unsigned resident_capacity = source ? source->array().capacity() : 0;
        ImGui::Text("Resident: %u / %u", resident_total, resident_capacity);

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

            uint32_t total_pixels = 0;
            for (const auto& t : tiles)
                total_pixels += t.pixel_count;
            ImGui::Text("%d tiles requested, %u recorded pixels (stride %u)", static_cast<int>(tiles.size()), total_pixels, s.wanted_tiles_stride);
            // The GPU-side set silently drops tiles once its hash table fills up.
            const uint32_t wanted_capacity = m_slippy_overlay->wanted_tiles_capacity();
            if (tiles.size() * 2 >= wanted_capacity)
                ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "Hash table %d%% full -- tiles may be missing.",
                    static_cast<int>(tiles.size() * 100 / wanted_capacity));
            ImGui::Separator();

            if (ImGui::BeginTable("wanted_tiles", 6, ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
                ImGui::TableSetupColumn("Zoom", ImGuiTableColumnFlags_WidthFixed, 38.0f);
                ImGui::TableSetupColumn("X", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Y", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Pixels", ImGuiTableColumnFlags_WidthFixed, 60.0f);
                ImGui::TableSetupColumn("Resident", ImGuiTableColumnFlags_WidthFixed, 56.0f);
                ImGui::TableSetupColumn("##url", ImGuiTableColumnFlags_WidthFixed, 26.0f);
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableHeadersRow();

                for (int i = 0; i < static_cast<int>(tiles.size()); ++i) {
                    const auto& t = tiles[static_cast<size_t>(i)];
                    const bool resident = source && source->has_tile_data(t.id);
                    ImGui::PushID(i);
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    // Full-row selectable (the link button below may overlap it) just to detect hover
                    const std::string zoom_label = std::to_string(t.id.zoom_level);
                    ImGui::Selectable(zoom_label.c_str(), false, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap);
                    if (ImGui::IsItemHovered())
                        hovered_tile = t.id;
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%u", static_cast<unsigned>(t.id.coords.x));
                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%u", static_cast<unsigned>(t.id.coords.y));
                    ImGui::TableSetColumnIndex(3);
                    ImGui::Text("%u", t.pixel_count);

                    ImGui::TableSetColumnIndex(4);
                    if (resident)
                        ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), ICON_FA_CHECK);
                    else
                        ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.4f, 1.0f), ICON_FA_TIMES);

                    ImGui::TableSetColumnIndex(5);
                    if (load_service) {
                        // unpack() yields TMS ids, build_tile_url converts to the source's scheme
                        const std::string url = load_service->build_tile_url(t.id).toStdString();
                        ImGui::TextLinkOpenURL(ICON_FA_LINK, url.c_str());
                        ImGui::SetItemTooltip("%s", url.c_str());
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
