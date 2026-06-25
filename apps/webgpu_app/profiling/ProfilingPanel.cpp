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

#include "ProfilingPanel.h"

#include <IconsFontAwesome5.h>
#include <imgui.h>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "../util/format_time.h"
#include "App.h"
#include "ProfilingStore.h"

namespace webgpu_app {

ProfilingPanel::ProfilingPanel(App* app)
    : m_app(app)
{
}

void ProfilingPanel::draw_panel()
{
    if (!ImGui::CollapsingHeader(ICON_FA_STOPWATCH "  Profiling"))
        return;

    const auto& data = m_app->get_profiling_store()->data();

    // Group entries by group name, sorted alphabetically for stable order.
    std::map<std::string, std::vector<const TimingSeries*>> groups;
    for (const auto& [hash, series] : data) {
        const std::string group = series.group ? series.group : "";
        groups[group].push_back(&series);
    }

    static constexpr uint64_t STALE_THRESHOLD = 10;
    static constexpr float COL_TIME_W = 72.0f;
    static constexpr float COL_BAR_W = 130.0f;

    for (const auto& [group_name, entries] : groups) {
        uint64_t max_frame = 0;
        for (const auto* s : entries)
            if (s->last_frame > max_frame)
                max_frame = s->last_frame;

        float group_sum = 0.0f;
        for (const auto* s : entries)
            if (s->last_frame + STALE_THRESHOLD >= max_frame)
                group_sum += average(*s);

        const bool has_group = !group_name.empty();
        const bool open = !has_group || ImGui::TreeNodeEx(group_name.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
        if (!open)
            continue;

        if (ImGui::BeginTable(group_name.c_str(), 3, ImGuiTableFlags_None)) {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, COL_TIME_W);
            ImGui::TableSetupColumn("Share", ImGuiTableColumnFlags_WidthFixed, COL_BAR_W);

            for (const auto* series : entries) {
                const bool stale = series->last_frame + STALE_THRESHOLD < max_frame;
                const float avg = average(*series);

                ImGui::TableNextRow();

                // Name column
                ImGui::TableSetColumnIndex(0);
                if (stale)
                    ImGui::TextDisabled("%s", series->name ? series->name : "?");
                else
                    ImGui::TextUnformatted(series->name ? series->name : "?");

                // Time column (right-aligned)
                ImGui::TableSetColumnIndex(1);
                const std::string time_str = format_time(avg);
                const float text_w = ImGui::CalcTextSize(time_str.c_str()).x;
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + COL_TIME_W - text_w);
                if (stale)
                    ImGui::TextDisabled("%s", time_str.c_str());
                else
                    ImGui::TextUnformatted(time_str.c_str());

                // Bar column
                ImGui::TableSetColumnIndex(2);
                if (stale) {
                    ImGui::TextDisabled("--");
                } else {
                    const float pct = group_sum > 0.0f ? avg / group_sum : 0.0f;
                    char overlay[16];
                    std::snprintf(overlay, sizeof(overlay), "%.0f%%", pct * 100.0f);
                    ImGui::ProgressBar(pct, ImVec2(COL_BAR_W, 0.0f), overlay);
                }
            }

            ImGui::EndTable();
        }

        if (has_group)
            ImGui::TreePop();
    }

    if (ImGui::Button("Reset All Timers"))
        m_app->get_profiling_store()->reset_all();
}

} // namespace webgpu_app
