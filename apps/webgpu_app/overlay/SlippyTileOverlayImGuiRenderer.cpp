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

#include <algorithm>
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
        s.max_zoom = std::min(s.max_zoom, preset.max_possible_zoom);
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

    return changed;
}

} // namespace webgpu_app
