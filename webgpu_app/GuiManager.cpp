/*****************************************************************************
 * weBIGeo
 * Copyright (C) 2024 Gerald Kimmersdorfer
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

#include "GuiManager.h"
#include "TerrainRenderer.h"

#ifdef ALP_WEBGPU_APP_ENABLE_IMGUI
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_wgpu.h"
#include <IconsFontAwesome5.h>
#include <imgui.h>
#include <imnodes.h>
#endif
#include "util/url_tools.h"
#include "webgpu_engine/Window.h"
#include <QDebug>
#include <QFile>
#include <nucleus/camera/PositionStorage.h>

namespace webgpu_app {

GuiManager::GuiManager(TerrainRenderer* terrain_renderer)
    : m_terrain_renderer(terrain_renderer)
{
    // Lets build a vector of std::string...
    const auto position_storage = nucleus::camera::PositionStorage::instance();
    const QList<QString> position_storage_list = position_storage->getPositionList();
    for (const auto& position : position_storage_list) {
        m_camera_preset_names.push_back(position.toStdString());
    }
}

void GuiManager::init(
    SDL_Window* window, WGPUDevice device, [[maybe_unused]] WGPUTextureFormat swapchainFormat, [[maybe_unused]] WGPUTextureFormat depthTextureFormat)
{
    qDebug() << "Setup GuiManager...";
    m_window = window;
    m_device = device;
#ifdef ALP_WEBGPU_APP_ENABLE_IMGUI
    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    // Setup ImNodes
    ImNodes::CreateContext();

    // Setup Platform/Renderer backends
    ImGui_ImplSDL2_InitForOther(m_window);
    ImGui_ImplWGPU_InitInfo init_info = {};
    init_info.Device = m_device;
    init_info.RenderTargetFormat = swapchainFormat;
    init_info.DepthStencilFormat = depthTextureFormat;
    init_info.NumFramesInFlight = 3;
    ImGui_ImplWGPU_Init(&init_info);

    ImGui::StyleColorsLight();
    ImGui::GetStyle().Colors[ImGuiCol_WindowBg] = ImVec4(0.9f, 0.9f, 0.9f, 0.9f);
    ImNodes::StyleColorsLight();

    this->install_fonts();
#endif
}

void GuiManager::install_fonts()
{
#ifdef ALP_WEBGPU_APP_ENABLE_IMGUI
    ImGuiIO& io = ImGui::GetIO();

    float baseFontSize = 16.0f;
    float iconFontSize = 14.0f;

    {
        QFile file(":/fonts/Roboto-Regular.ttf");
        if (!file.open(QIODevice::ReadOnly)) {
            throw std::runtime_error("Failed to open Main Font.");
        }
        QByteArray byteArray = file.readAll();
        file.close();

        ImFontConfig font_cfg;
        font_cfg.FontDataOwnedByAtlas = false;
        io.Fonts->AddFontFromMemoryTTF(byteArray.data(), byteArray.size(), baseFontSize, &font_cfg);
    }

    {
        QFile file(":/fonts/fa5-solid-900.ttf");
        if (!file.open(QIODevice::ReadOnly)) {
            throw std::runtime_error("Failed to open glyph font.");
        }
        QByteArray byteArray = file.readAll();
        file.close();

        // merge in icons from Font Awesome
        static const ImWchar icons_ranges[] = { ICON_MIN_FA, ICON_MAX_16_FA, 0 };
        ImFontConfig icons_config;
        icons_config.MergeMode = true;
        icons_config.PixelSnapH = true;
        icons_config.GlyphMinAdvanceX = iconFontSize;
        icons_config.FontDataOwnedByAtlas = false;
        io.Fonts->AddFontFromMemoryTTF(byteArray.data(), byteArray.size(), iconFontSize, &icons_config, icons_ranges);
    }
#endif
}

void GuiManager::render([[maybe_unused]] WGPURenderPassEncoder renderPass)
{
#ifdef ALP_WEBGPU_APP_ENABLE_IMGUI
    ImGui_ImplWGPU_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    draw();

    ImGui::Render();
    ImGui_ImplWGPU_RenderDrawData(ImGui::GetDrawData(), renderPass);
#endif
}

void GuiManager::shutdown()
{
#ifdef ALP_WEBGPU_APP_ENABLE_IMGUI
    qDebug() << "Releasing GuiManager...";
    ImGui_ImplWGPU_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImNodes::DestroyContext();
    ImGui::DestroyContext();
#endif
}

bool GuiManager::want_capture_keyboard()
{
#ifdef ALP_WEBGPU_APP_ENABLE_IMGUI
    return ImGui::GetIO().WantCaptureKeyboard;
#else
    return false;
#endif
}

bool GuiManager::want_capture_mouse()
{
#ifdef ALP_WEBGPU_APP_ENABLE_IMGUI
    return ImGui::GetIO().WantCaptureMouse;
#else
    return false;
#endif
}

void GuiManager::on_sdl_event(SDL_Event& event)
{
#ifdef ALP_WEBGPU_APP_ENABLE_IMGUI
    ImGui_ImplSDL2_ProcessEvent(&event);
#endif
}

void GuiManager::set_gui_visibility(bool visible) { m_gui_visible = visible; }

bool GuiManager::get_gui_visibility() const { return m_gui_visible; }

void GuiManager::toggle_timer(uint32_t timer_id)
{
    if (is_timer_selected(timer_id)) {
        m_selected_timer.erase(timer_id);
    } else {
        m_selected_timer.clear(); // Remove if multiple selection possible
        m_selected_timer.insert(timer_id);
    }
}

bool GuiManager::is_timer_selected(uint32_t timer_id) { return m_selected_timer.find(timer_id) != m_selected_timer.end(); }

void GuiManager::draw()
{
#ifdef ALP_WEBGPU_APP_ENABLE_IMGUI
    static std::vector<std::pair<int, int>> links;
    static bool first_frame = true;

    if (!m_gui_visible)
        return;
    // ImGuiIO& io = ImGui::GetIO();

    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 400, 0)); // Set position to top-left corner
    ImGui::SetNextWindowSize(ImVec2(400, ImGui::GetIO().DisplaySize.y)); // Set height to full screen height, width as desired

    ImGui::Begin("weBIGeo", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar);

    if (ImGui::CollapsingHeader(ICON_FA_STOPWATCH "  Timing")) {

        const webgpu::timing::GuiTimerWrapper* selected_timer = nullptr;
        if (!m_selected_timer.empty()) {
            uint32_t first_selected_timer_id = *m_selected_timer.begin();
            selected_timer = m_terrain_renderer->get_timer_manager()->get_timer_by_id(first_selected_timer_id);
        }
        if (selected_timer) {
            const auto* tmr = selected_timer->timer.get();
            if (tmr->get_sample_count() > 2) {
                ImVec4 timer_color = *(ImVec4*)(void*)&selected_timer->color;
                ImGui::PushStyleColor(ImGuiCol_PlotLines, timer_color);
                ImGui::PlotLines("##SelTimerGraph", &tmr->get_results()[0], (int)tmr->get_sample_count(), 0, nullptr, 0.0f, tmr->get_max(), ImVec2(380, 80));
                ImGui::PopStyleColor();
            }
        }

        auto group_list = m_terrain_renderer->get_timer_manager()->get_groups();
        for (const auto& group : group_list) {
            bool showGroup = true;
            if (group.name != "") {
                ImGui::Indent();
                showGroup = ImGui::CollapsingHeader(group.name.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
            }
            if (showGroup) {
                for (const auto& tmr : group.timers) {
                    const uint32_t tmr_id = tmr.timer->get_id();
                    ImVec4 color(0.8f, 0.8f, 0.8f, 1.0f);
                    if (is_timer_selected(tmr_id)) {
                        color = *(ImVec4*)(void*)&tmr.color;
                    }

                    if (ImGui::ColorButton(
                            ("##t" + std::to_string(tmr_id)).c_str(), color, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop, ImVec2(10, 10))) {
                        toggle_timer(tmr_id);
                    }
                    ImGui::SameLine();
                    ImGui::Text("%s: %s ±%s [%zu]", tmr.name.c_str(), webgpu::timing::format_time(tmr.timer->get_average()).c_str(),
                        webgpu::timing::format_time(tmr.timer->get_standard_deviation()).c_str(), tmr.timer->get_sample_count());
                }
            }
            if (group.name != "")
                ImGui::Unindent();
        }
        if (ImGui::Button("Reset All Timers")) {
            for (const auto& group : group_list)
                for (const auto& tmr : group.timers)
                    tmr.timer->clear_results();
        }
    }

    if (ImGui::CollapsingHeader(ICON_FA_CAMERA " Camera")) {
        if (ImGui::BeginCombo("Preset", m_camera_preset_names[m_selected_camera_preset].c_str())) {
            for (size_t n = 0; n < m_camera_preset_names.size(); n++) {
                bool is_selected = (size_t(m_selected_camera_preset) == n);
                if (ImGui::Selectable(m_camera_preset_names[n].c_str(), is_selected)) {
                    m_selected_camera_preset = int(n);

                    const auto position_storage = nucleus::camera::PositionStorage::instance();
                    const auto camera_controller = m_terrain_renderer->get_controller()->camera_controller();
                    auto new_definition = position_storage->get_by_index(m_selected_camera_preset);
                    auto old_vp_size = camera_controller->definition().viewport_size();
                    new_definition.set_viewport_size(old_vp_size);
                    camera_controller->set_definition(new_definition);
                }
                if (is_selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
    }

    if (ImGui::CollapsingHeader(ICON_FA_COG "  App Settings")) {
        m_terrain_renderer->render_gui();
    }

    if (ImGui::CollapsingHeader(ICON_FA_COGS "  Engine Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
        auto webgpu_window = m_terrain_renderer->get_webgpu_window();
        if (webgpu_window) {
            webgpu_window->paint_gui();
        }
    }

    /*if (ImGui::Button(!m_show_nodeeditor ? ICON_FA_NETWORK_WIRED "  Show Node Editor" : ICON_FA_NETWORK_WIRED "  Hide Node Editor")) {
        m_show_nodeeditor = !m_show_nodeeditor;
    }*/
    ImGui::End();

    if (first_frame) {
        ImNodes::SetNodeScreenSpacePos(1, ImVec2(50, 50));
        ImNodes::SetNodeScreenSpacePos(2, ImVec2(400, 50));
    }

    if (m_show_nodeeditor) {
        // ========== BEGIN NODE WINDOW ===========
        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(ImGui::GetIO().DisplaySize.x - 400, ImGui::GetIO().DisplaySize.y), ImGuiCond_Always);
        ImGui::Begin("Node Editor", NULL, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar);

        // BEGINN NODE EDITOR
        ImNodes::BeginNodeEditor();

        // DRAW NODE 1
        ImNodes::BeginNode(1);

        ImNodes::BeginNodeTitleBar();
        ImGui::TextUnformatted("input node");
        ImNodes::EndNodeTitleBar();

        ImNodes::BeginOutputAttribute(2);
        ImGui::Text("data");
        ImNodes::EndOutputAttribute();

        ImNodes::EndNode();

        // DRAW NODE 2
        ImNodes::BeginNode(2);

        ImNodes::BeginNodeTitleBar();
        ImGui::TextUnformatted("output node");
        ImNodes::EndNodeTitleBar();

        ImNodes::BeginInputAttribute(3);
        ImGui::Text("data");
        ImNodes::EndInputAttribute();

        ImNodes::BeginInputAttribute(4, ImNodesPinShape_Triangle);
        ImGui::Text("overlay");
        ImNodes::EndInputAttribute();

        ImNodes::EndNode();

        // IMNODES - DRAW LINKS
        int id = 0;
        for (const auto& p : links) {
            ImNodes::Link(id++, p.first, p.second);
        }

        // IMNODES - MINIMAP
        ImNodes::MiniMap(0.1f, ImNodesMiniMapLocation_BottomRight);

        ImNodes::EndNodeEditor();

        int start_attr, end_attr;
        if (ImNodes::IsLinkCreated(&start_attr, &end_attr)) {
            links.push_back(std::make_pair(start_attr, end_attr));
        }

        ImGui::End();
    }

    {
        // === ROTATE NORTH BUTTON ===
        // Offset position from the bottom-left corner by 32 pixels, then position the button
        ImVec2 button_pos(10, ImGui::GetIO().DisplaySize.y - 48 - 40);
        ImGui::SetNextWindowPos(button_pos, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.5f); // Semi-transparent background
        ImGui::SetNextWindowSize(ImVec2(48, 48)); // Set button size

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0)); // No padding for better icon alignment
        ImGui::Begin("RotateNorthButton", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize);

        auto camController = m_terrain_renderer->get_controller()->camera_controller();

        // Draw button with custom rotation
        if (ImGui::InvisibleButton("RotateNorthBtn", ImVec2(48, 48))) {
            // Your code here when the button is clicked (e.g., rotate north)
            camController->rotate_north();
        }

        // Drawing the arrow icon manually with rotation
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        const auto rectMin = ImGui::GetItemRectMin();

        auto cameraFrontAxis = camController->definition().z_axis();
        auto degFromNorth = glm::degrees(glm::acos(glm::dot(glm::normalize(glm::dvec3(cameraFrontAxis.x, cameraFrontAxis.y, 0)), glm::dvec3(0, -1, 0))));
        float cameraAngle = cameraFrontAxis.x > 0 ? degFromNorth : -degFromNorth;

        ImVec2 center = ImVec2(rectMin.x + 24, rectMin.y + 24); // Center of the button
        float rotation_angle = cameraAngle * (M_PI / 180.0f); // Replace with your custom angle in degrees, converting to radians

        // Define arrow vertices relative to the center
        float arrow_length = 16.0f; // Size of the arrow
        ImVec2 points[3] = {
            ImVec2(0.0f, -arrow_length), // Arrow tip
            ImVec2(-arrow_length * 0.5f, arrow_length * 0.5f), // Left base
            ImVec2(arrow_length * 0.5f, arrow_length * 0.5f), // Right base
        };

        // Rotate and translate arrow vertices to draw at the specified angle
        for (int i = 0; i < 3; ++i) {
            float rotated_x = cos(rotation_angle) * points[i].x - sin(rotation_angle) * points[i].y;
            float rotated_y = sin(rotation_angle) * points[i].x + cos(rotation_angle) * points[i].y;
            points[i] = ImVec2(center.x + rotated_x, center.y + rotated_y);
        }

        // Draw the rotated arrow
        draw_list->AddTriangleFilled(points[0], points[1], points[2], IM_COL32(70, 70, 70, 255)); // White color for the arrow

        ImGui::End();
        ImGui::PopStyleVar(); // Restore padding
    }

    { // Draw the copyright box
        // Position the white box in the bottom-left corner
        ImGui::SetNextWindowPos(ImVec2(0, ImGui::GetIO().DisplaySize.y - 30), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.5f); // Semi-transparent background
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4, 4)); // Reduce padding
        // Set border color to transparent
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.0f)); // Transparent border
        ImGui::Begin("CopyrightBox", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize);

        // Set up a button with no hover effect by temporarily changing colors
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 1.0f, 1.0f, 0.0f)); // Transparent background
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.0f, 0.0f, 1.0f, 0.2f)); // No hover effect
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.0f, 0.0f, 1.0f, 0.1f)); // No active effect

        if (ImGui::Button("© basemap.at")) {
            util::open_website("https://basemap.at/");
        }

        ImGui::PopStyleColor(3); // Restore previous color settings
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(); // Restore padding
    }

    first_frame = false;
#endif
}

} // namespace webgpu_app
