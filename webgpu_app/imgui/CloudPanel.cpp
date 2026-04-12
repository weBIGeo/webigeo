/*****************************************************************************
 * weBIGeo
 * Copyright (C) 2026 Gerald Kimmersdorfer
 * Copyright (C) 2026 Wendelin Muth
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

#include "CloudPanel.h"

#include <IconsFontAwesome5.h>
#include <imgui.h>

#include "../CloudsManager.h"
#include "webgpu_engine/CloudRenderer.h"

namespace webgpu_app {

CloudPanel::CloudPanel(clouds::Manager* clouds_manager, webgpu_engine::CloudRenderer* cloud_renderer)
    : m_clouds_manager(clouds_manager)
    , m_cloud_renderer(cloud_renderer)
{
}

void CloudPanel::draw_panel()
{
    const auto& times = m_clouds_manager->get_slots();
    auto manifest_status = m_clouds_manager->get_manifest_status();
    auto selected_slot = m_clouds_manager->selected_time_slot();

    if (ImGui::CollapsingHeader(ICON_FA_CLOUD "  Clouds")) {

        ImGui::SeparatorText("Data");

        if (times.empty()) {
            if (manifest_status == clouds::ManifestStatus::Pending) {
                ImGui::Text("Loading cloud data...");
            } else {
                if (manifest_status == clouds::ManifestStatus::Error) {
                    ImGui::Text("Failed to fetch cloud data.");
                } else {
                    ImGui::Text("No cloud data available.");
                }
                ImGui::SameLine();
                if (ImGui::Button(ICON_FA_SYNC "##reload_clouds")) {
                    m_clouds_manager->refresh_manifest();
                }
            }
        } else {
            std::string preview_str = "Select time";
            if (!selected_slot.id.isEmpty()) {
                preview_str = selected_slot.format_string() + " (" + clouds::to_string(selected_slot.status) + ")";
            }
            if (ImGui::BeginCombo("(UTC)", preview_str.c_str())) {
                for (int n = 0; n < (int)times.size(); n++) {
                    auto slot = times[n];
                    ImGui::PushID(slot.id.toStdString().c_str());
                    const bool is_selected = slot.id == selected_slot.id;
                    std::string label = slot.format_string() + " (" + clouds::to_string(slot.status) + ")";
                    if (ImGui::Selectable(label.c_str(), is_selected)) {
                        m_clouds_manager->select_time_slot(times[n]);
                    }
                    if (is_selected)
                        ImGui::SetItemDefaultFocus();
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }

            ImGui::SameLine();
            if (manifest_status == clouds::ManifestStatus::Pending) ImGui::BeginDisabled();
            if (ImGui::Button(ICON_FA_SYNC "##reload_clouds")) {
                m_clouds_manager->refresh_manifest();
            }
            if (manifest_status == clouds::ManifestStatus::Pending) ImGui::EndDisabled();

            bool can_generate = selected_slot.is_generation_requestable();
            if (!can_generate) ImGui::BeginDisabled();
            if (ImGui::Button("Generate")) {
                m_clouds_manager->generate_selected_slot();
            }
            if (!can_generate) ImGui::EndDisabled();
            if (selected_slot.status == clouds::SlotStatus::Pending) {
                ImGui::SameLine();
                ImGui::Text("Progress: %s %d%%", selected_slot.progress.stage.toStdString().c_str(), selected_slot.progress.percent);
            }
        }

        ImGui::SeparatorText("Shading");
        auto& shader_params = m_cloud_renderer->shader_params;
        ImGui::Text("Step Size");
        ImGui::Indent();
        ImGui::DragFloat("Minimum", &shader_params.step_size_min, 1.0f, 0.0f, 10000.0f);
        float inv_dist_fact = 1.0f / shader_params.step_size_distance_factor;
        if (ImGui::DragFloat("Distance Factor", &inv_dist_fact, 1.0f, 0.0f, 10000.0f)) {
            shader_params.step_size_distance_factor = 1.0f / inv_dist_fact;
        }
        ImGui::DragFloat("Horizon Factor", &shader_params.step_size_horizon_factor, 1.0f, 0.0f, 10000.0f);
        ImGui::Unindent();
        ImGui::Text("Scattering");
        ImGui::Indent();
        ImGui::SliderFloat("Scattering Coeff", &shader_params.scattering_coeff, -1.0f, 1.0f);
        ImGui::SliderFloat("Extinction Coeff", &shader_params.extinction_coeff, 0.0f, 1.0f, "%.5f");
        ImGui::SliderFloat("Albedo", &shader_params.albedo, 0.0f, 1.0f);
        ImGui::Unindent();
        ImGui::Text("Lighting");
        ImGui::Indent();
        ImGui::DragFloat("Sun Light Scale", &shader_params.sun_light_scale, 1.0f, 0.0f, 10000.0f);
        ImGui::DragFloat("Ambient Light Scale", &shader_params.ambient_light_scale, 0.01f, 0.0f, 10000.0f);
        ImGui::DragFloat("Atmospheric Light Scale", &shader_params.atmospheric_light_scale, 0.01f, 0.0f, 10000.0f);
        ImGui::DragFloat("Shadow Extinction Scale", &shader_params.shadow_extinction_scale, 0.01f, 0.0f, 10000.0f);
        ImGui::SliderFloat("Powder Effect Scale", &shader_params.powder_scale, 0.0f, 1.0f);
        ImGui::Unindent();
        ImGui::Text("Visibility");
        ImGui::Indent();
        ImGui::SliderFloat("Fade", &shader_params.fade_factor, 0.001f, 1.0f);
        ImGui::Unindent();
        ImGui::Text("Accumulation");
        ImGui::Indent();
        ImGui::SliderInt("Stable Frames Limit", &shader_params.stable_frames_limit, 1, 256);
        ImGui::Unindent();
    }
}

} // namespace webgpu_app
