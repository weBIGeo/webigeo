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
#include <map>
#include <string>
#include <vector>

#include "App.h"
#include "ProfilingStore.h"
#include "../util/format_time.h"

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

    for (const auto& [group_name, entries] : groups) {
        const bool has_group = !group_name.empty();
        const bool open = !has_group || ImGui::TreeNodeEx(group_name.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
        if (open) {
            for (const auto* series : entries) {
                const float avg = average(*series);
                const float sd = stddev(*series);
                ImGui::Text("%s: %s \xc2\xb1%s [%zu]",
                    series->name ? series->name : "?",
                    format_time(avg).c_str(),
                    format_time(sd).c_str(),
                    series->count);
            }
            if (has_group)
                ImGui::TreePop();
        }
    }

    if (ImGui::Button("Reset All Timers"))
        m_app->get_profiling_store()->reset_all();
}

} // namespace webgpu_app
