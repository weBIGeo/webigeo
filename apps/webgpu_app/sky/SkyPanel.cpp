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

#include "SkyPanel.h"

#include "ImGuiManager.h"
#include <IconsFontAwesome5.h>
#include <glm/gtc/type_ptr.hpp>
#include <glm/trigonometric.hpp>
#include <imgui.h>

#include <webgpu/engine/Context.h>
#include <webgpu/engine/sky/SkyRenderer.h>

namespace webgpu_app {

SkyPanel::SkyPanel(webgpu_engine::Context* context, webgpu_engine::sky::SkyRenderer* sky_renderer)
    : m_context(context)
    , m_sky_renderer(sky_renderer)
{
}

void SkyPanel::draw()
{
    auto& cfg = m_context->shared_config();
    // Toggles between the legacy gradient atmosphere (0) and the LUT-based sky (1) for perf comparison.
    if (ImGuiManager::FloatingToggleButton("ToggleLutSkyButton", ICON_FA_SUN, "LUT Sky", &cfg.m_sky_mode))
        m_context->request_redraw();
}

void SkyPanel::draw_panel()
{
    if (!ImGui::CollapsingHeader(ICON_FA_SUN "  Sky (LUT)"))
        return;

    auto& cfg = m_context->shared_config();

    int mode = int(cfg.m_sky_mode);
    if (ImGui::Combo("Renderer", &mode, "Legacy gradient\0LUT sky\0\0")) {
        cfg.m_sky_mode = uint32_t(mode);
        m_context->request_redraw();
    }

    if (cfg.m_sky_mode != 1u) {
        ImGui::TextDisabled("Select 'LUT sky' to edit atmosphere parameters.");
        return;
    }

    auto& atm = m_sky_renderer->atmosphere();
    auto& uni = m_sky_renderer->uniforms();
    bool atmosphere_changed = false; // requires constant-LUT re-render
    bool redraw = false;

    ImGui::SeparatorText("Planet (world scale)");
    if (ImGui::DragFloat("Bottom radius (km)", &atm.bottomRadius, 100.0f, 1.0f, 1.0e7f, "%.1f")) {
        cfg.m_planet_radius_m = atm.bottomRadius * 1000.0f; // keep terrain curvature in sync
        atmosphere_changed = true;
    }
    atmosphere_changed |= ImGui::DragFloat("Atmosphere height (km)", &atm.height, 1.0f, 0.1f, 2000.0f, "%.2f");
    redraw |= ImGui::DragFloat3("Center (km)", glm::value_ptr(atm.center), 10.0f); // per-frame only, no LUT re-render

    ImGui::SeparatorText("Rayleigh");
    atmosphere_changed |= ImGui::DragFloat3("Rayleigh scattering", glm::value_ptr(atm.rayleigh.scattering), 0.001f, 0.0f, 2.0f, "%.4f");
    atmosphere_changed |= ImGui::DragFloat("Rayleigh density scale", &atm.rayleigh.densityExpScale, 0.01f, -5.0f, 0.0f, "%.3f");

    ImGui::SeparatorText("Mie");
    atmosphere_changed |= ImGui::DragFloat3("Mie scattering", glm::value_ptr(atm.mie.scattering), 0.001f, 0.0f, 2.0f, "%.4f");
    atmosphere_changed |= ImGui::DragFloat3("Mie extinction", glm::value_ptr(atm.mie.extinction), 0.001f, 0.0f, 2.0f, "%.4f");
    atmosphere_changed |= ImGui::DragFloat("Mie density scale", &atm.mie.densityExpScale, 0.01f, -5.0f, 0.0f, "%.3f");

    ImGui::SeparatorText("Ozone / Absorption");
    atmosphere_changed |= ImGui::DragFloat3("Ozone extinction", glm::value_ptr(atm.absorption.extinction), 0.001f, 0.0f, 2.0f, "%.4f");

    ImGui::SeparatorText("Ground / Scattering");
    atmosphere_changed |= ImGui::ColorEdit3("Ground albedo", glm::value_ptr(atm.groundAlbedo));
    atmosphere_changed |= ImGui::SliderFloat("Multiple scattering", &atm.multipleScatteringFactor, 0.0f, 2.0f);

    ImGui::SeparatorText("Sun");
    float disk_deg = glm::degrees(uni.sun.diskAngularDiameter);
    if (ImGui::SliderFloat("Sun disk diameter (deg)", &disk_deg, 0.1f, 20.0f)) {
        uni.sun.diskAngularDiameter = glm::radians(disk_deg);
        redraw = true;
    }
    redraw |= ImGui::SliderFloat("Sun disk luminance", &uni.sun.diskLuminanceScale, 0.1f, 100.0f);
    redraw |= ImGui::DragFloat3("Sun illuminance", glm::value_ptr(uni.sun.illuminance), 0.01f, 0.0f, 20.0f);

    if (atmosphere_changed)
        m_sky_renderer->mark_atmosphere_dirty();
    if (atmosphere_changed || redraw)
        m_context->request_redraw();
}

} // namespace webgpu_app
