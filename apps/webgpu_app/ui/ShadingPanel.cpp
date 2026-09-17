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

#include "ShadingPanel.h"

#include "ImGuiManager.h"
#include <IconsFontAwesome5.h>
#include <glm/glm.hpp>
#include <imgui.h>

#include <webgpu/engine/Context.h>

namespace webgpu_app {

ShadingPanel::ShadingPanel(webgpu_engine::Context* context)
    : m_context(context)
{
}

void ShadingPanel::draw()
{
    auto& cfg = m_context->shared_config();
    if (ImGuiManager::FloatingToggleButton("ToggleShadingButton", ICON_FA_SUN, "Shading", &cfg.m_shading_enabled))
        m_context->request_redraw();
}

void ShadingPanel::draw_panel()
{
    const bool enabled = m_context->shared_config().m_shading_enabled;

    if (!enabled) {
        ImGui::SetNextItemOpen(false);
        ImGui::BeginDisabled();
    }
    bool header_open = ImGui::CollapsingHeader(ICON_FA_SUN "  Shading");
    if (!enabled)
        ImGui::EndDisabled();

    if (!header_open)
        return;

    auto& cfg = m_context->shared_config();

    bool changed = ImGui::Combo("Normal Mode", (int*)&cfg.m_normal_mode, "None\0Flat\0Smooth\0\0");
    ImGui::Separator();
    changed |= ImGui::ColorEdit4("Material Color", (float*)&cfg.m_material_color);
    ImGui::Separator();
    changed |= ImGui::SliderFloat("Ambient Strength", &cfg.m_material_light_response.x, 0.0f, 5.0f);
    changed |= ImGui::SliderFloat("Diffuse Strength", &cfg.m_material_light_response.y, 0.0f, 5.0f);
    changed |= ImGui::SliderFloat("Specular Strength", &cfg.m_material_light_response.z, 0.0f, 5.0f);
    changed |= ImGui::SliderFloat("Shininess", &cfg.m_material_light_response.w, 1.0f, 256.0f);

    if (changed) {
        m_context->request_redraw();
    }
}

} // namespace webgpu_app
