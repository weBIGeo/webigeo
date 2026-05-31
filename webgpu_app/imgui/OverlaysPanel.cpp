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

#include "OverlaysPanel.h"

#include "overlays/OverlayImGuiRendererFactory.h"

#include <IconsFontAwesome5.h>
#include <algorithm>
#include <imgui.h>

#include "webgpu_engine/Context.h"
#include "webgpu_engine/renderer/OverlayRenderer.h"
#include "webgpu_engine/renderer/overlays/HeightLinesOverlay.h"
#include "webgpu_engine/renderer/overlays/TextureOverlay.h"

namespace webgpu_app {

OverlaysPanel::OverlaysPanel(webgpu_engine::Context* context)
    : m_context(context)
    , m_overlay_renderer(context ? context->overlay_renderer() : nullptr)
{
    if (m_overlay_renderer)
        rebuild_renderers();
}

void OverlaysPanel::rebuild_renderers()
{
    m_renderers.clear();
    if (!m_overlay_renderer)
        return;
    for (auto& overlay : m_overlay_renderer->overlays())
        m_renderers.push_back(OverlayImGuiRendererFactory::create(*overlay));
    if (m_selected_engine_idx >= static_cast<int>(m_renderers.size()))
        m_selected_engine_idx = -1;
}

// GUI shows overlays in descending z_index order with a virtual "Shading" row at position P.
// display index d ∈ [0, N-1] maps 1-to-1 to the overlay sorted by z_index descending:
//   d=0      → engine idx N-1 (highest z_index, topmost layer)
//   d=P-1    → engine idx N-P = Q (z_index=1, bottom post-shading)
//   [shading divider at gui_row P]
//   d=P      → engine idx Q-1 (z_index=-1, top pre-shading)
//   d=N-1    → engine idx 0   (lowest z_index, bottom pre-shading)
// gui_row = d < P ? d : d+1  (add 1 to skip the shading divider row in the loop)
// d        = gui_row < P ? gui_row : gui_row-1

static int display_to_engine(int d, int N) { return N - 1 - d; }

void OverlaysPanel::do_move(int gui_row, int direction, int P, int N)
{
    // GUI has N+1 slots (0..N): the N overlays plus the Shading divider at slot P.
    // Moving = swapping two adjacent slots. If the neighbour is the divider, the
    // overlay simply crosses into the other bucket (its z_index sign flips).
    const int other = gui_row + direction;
    if (other < 0 || other > N) return;

    // Remember the selected overlay by identity so we can restore selection after the sort.
    std::shared_ptr<webgpu_engine::Overlay> selected_ptr;
    if (m_selected_engine_idx >= 0 && m_selected_engine_idx < N)
        selected_ptr = m_overlay_renderer->overlays()[m_selected_engine_idx];

    // Build the GUI-ordered slot list (descending z_index) with the divider as a nullptr at slot P.
    // NB: "slots" is a Qt macro (Q_SLOTS), so the local must use a different name.
    auto gui_slots = m_overlay_renderer->overlays();  // ascending by z_index
    std::reverse(gui_slots.begin(), gui_slots.end()); // descending: post-shading first, then pre-shading
    gui_slots.insert(gui_slots.begin() + P, nullptr); // divider at slot P → size becomes N+1

    std::swap(gui_slots[gui_row], gui_slots[other]);

    // The divider's new slot is the new boundary; reassign unique z_indices around it.
    int new_P = 0;
    for (int i = 0; i < static_cast<int>(gui_slots.size()); ++i)
        if (!gui_slots[i]) { new_P = i; break; }

    for (int i = 0; i < static_cast<int>(gui_slots.size()); ++i) {
        if (i == new_P) continue;                 // divider slot
        if (i < new_P)
            gui_slots[i]->z_index = new_P - i;    // new_P, new_P-1, ..., 1  (post-shading, top→bottom)
        else
            gui_slots[i]->z_index = -(i - new_P); // -1, -2, ...             (pre-shading, top→bottom)
    }

    m_overlay_renderer->sort_overlays(); // re-sort m_overlays by the updated z_indices

    // Restore selection to the same overlay at its new engine index.
    if (selected_ptr) {
        const auto& ov = m_overlay_renderer->overlays();
        for (int i = 0; i < static_cast<int>(ov.size()); ++i)
            if (ov[i] == selected_ptr) { m_selected_engine_idx = i; break; }
    }

    rebuild_renderers();
    m_context->request_redraw();
}

void OverlaysPanel::draw_panel()
{
    if (!m_overlay_renderer)
        return;

    if (!ImGui::CollapsingHeader(ICON_FA_LAYER_GROUP "  Overlays"))
        return;

    // --- Add controls (top) ---
    const char* add_items[] = { "Height Lines", "Texture Overlay" };
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 36);
    ImGui::Combo("##add_type", &m_add_type_index, add_items, 2);
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_PLUS)) {
        std::shared_ptr<webgpu_engine::Overlay> new_overlay;
        if (m_add_type_index == 0)
            new_overlay = std::make_shared<webgpu_engine::HeightLinesOverlay>();
        else
            new_overlay = std::make_shared<webgpu_engine::TextureOverlay>();
        // add_overlay() auto-assigns the topmost z_index
        m_overlay_renderer->add_overlay(new_overlay);
        rebuild_renderers();
        m_selected_engine_idx = static_cast<int>(m_overlay_renderer->overlays().size()) - 1;
        m_context->request_redraw();
    }

    ImGui::Separator();

    // --- Overlay list ---
    const auto& overlays = m_overlay_renderer->overlays();
    const int N = static_cast<int>(overlays.size());

    int P = 0; // count of post-shading overlays (z_index > 0)
    for (const auto& o : overlays)
        if (o->z_index > 0) ++P;

    const float row_h   = ImGui::GetTextLineHeightWithSpacing() + 6.0f;
    const float btn_w   = row_h;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float btns_w  = btn_w * 3.0f + spacing * 2.0f;

    // Iterate GUI rows: 0..P-1 (post-shading), P (shading divider), P+1..P+Q (pre-shading).
    // There are N+1 GUI rows total (N overlays + the divider), indexed 0..N.
    for (int gui_row = 0; gui_row <= N; ++gui_row) {

        // --- Shading divider ---
        if (gui_row == P) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.78f, 0.78f, 0.82f, 1.0f)); // light gray, more apparent
            const char* label = ICON_FA_SUN "  Shading";
            const float text_w = ImGui::CalcTextSize(label).x;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (ImGui::GetContentRegionAvail().x - text_w) * 0.5f);
            ImGui::TextUnformatted(label);
            ImGui::PopStyleColor();
            continue;
        }

        // Convert gui_row to display index d and engine index
        const int d = (gui_row < P) ? gui_row : gui_row - 1;
        const int engine_idx = display_to_engine(d, N);
        ImGui::PushID(engine_idx);

        const bool selected = (m_selected_engine_idx == engine_idx);
        const auto& name = (engine_idx < static_cast<int>(m_renderers.size()))
            ? m_renderers[engine_idx]->display_name()
            : "Overlay";

        if (selected)
            m_selected_row_screen_y = ImGui::GetCursorScreenPos().y;

        // Selectable with vertically centered text
        ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.0f, 0.5f));
        if (ImGui::Selectable(name.c_str(), selected, ImGuiSelectableFlags_None,
                ImVec2(ImGui::GetContentRegionAvail().x - btns_w - spacing, row_h)))
            m_selected_engine_idx = selected ? -1 : engine_idx;
        ImGui::PopStyleVar();
        ImGui::SameLine();

        // Enable in GUI-slot space (N+1 slots, 0..N): an overlay can move toward any
        // adjacent slot, including the divider — that's how it crosses buckets.
        const bool can_up   = (gui_row > 0);
        const bool can_down = (gui_row < N);

        if (!can_up) ImGui::BeginDisabled();
        if (ImGui::Button(ICON_FA_ARROW_UP, ImVec2(btn_w, row_h))) {
            do_move(gui_row, -1, P, N);
            ImGui::PopID();
            if (!can_up) ImGui::EndDisabled();
            break;
        }
        if (!can_up) ImGui::EndDisabled();
        ImGui::SameLine();

        if (!can_down) ImGui::BeginDisabled();
        if (ImGui::Button(ICON_FA_ARROW_DOWN, ImVec2(btn_w, row_h))) {
            do_move(gui_row, +1, P, N);
            ImGui::PopID();
            if (!can_down) ImGui::EndDisabled();
            break;
        }
        if (!can_down) ImGui::EndDisabled();
        ImGui::SameLine();

        // Delete button (red, trash icon)
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.6f, 0.1f, 0.1f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
        const bool deleted = ImGui::Button(ICON_FA_TRASH, ImVec2(btn_w, row_h));
        ImGui::PopStyleColor(2);
        if (deleted) {
            if (m_selected_engine_idx == engine_idx) m_selected_engine_idx = -1;
            m_overlay_renderer->remove_overlay(static_cast<size_t>(engine_idx));
            rebuild_renderers();
            m_context->request_redraw();
            ImGui::PopID();
            break;
        }

        ImGui::PopID();
    }
}

void OverlaysPanel::draw()
{
    if (!m_overlay_renderer || m_selected_engine_idx < 0
        || m_selected_engine_idx >= static_cast<int>(m_renderers.size()))
        return;

    auto& renderer = m_renderers[m_selected_engine_idx];

    const float popup_w   = 360.0f;
    const float sidebar_x = ImGui::GetIO().DisplaySize.x - 430.0f;
    ImGui::SetNextWindowPos(ImVec2(sidebar_x - popup_w - 8.0f, m_selected_row_screen_y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(popup_w, 0.0f));
    ImGui::Begin("##overlay_settings", nullptr,
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar
        | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize
        | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing);

    ImGui::TextDisabled("%s Settings", renderer->display_name().c_str());
    ImGui::Separator();

    if (renderer->render_settings())
        m_context->request_redraw();

    ImGui::End();
}

} // namespace webgpu_app
