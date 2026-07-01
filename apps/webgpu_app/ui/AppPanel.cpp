/*****************************************************************************
 * weBIGeo
 * Copyright (C) 2024 Gerald Kimmersdorfer
 * Copyright (C) 2025 Patrick Komon
 * Copyright (C) 2025 Markus Rampp
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

#include "AppPanel.h"

#include <IconsFontAwesome5.h>
#include <imgui.h>

#include "App.h"
#include "RenderingContext.h"
#include <QDebug>

namespace webgpu_app {

AppPanel::AppPanel(App* terrain_renderer)
    : m_terrain_renderer(terrain_renderer)
{
}

void AppPanel::ready()
{
    m_terrain_renderer->get_webgpu_window()->set_max_zoom_level(m_max_zoom_level);
    m_terrain_renderer->get_camera_controller()->update();
}

void AppPanel::draw_panel()
{
    if (ImGui::CollapsingHeader(ICON_FA_COG "  App Settings")) {
        m_terrain_renderer->render_gui();
        static float render_quality = 0.5f;
        if (ImGui::SliderFloat("Level of Detail", &render_quality, 0.1f, 2.0f)) {
            const auto permissible_error = 1.0f / render_quality;
            m_terrain_renderer->get_camera_controller()->set_pixel_error_threshold(permissible_error);
            m_terrain_renderer->update_camera();
            qDebug() << "Setting permissible error to " << permissible_error;
        }

        const uint32_t min_max_zoom_lvl = 1;
        const uint32_t max_max_zoom_lvl = 18;
        if (ImGui::SliderScalar("Max zoom level", ImGuiDataType_U32, &m_max_zoom_level, &min_max_zoom_lvl, &max_max_zoom_lvl, "%u")) {
            m_terrain_renderer->get_webgpu_window()->set_max_zoom_level(m_max_zoom_level);
            m_terrain_renderer->get_camera_controller()->update();
        }

        static int geometry_tile_source_index = 0; // 0 ... DSM, 1 ... DTM
        if (ImGui::Combo("Geometry Tiles", &geometry_tile_source_index, "AlpineMaps DSM\0AlpineMaps DTM\0")) {
            auto geometry_load_service = m_terrain_renderer->get_rendering_context()->geometry_tile_load_service();
            if (geometry_tile_source_index == 0) {
                geometry_load_service->set_base_url("https://alpinemaps.cg.tuwien.ac.at/tiles/alpine_png/");
            } else if (geometry_tile_source_index == 1) {
                geometry_load_service->set_base_url("https://alpinemaps.cg.tuwien.ac.at/tiles/at_dtm_alpinemaps/");
            }
            m_terrain_renderer->get_rendering_context()->geometry_scheduler()->clear_full_cache();
            m_terrain_renderer->get_camera_controller()->update();
        }

    }
}

} // namespace webgpu_app
