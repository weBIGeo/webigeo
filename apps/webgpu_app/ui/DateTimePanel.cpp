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

#include "DateTimePanel.h"

#include "App.h"
#include "ImGuiManager.h"
#include <IconsFontAwesome5.h>
#include <QDateTime>
#include <algorithm>
#include <imgui.h>
#include <nucleus/camera/Controller.h>
#include <nucleus/srs.h>
#include <nucleus/utils/sun_calculations.h>
#include <webgpu/engine/Context.h>

namespace webgpu_app {

DateTimePanel::DateTimePanel(App* terrain_renderer, webgpu_engine::Context* context)
    : m_terrain_renderer(terrain_renderer)
    , m_context(context)
{
}

void DateTimePanel::draw()
{
    if (m_manager->is_window_open())
        return;

    const float panel_w = 320.0f, panel_h = 185.0f, margin = 10.0f;
    ImVec2 avail = m_manager->get_window_size();
    ImGui::SetNextWindowPos(ImVec2(avail.x - panel_w - margin, avail.y - panel_h - margin), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(panel_w, panel_h), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.85f);

    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoBringToFrontOnFocus;

    if (ImGui::Begin("##datetime_panel", nullptr, flags)) {
        ImGui::SeparatorText(ICON_FA_CLOCK "  Date & Time (Local)");

        bool changed = false;

        ImGui::SetNextItemWidth(70);
        changed |= ImGui::InputInt("##year", &m_year, 0, 0);
        m_year = std::clamp(m_year, 2000, 2100);

        ImGui::SameLine();
        constexpr const char* months[]
            = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
        ImGui::SetNextItemWidth(60);
        changed |= ImGui::Combo("##month", &m_month, months, 12);

        ImGui::SameLine();
        ImGui::SetNextItemWidth(50);
        changed |= ImGui::InputInt("##day", &m_day, 0, 0);
        m_day = std::clamp(m_day, 1, 31);

        int total_minutes = m_hour * 60 + m_minute;
        char time_label[8];
        std::snprintf(time_label, sizeof(time_label), "%02d:%02d", m_hour, m_minute);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderInt("##time", &total_minutes, 0, 1439, time_label)) {
            m_hour = total_minutes / 60;
            m_minute = total_minutes % 60;
            changed = true;
        }

        auto world_pos = m_terrain_renderer->get_camera_controller()->definition().position();
        auto lla = nucleus::srs::world_to_lat_long_alt(world_pos);
        ImGui::Separator();
        ImGui::TextDisabled("Location: %.4f° N  %.4f° E  %.0f m", lla.x, lla.y, lla.z);

        if (changed)
            recalculate_and_apply();
    }
    ImGui::End();
}

void DateTimePanel::recalculate_and_apply()
{
    QDateTime dt(QDate(m_year, m_month + 1, m_day), QTime(m_hour, m_minute, 0), Qt::LocalTime);

    auto world_pos = m_terrain_renderer->get_camera_controller()->definition().position();
    auto lla = nucleus::srs::world_to_lat_long_alt(world_pos);

    glm::vec2 angles = nucleus::utils::sun_calculations::calculate_sun_angles(dt, lla);
    glm::vec3 dir = nucleus::utils::sun_calculations::sun_rays_direction_from_sun_angles(angles);

    auto& cfg = m_context->shared_config();
    cfg.m_sun_light_dir = glm::vec4(dir, cfg.m_sun_light_dir.w);
    m_context->request_redraw();
}

} // namespace webgpu_app
