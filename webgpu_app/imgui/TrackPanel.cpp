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

#include "TrackPanel.h"

#ifdef __EMSCRIPTEN__
#include <webgpu_app/WebInterop.h>
#else
#include <ImGuiFileDialog.h>
#include <filesystem>
#endif

#include <imgui.h>

#include "webgpu_engine/Context.h"
#include "webgpu_engine/renderer/TrackRenderer.h"

namespace webgpu_app {

TrackPanel::TrackPanel(webgpu_engine::Context* context)
    : m_context(context)
    , m_track_renderer(context->track_renderer())
{
}

void TrackPanel::draw_panel()
{
    if (ImGui::CollapsingHeader("Track", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Button("Open GPX file ...", ImVec2(250, 0))) {
#ifdef __EMSCRIPTEN__
            WebInterop::instance().open_file_dialog(".gpx", "track");
#else
            IGFD::FileDialogConfig config;
            config.path = m_last_dialog_directory;
            ImGuiFileDialog::Instance()->OpenDialog("ChooseFileDlgKey", "Choose File", ".gpx,.*", config);
#endif
        }
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(106 / 255.0f, 112 / 255.0f, 115 / 255.0f, 1.00f));
        if (ImGui::Button("Open Preset ...", ImVec2(100, 0))) {
            m_track_renderer->load_track(webgpu_engine::TrackRenderer::DEFAULT_GPX_TRACK_PATH);
        }
        ImGui::PopStyleColor(1);

        const char* items = "none\0without depth test\0with depth test\0semi-transparent\0";
        if (ImGui::Combo("Line render mode", (int*)&(m_context->shared_config().m_track_render_mode), items)) {
            m_context->request_redraw();
        }
    }

#ifndef __EMSCRIPTEN__
    if (ImGuiFileDialog::Instance()->Display("ChooseFileDlgKey")) {
        if (ImGuiFileDialog::Instance()->IsOk()) {
            std::string file_path = ImGuiFileDialog::Instance()->GetFilePathName();
            m_last_dialog_directory = std::filesystem::path(file_path).parent_path().string();
            m_track_renderer->load_track(file_path);
        }
        ImGuiFileDialog::Instance()->Close();
    }
#endif
}

} // namespace webgpu_app
