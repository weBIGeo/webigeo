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
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_wgpu.h"
#include <imgui.h>
#include <imnodes.h>
#endif
#include "webgpu_engine/Window.h"
#include <QDebug>

namespace webgpu_app {

void GuiManager::init(
    GLFWwindow* window, WGPUDevice device, [[maybe_unused]] WGPUTextureFormat swapchainFormat, [[maybe_unused]] WGPUTextureFormat depthTextureFormat)
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
    ImGui_ImplGlfw_InitForOther(m_window, true);
    ImGui_ImplWGPU_InitInfo init_info = {};
    init_info.Device = m_device;
    init_info.RenderTargetFormat = swapchainFormat;
    init_info.DepthStencilFormat = depthTextureFormat;
    init_info.NumFramesInFlight = 3;
    ImGui_ImplWGPU_Init(&init_info);

    ImGui::StyleColorsLight();
    ImGui::GetStyle().Colors[ImGuiCol_WindowBg] = ImVec4(0.9f, 0.9f, 0.9f, 0.9f);
    ImNodes::StyleColorsLight();
#endif
}

void GuiManager::render([[maybe_unused]] WGPURenderPassEncoder renderPass)
{
#ifdef ALP_WEBGPU_APP_ENABLE_IMGUI
    ImGui_ImplWGPU_NewFrame();
    ImGui_ImplGlfw_NewFrame();
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
    ImGui_ImplGlfw_Shutdown();
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

void GuiManager::draw()
{
#ifdef ALP_WEBGPU_APP_ENABLE_IMGUI
    static float frame_time = 0.0f;
    static std::vector<std::pair<int, int>> links;
    static bool first_frame = true;
    static float fpsValues[90] = {}; // Array to store FPS values for the graph, adjust size as needed for the time window
    static float fpsRepaint[90] = {};
    static int fpsIndex = 0; // Current index in FPS values array
    static float lastTime = 0.0f; // Last time FPS was updated

    ImGuiIO& io = ImGui::GetIO();

    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 300, 0)); // Set position to top-left corner
    ImGui::SetNextWindowSize(ImVec2(300, ImGui::GetIO().DisplaySize.y)); // Set height to full screen height, width as desired

    ImGui::Begin("weBIGeo", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar);

    // Calculate delta time and FPS
    float currentTime = ImGui::GetTime();
    float deltaTime = currentTime - lastTime;
    lastTime = currentTime;
    float fps = 1.0f / deltaTime;

    // Store the current FPS value in the array
    fpsValues[fpsIndex] = fps;
    fpsRepaint[fpsIndex] = m_terrain_renderer->prop_repaint_count;
    fpsIndex = (fpsIndex + 1) % IM_ARRAYSIZE(fpsValues); // Loop around the array

    frame_time = frame_time * 0.95f + (1000.0f / io.Framerate) * 0.05f;

    ImGui::Text("Average: %.3f ms/frame (%.1f FPS)", frame_time, io.Framerate);

    ImGui::PlotLines("", fpsValues, IM_ARRAYSIZE(fpsValues), fpsIndex, nullptr, 0.0f, 70.0f, ImVec2(280, 80));

    if (ImGui::CollapsingHeader("Timing", ImGuiTreeNodeFlags_DefaultOpen)) {
        auto group_list = m_terrain_renderer->get_timer_manager()->get_groups();
        for (const auto& group : group_list) {
            bool showGroup = true;
            if (group.name != "") {
                showGroup = ImGui::CollapsingHeader(group.name.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
            }
            if (showGroup) {
                for (const auto& tmr : group.timers) {
                    ImVec4 color(tmr.color.x, tmr.color.y, tmr.color.z, tmr.color.w);

                    if (ImGui::ColorButton("##Color", color, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop, ImVec2(10, 10))) {
                        tmr.timer->clear_results();
                    }
                    ImGui::SameLine();
                    ImGui::Text("%s: %s ±%s [%zu]", tmr.name.c_str(), timing::format_time(tmr.timer->get_average()).c_str(),
                        timing::format_time(tmr.timer->get_standard_deviation()).c_str(), tmr.timer->get_sample_count());
                }
            }
        }
        if (ImGui::Button("Reset All Timers")) {
            for (const auto& group : group_list)
                for (const auto& tmr : group.timers)
                    tmr.timer->clear_results();
        }
    }

    if (ImGui::CollapsingHeader("APP Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
        m_terrain_renderer->render_gui();
    }

    if (ImGui::CollapsingHeader("Engine Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
        auto webgpu_window = m_terrain_renderer->get_webgpu_window();
        if (webgpu_window) {
            webgpu_window->paint_gui();
        }
    }

    if (ImGui::Button(!m_showNodeEditor ? "Show Node Editor" : "Hide Node Editor", ImVec2(280, 20))) {
        m_showNodeEditor = !m_showNodeEditor;
    }
    ImGui::End();

    if (first_frame) {
        ImNodes::SetNodeScreenSpacePos(1, ImVec2(50, 50));
        ImNodes::SetNodeScreenSpacePos(2, ImVec2(400, 50));
    }

    if (m_showNodeEditor) {
        // ========== BEGIN NODE WINDOW ===========
        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(ImGui::GetIO().DisplaySize.x - 300, ImGui::GetIO().DisplaySize.y), ImGuiCond_Always);
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

    first_frame = false;
#endif
}

} // namespace webgpu_app
