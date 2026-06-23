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
#include "cloud/CloudsManager.h"
#include <IconsFontAwesome5.h>
#include <ImGuiDatePicker.hpp>
#include <QDateTime>
#include <algorithm>
#include <climits>
#include <ctime>
#include <imgui.h>
#include <nucleus/camera/Controller.h>
#include <nucleus/srs.h>
#include <nucleus/utils/sun_calculations.h>
#include <webgpu/engine/Context.h>

namespace webgpu_app {

DateTimePanel::DateTimePanel(App* terrain_renderer, webgpu_engine::Context* context, clouds::Manager* clouds_manager)
    : m_terrain_renderer(terrain_renderer)
    , m_context(context)
    , m_clouds_manager(clouds_manager)
{
}

void DateTimePanel::ready()
{
    QDateTime now = QDateTime::currentDateTime();
    m_date.tm_year = now.date().year() - 1900;
    m_date.tm_mon  = now.date().month() - 1;
    m_date.tm_mday = now.date().day();
    m_hour         = now.time().hour();
    m_minute       = now.time().minute();

    connect(m_clouds_manager, &clouds::Manager::tileset_list_ready, this, [this]() {
        recalculate_and_apply(true);
    });

    recalculate_and_apply(true);
}

void DateTimePanel::draw()
{
    if (m_manager->is_window_open())
        return;

    const float panel_w = 350.0f, margin = 10.0f;
    ImVec2 avail = m_manager->get_window_size();
    ImGui::SetNextWindowPos(ImVec2(avail.x - margin, avail.y - margin), ImGuiCond_Always, ImVec2(1.0f, 1.0f));
    ImGui::SetNextWindowSize(ImVec2(panel_w, 0), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.85f);

    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoBringToFrontOnFocus
        | ImGuiWindowFlags_AlwaysAutoResize;

    if (ImGui::Begin("##datetime_panel", nullptr, flags)) {
        const float btn_w = 30.0f;
        const float spacing = ImGui::GetStyle().ItemSpacing.x;

        ImGui::AlignTextToFramePadding();
        ImGui::Text(ICON_FA_CLOCK "  Date & Time (Local)");
        ImGui::SameLine();
        ImGui::SetCursorPosX(ImGui::GetContentRegionMax().x - 2.0f * btn_w - spacing);

        ImGui::PushFont(ImGuiManager::s_node_font);

        ImVec4 sun_btn_col = m_sun_linked ? ImVec4(0.2f, 0.8f, 0.2f, 1.0f) : ImVec4(0.4f, 0.4f, 0.4f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(sun_btn_col.x * 0.6f, sun_btn_col.y * 0.6f, sun_btn_col.z * 0.6f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, sun_btn_col);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  sun_btn_col);
        if (ImGui::Button(ICON_FA_SUN "##sun_link", ImVec2(btn_w, ImGui::GetFrameHeight()))) {
            m_sun_linked = !m_sun_linked;
            if (m_sun_linked)
                recalculate_and_apply(true);
        }
        ImGui::PopStyleColor(3);
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::Text(m_sun_linked ? "Sun direction linked" : "Sun direction unlinked");
            ImGui::EndTooltip();
        }

        ImGui::SameLine();

        // Cloud link button (color by match quality when active)
        ImVec4 cloud_btn_col;
        if (!m_cloud_linked) {
            cloud_btn_col = ImVec4(0.4f, 0.4f, 0.4f, 1.0f);
        } else {
            switch (m_cloud_link_state) {
            case CloudLinkState::Unavailable: cloud_btn_col = ImVec4(0.4f, 0.4f, 0.4f, 1.0f); break;
            case CloudLinkState::Green:       cloud_btn_col = ImVec4(0.2f, 0.8f, 0.2f, 1.0f); break;
            case CloudLinkState::Yellow:      cloud_btn_col = ImVec4(0.9f, 0.8f, 0.1f, 1.0f); break;
            case CloudLinkState::Red:         cloud_btn_col = ImVec4(0.9f, 0.2f, 0.2f, 1.0f); break;
            }
        }
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(cloud_btn_col.x * 0.6f, cloud_btn_col.y * 0.6f, cloud_btn_col.z * 0.6f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, cloud_btn_col);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  cloud_btn_col);
        ImGui::BeginDisabled(m_cloud_link_state == CloudLinkState::Unavailable);
        if (ImGui::Button(ICON_FA_CLOUD "##cloud_link", ImVec2(btn_w, ImGui::GetFrameHeight()))) {
            m_cloud_linked = !m_cloud_linked;
            if (m_cloud_linked)
                recalculate_and_apply(true);
        }
        ImGui::EndDisabled();
        ImGui::PopStyleColor(3);
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            if (m_cloud_link_state == CloudLinkState::Unavailable) {
                ImGui::Text("Cloud data not yet loaded");
            } else if (!m_cloud_linked) {
                ImGui::Text("Cloud coverage unlinked");
            } else if (m_cloud_tileset_local_hour < 0) {
                ImGui::Text("No dataset available");
            } else {
                ImGui::Text("Closest dataset from %02d:00 (local)", m_cloud_tileset_local_hour);
            }
            ImGui::EndTooltip();
        }

        ImGui::PopFont();

        ImGui::Separator();

        ImGui::SetNextItemWidth(110.0f);
        bool date_changed = ImGui::DatePicker("##date", m_date, false, 130.0f, "%d.%m.%Y");
        if (date_changed)
            recalculate_and_apply(true);

        ImGui::SameLine();

        int total_minutes = m_hour * 60 + m_minute;
        char time_label[8];
        std::snprintf(time_label, sizeof(time_label), "%02d:%02d", m_hour, m_minute);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        if (ImGui::SliderInt("##time", &total_minutes, 0, 1439, time_label)) {
            m_hour   = total_minutes / 60;
            m_minute = total_minutes % 60;
            recalculate_and_apply(false);
        }
        if (ImGui::IsItemDeactivatedAfterEdit())
            recalculate_and_apply(true);

        auto world_pos = m_terrain_renderer->get_camera_controller()->definition().position();
        auto lla = nucleus::srs::world_to_lat_long_alt(world_pos);
        ImGui::Separator();
        ImGui::TextDisabled("Location: %.4f° N  %.4f° E  %.0f m", lla.x, lla.y, lla.z);
    }
    ImGui::End();
}

void DateTimePanel::recalculate_and_apply(bool load_cloud)
{
    int year  = m_date.tm_year + 1900;
    int month = m_date.tm_mon + 1;   // 1-based for QDate
    int day   = m_date.tm_mday;

    QDateTime local_dt(QDate(year, month, day), QTime(m_hour, m_minute, 0), Qt::LocalTime);

    if (m_sun_linked) {
        auto world_pos = m_terrain_renderer->get_camera_controller()->definition().position();
        auto lla = nucleus::srs::world_to_lat_long_alt(world_pos);
        glm::vec2 angles = nucleus::utils::sun_calculations::calculate_sun_angles(local_dt, lla);
        glm::vec3 dir    = nucleus::utils::sun_calculations::sun_rays_direction_from_sun_angles(angles);
        auto& cfg = m_context->shared_config();
        cfg.m_sun_light_dir = glm::vec4(dir, cfg.m_sun_light_dir.w);
        m_context->request_redraw();
    }

    if (m_clouds_manager->is_loading()) {
        m_cloud_link_state = CloudLinkState::Unavailable;
        m_cloud_tileset_local_hour = -1;
        return;
    }

    // always update cloud state indicator such that button color is correct while dragging
    QDateTime utc_dt = local_dt.toUTC();
    const auto* best = m_clouds_manager->find_best_tileset(
        utc_dt.date().year(), utc_dt.date().month(), utc_dt.date().day(),
        utc_dt.time().hour(), utc_dt.time().minute());

    if (!best) {
        m_cloud_link_state = CloudLinkState::Red;
        m_cloud_tileset_local_hour = -1;
    } else {
        int diff = std::abs(best->date.hour * 60 - (utc_dt.time().hour() * 60 + utc_dt.time().minute()));
        m_cloud_link_state = (diff <= 60) ? CloudLinkState::Green : CloudLinkState::Yellow;
        QDateTime tileset_utc(QDate(best->date.year, best->date.month, best->date.day),
                              QTime(best->date.hour, 0, 0), Qt::UTC);
        m_cloud_tileset_local_hour = tileset_utc.toLocalTime().time().hour();
        if (load_cloud && m_cloud_linked)
            m_clouds_manager->select_time_slot(*best);
    }
}

void DateTimePanel::disable_sun_link()   { m_sun_linked   = false; }
void DateTimePanel::disable_cloud_link() { m_cloud_linked = false; }

} // namespace webgpu_app
