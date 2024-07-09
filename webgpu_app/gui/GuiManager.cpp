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
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_wgpu.h"
#include <QDebug>
#include <imgui.h>
#include <imnodes.h>

void GuiManager::init(GLFWwindow* window, WGPUDevice device, WGPUTextureFormat swapchainFormat, WGPUTextureFormat depthTextureFormat)
{
    m_window = window;
    m_device = device;

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
}

void GuiManager::render(WGPURenderPassEncoder renderPass)
{
    ImGui_ImplWGPU_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    updateUI();

    ImGui::Render();
    ImGui_ImplWGPU_RenderDrawData(ImGui::GetDrawData(), renderPass);
}

void GuiManager::shutdown()
{
    qDebug() << "Releasing GuiManager...";
    ImGui_ImplWGPU_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImNodes::DestroyContext();
    ImGui::DestroyContext();
}

bool GuiManager::wantCaptureKeyboard() { return ImGui::GetIO().WantCaptureKeyboard; }

bool GuiManager::wantCaptureMouse() { return ImGui::GetIO().WantCaptureMouse; }

void GuiManager::updateUI()
{
    ImGuiIO& io = ImGui::GetIO();

    static float frame_time = 0.0f;
    static std::vector<std::pair<int, int>> links;
    static bool show_node_editor = false;
    static bool first_frame = true;

    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 300, 0)); // Set position to top-left corner
    ImGui::SetNextWindowSize(ImVec2(300, ImGui::GetIO().DisplaySize.y)); // Set height to full screen height, width as desired

    ImGui::Begin("weBIGeo", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar);

    // FPS counter variables
    static float fpsValues[90] = {}; // Array to store FPS values for the graph, adjust size as needed for the time window
    static int fpsIndex = 0; // Current index in FPS values array
    static float lastTime = 0.0f; // Last time FPS was updated

    // Calculate delta time and FPS
    float currentTime = ImGui::GetTime();
    float deltaTime = currentTime - lastTime;
    lastTime = currentTime;
    float fps = 1.0f / deltaTime;

    // Store the current FPS value in the array
    fpsValues[fpsIndex] = fps;
    fpsIndex = (fpsIndex + 1) % IM_ARRAYSIZE(fpsValues); // Loop around the array

    frame_time = frame_time * 0.95f + (1000.0f / io.Framerate) * 0.05f;
    /*
    static bool vsync_enabled = (m_swapchain_presentmode == WGPUPresentMode::WGPUPresentMode_Fifo);
    if (ImGui::Checkbox("VSync", &vsync_enabled)) {
        m_swapchain_presentmode = vsync_enabled ? WGPUPresentMode::WGPUPresentMode_Fifo : WGPUPresentMode::WGPUPresentMode_Immediate;
        // Recreate swapchain
        resize_framebuffer(m_swapchain_size.x, m_swapchain_size.y);
    }*/
    ImGui::Text("Average: %.3f ms/frame (%.1f FPS)", frame_time, io.Framerate);

    ImGui::PlotLines("", fpsValues, IM_ARRAYSIZE(fpsValues), fpsIndex, nullptr, 0.0f, 80.0f, ImVec2(280, 100));

    ImGui::Separator();
    /*
    ImGui::Combo("Normal Mode", (int*)&m_shared_config_ubo->data.m_normal_mode, "None\0Flat\0Smooth\0\0");

    {
        static int currentItem = m_shared_config_ubo->data.m_overlay_mode;
        static const std::vector<std::pair<std::string, int>> overlays = {
            {"None", 0},
            {"Normals", 1},
            {"Tiles", 2},
            {"Zoomlevel", 3},
            {"Vertex-ID", 4},
            {"Vertex Height-Sample", 5},
            {"Decoded Normals", 100},
            {"Steepness", 101},
            {"SSAO Buffer", 102},
            {"Shadow Cascades", 103}
        };
        const char* currentItemLabel = overlays[currentItem].first.c_str();
        if (ImGui::BeginCombo("Overlay", currentItemLabel))
        {
            for (size_t i = 0; i < overlays.size(); i++)
            {
                bool isSelected = ((size_t)currentItem == i);
                if (ImGui::Selectable(overlays[i].first.c_str(), isSelected)) currentItem = i;
                if (isSelected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        m_shared_config_ubo->data.m_overlay_mode = overlays[currentItem].second;
        if (m_shared_config_ubo->data.m_overlay_mode > 0) {
            ImGui::SliderFloat("Overlay Strength", &m_shared_config_ubo->data.m_overlay_strength, 0.0f, 1.0f);
        }
        if (m_shared_config_ubo->data.m_overlay_mode >= 100) {
            ImGui::Checkbox("Overlay Post Shading", (bool*)&m_shared_config_ubo->data.m_overlay_postshading_enabled);
        }
    }

    ImGui::Checkbox("Phong Shading", (bool*)&m_shared_config_ubo->data.m_phong_enabled);




    if (ImGui::CollapsingHeader("Compute pipeline")) {
        if (ImGui::Button("Request tiles", ImVec2(280, 20))) {
            // hardcoded test region
            RectangularTileRegion region;
            region.min = { 1096, 1328 };
            region.max = { 1096 + 14, 1328 + 14 }; // inclusive, so this region has 15x15 tiles
            region.scheme = tile::Scheme::Tms;
            region.zoom_level = 11;
            m_compute_controller->request_tiles(region);
        }

        if (ImGui::Button("Run pipeline", ImVec2(280, 20))) {
            m_compute_controller->run_pipeline();
        }

        if (ImGui::Button("Write per-tile output to files", ImVec2(280, 20))) {
            m_compute_controller->write_output_tiles("output_tiles"); // writes dir output_tiles next to app.exe
        }
    }
*/

    if (ImGui::Button(!show_node_editor ? "Show Node Editor" : "Hide Node Editor", ImVec2(280, 20))) {
        show_node_editor = !show_node_editor;
    }
    ImGui::End();

    if (first_frame) {
        ImNodes::SetNodeScreenSpacePos(1, ImVec2(50, 50));
        ImNodes::SetNodeScreenSpacePos(2, ImVec2(400, 50));
    }

    if (show_node_editor) {
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
}
