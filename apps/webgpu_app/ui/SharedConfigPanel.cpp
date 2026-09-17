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

#include "SharedConfigPanel.h"

#include <IconsFontAwesome5.h>
#include <glm/glm.hpp>
#include <imgui.h>

#include <webgpu/engine/Context.h>

namespace webgpu_app {

SharedConfigPanel::SharedConfigPanel(webgpu_engine::Context* context)
    : m_context(context)
{
}

void SharedConfigPanel::draw_panel()
{
    if (!ImGui::CollapsingHeader(ICON_FA_SLIDERS_H "  Shared Config"))
        return;

    auto& cfg = m_context->shared_config();
    bool changed = false;

    ImGui::SeparatorText(ICON_FA_GLOBE " Planet");
    float radius_km = cfg.m_planet_radius_m / 1000.0f;
    if (ImGui::DragFloat("Radius (km)", &radius_km, 100.0f, 1.0f, 1.0e7f, "%.1f")) {
        cfg.m_planet_radius_m = radius_km * 1000.0f;
        changed = true;
    }
    float atmosphere_height_km = cfg.m_atmosphere_height_m / 1000.0f;
    if (ImGui::DragFloat("Atmosphere height (km)", &atmosphere_height_km, 1.0f, 0.1f, 2000.0f, "%.2f")) {
        cfg.m_atmosphere_height_m = atmosphere_height_km * 1000.0f;
        changed = true;
    }

    ImGui::SeparatorText(ICON_FA_SUN " Sun");
    changed |= ImGui::ColorEdit3("Light Color", (float*)&cfg.m_sun_light);
    changed |= ImGui::SliderFloat("Light Intensity", &cfg.m_sun_light.w, 0.0f, 10.0f);
    bool sun_dir_changed = ImGui::DragFloat3("Light Direction", (float*)&cfg.m_sun_light_dir, 0.01f, -1.0f, 1.0f);
    changed |= ImGui::ColorEdit3("Ambient Color", (float*)&cfg.m_amb_light);
    changed |= ImGui::SliderFloat("Ambient Intensity", &cfg.m_amb_light.w, 0.0f, 10.0f);
    changed |= sun_dir_changed;

    if (changed) {
        cfg.m_sun_light_dir = glm::normalize(cfg.m_sun_light_dir);
        m_context->request_redraw();
        if (sun_dir_changed)
            emit sun_dir_manually_changed();
    }
}

} // namespace webgpu_app
