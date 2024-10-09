/*****************************************************************************
 * weBIGeo
 * Copyright (C) 2024 Patrick Komon
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

#include "Window.h"
#include "compute/nodes/ComputeAvalancheTrajectoriesNode.h"
#include "compute/nodes/ComputeSnowNode.h"
#include "compute/nodes/SelectTilesNode.h"
#include "nucleus/track/GPX.h"
#include "webgpu/raii/RenderPassEncoder.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#include <webgpu_app/WebInterop.h>
#endif
#include <webgpu/webgpu.h>
#include <webgpu/webgpu_interface.hpp>

#include <glm/gtx/string_cast.hpp>

#ifdef ALP_WEBGPU_APP_ENABLE_IMGUI
#ifndef __EMSCRIPTEN__
#include "ImGuiFileDialog.h"
#endif
// TODO: Remove ImGuiFileDialog dependency on Web-build
#include "imgui.h"
#endif

namespace webgpu_engine {

Window::Window()
    : m_tile_manager { std::make_unique<TileManager>() }
{
#ifdef __EMSCRIPTEN__
    connect(&WebInterop::instance(), &WebInterop::file_uploaded, this, &Window::load_track_and_focus);
#endif
}

Window::~Window()
{
    // Destructor cleanup logic here
}

void Window::set_wgpu_context(WGPUInstance instance, WGPUDevice device, WGPUAdapter adapter, WGPUSurface surface, WGPUQueue queue)
{
    m_instance = instance;
    m_device = device;
    m_adapter = adapter;
    m_surface = surface;
    m_queue = queue;
}

void Window::initialise_gpu()
{
    assert(m_device != nullptr); // just make sure that wgpu context is set

    create_buffers();

    m_shader_manager = std::make_unique<ShaderModuleManager>(m_device);
    m_shader_manager->create_shader_modules();
    m_pipeline_manager = std::make_unique<PipelineManager>(m_device, *m_shader_manager);
    m_pipeline_manager->create_pipelines();
    create_bind_groups();

    m_tile_manager->init(m_device, m_queue, *m_pipeline_manager);

    create_and_set_compute_pipeline(ComputePipelineType::AVALANCHE_TRAJECTORIES);

    m_track_renderer = std::make_unique<TrackRenderer>(m_device, *m_pipeline_manager);

    qInfo() << "gpu_ready_changed";
    emit gpu_ready_changed(true);
}

void Window::resize_framebuffer(int w, int h)
{
    m_swapchain_size = glm::vec2(w, h);

    m_gbuffer_format = webgpu::FramebufferFormat(m_pipeline_manager->tile_pipeline().framebuffer_format());
    m_gbuffer_format.size = glm::uvec2 { w, h };
    m_gbuffer = std::make_unique<webgpu::Framebuffer>(m_device, m_gbuffer_format);

    webgpu::FramebufferFormat atmosphere_framebuffer_format(m_pipeline_manager->atmosphere_pipeline().framebuffer_format());
    atmosphere_framebuffer_format.size = glm::uvec2(1, h);
    m_atmosphere_framebuffer = std::make_unique<webgpu::Framebuffer>(m_device, atmosphere_framebuffer_format);

    m_compose_bind_group = std::make_unique<webgpu::raii::BindGroup>(m_device, m_pipeline_manager->compose_bind_group_layout(),
        std::initializer_list<WGPUBindGroupEntry> {
            m_gbuffer->color_texture_view(0).create_bind_group_entry(0), // albedo texture
            m_gbuffer->color_texture_view(1).create_bind_group_entry(1), // position texture
            m_gbuffer->color_texture_view(2).create_bind_group_entry(2), // normal texture
            m_atmosphere_framebuffer->color_texture_view(0).create_bind_group_entry(3) // atmosphere texture
        });

    m_depth_texture_bind_group = std::make_unique<webgpu::raii::BindGroup>(m_device, m_pipeline_manager->depth_texture_bind_group_layout(),
        std::initializer_list<WGPUBindGroupEntry> {
            m_gbuffer->depth_texture_view().create_bind_group_entry(0) // depth
        });
}

std::unique_ptr<webgpu::raii::RenderPassEncoder> begin_render_pass(
    WGPUCommandEncoder encoder, WGPUTextureView color_attachment, WGPUTextureView depth_attachment)
{
    return std::make_unique<webgpu::raii::RenderPassEncoder>(encoder, color_attachment, depth_attachment);
}

void Window::paint(webgpu::Framebuffer* framebuffer, WGPUCommandEncoder command_encoder)
{
    // Painting logic here, using the optional framebuffer parameter which is currently unused

    // ONLY ON CAMERA CHANGE!
    // update_camera(m_camera);
    emit update_camera_requested();

    // TODO remove, debugging
    // uboSharedConfig* sc = &m_shared_config_ubo->data;
    // sc->m_sun_light = QVector4D(0.0f, 1.0f, 1.0f, 1.0f);
    // sc->m_sun_light_dir = QVector4D(elapsed, 1.0f, 1.0f, 1.0f);
    // ToDo only update on change?
    m_shared_config_ubo->update_gpu_data(m_queue);

    // render atmosphere to color buffer
    {
        std::unique_ptr<webgpu::raii::RenderPassEncoder> render_pass = m_atmosphere_framebuffer->begin_render_pass(command_encoder);
        wgpuRenderPassEncoderSetBindGroup(render_pass->handle(), 0, m_camera_bind_group->handle(), 0, nullptr);
        wgpuRenderPassEncoderSetPipeline(render_pass->handle(), m_pipeline_manager->atmosphere_pipeline().pipeline().handle());
        wgpuRenderPassEncoderDraw(render_pass->handle(), 3, 1, 0, 0);
    }

    // render tiles to geometry buffers
    {
        std::unique_ptr<webgpu::raii::RenderPassEncoder> render_pass = m_gbuffer->begin_render_pass(command_encoder);
        wgpuRenderPassEncoderSetBindGroup(render_pass->handle(), 0, m_shared_config_bind_group->handle(), 0, nullptr);
        wgpuRenderPassEncoderSetBindGroup(render_pass->handle(), 1, m_camera_bind_group->handle(), 0, nullptr);

        const auto tile_set = m_tile_manager->generate_tilelist(m_camera);
        m_tile_manager->draw(render_pass->handle(), m_camera, tile_set, true, m_camera.position());
    }

    // render geometry buffers to target framebuffer
    {
        std::unique_ptr<webgpu::raii::RenderPassEncoder> render_pass = framebuffer->begin_render_pass(command_encoder);
        wgpuRenderPassEncoderSetPipeline(render_pass->handle(), m_pipeline_manager->compose_pipeline().pipeline().handle());
        wgpuRenderPassEncoderSetBindGroup(render_pass->handle(), 0, m_shared_config_bind_group->handle(), 0, nullptr);
        wgpuRenderPassEncoderSetBindGroup(render_pass->handle(), 1, m_camera_bind_group->handle(), 0, nullptr);
        wgpuRenderPassEncoderSetBindGroup(render_pass->handle(), 2, m_compose_bind_group->handle(), 0, nullptr);
        wgpuRenderPassEncoderDraw(render_pass->handle(), 3, 1, 0, 0);
    }

    // render lines to color buffer
    if (m_shared_config_ubo->data.m_track_render_mode > 0) {
        m_track_renderer->render(
            command_encoder, *m_shared_config_bind_group, *m_camera_bind_group, *m_depth_texture_bind_group, framebuffer->color_texture_view(0));
    }

    m_needs_redraw = false;
}

void Window::paint_gui()
{
#ifdef ALP_WEBGPU_APP_ENABLE_IMGUI
    if (ImGui::Combo("Normal Mode", (int*)&m_shared_config_ubo->data.m_normal_mode, "None\0Flat\0Smooth\0\0")) {
        m_needs_redraw = true;
    }
    {
        static int currentItem = 6;
        static const std::vector<std::pair<std::string, int>> overlays
            = { { "None", 0 }, { "Normals", 1 }, { "Tiles", 2 }, { "Zoomlevel", 3 }, { "Vertex-ID", 4 }, { "Vertex Height-Sample", 5 },
                  { "Compute Output", 99 }, { "Decoded Normals", 100 }, { "Steepness", 101 }, { "SSAO Buffer", 102 }, { "Shadow Cascades", 103 } };
        const char* currentItemLabel = overlays[currentItem].first.c_str();
        if (ImGui::BeginCombo("Overlay", currentItemLabel)) {
            for (size_t i = 0; i < overlays.size(); i++) {
                bool isSelected = ((size_t)currentItem == i);
                if (ImGui::Selectable(overlays[i].first.c_str(), isSelected)) {
                    currentItem = i;
                    m_needs_redraw = true;
                }
                if (isSelected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        m_shared_config_ubo->data.m_overlay_mode = overlays[currentItem].second;
        if (m_shared_config_ubo->data.m_overlay_mode > 0) {
            if (ImGui::SliderFloat("Overlay Strength", &m_shared_config_ubo->data.m_overlay_strength, 0.0f, 1.0f)) {
                m_needs_redraw = true;
            }
        }
        if (m_shared_config_ubo->data.m_overlay_mode >= 100) {
            if (ImGui::Checkbox("Overlay Post Shading", (bool*)&m_shared_config_ubo->data.m_overlay_postshading_enabled)) {
                m_needs_redraw = true;
            }
        }

        if (ImGui::Checkbox("Phong Shading", (bool*)&m_shared_config_ubo->data.m_phong_enabled)) {
            m_needs_redraw = true;
        }

        bool snow_on = (m_shared_config_ubo->data.m_snow_settings_angle.x == 1.0f);
        if (ImGui::Checkbox("Snow", &snow_on)) {
            m_needs_redraw = true;
            m_shared_config_ubo->data.m_snow_settings_angle.x = (snow_on ? 1.0f : 0.0f);
        }

        if (m_shared_config_ubo->data.m_snow_settings_angle.x) {
            if (ImGui::DragFloatRange2("Angle limit", &m_shared_config_ubo->data.m_snow_settings_angle.y, &m_shared_config_ubo->data.m_snow_settings_angle.z,
                    0.1f, 0.0f, 90.0f, "Min: %.1f°", "Max: %.1f°", ImGuiSliderFlags_AlwaysClamp)) {
                m_needs_redraw = true;
                update_compute_pipeline_settings();
            }
            if (ImGui::SliderFloat("Angle blend", &m_shared_config_ubo->data.m_snow_settings_angle.w, 0.0f, 90.0f, "%.1f°")) {
                m_needs_redraw = true;
                update_compute_pipeline_settings();
            }
            if (ImGui::SliderFloat("Altitude limit", &m_shared_config_ubo->data.m_snow_settings_alt.x, 0.0f, 4000.0f, "%.1fm")) {
                m_needs_redraw = true;
                update_compute_pipeline_settings();
            }
            if (ImGui::SliderFloat("Altitude variation", &m_shared_config_ubo->data.m_snow_settings_alt.y, 0.0f, 1000.0f, "%.1f°")) {
                m_needs_redraw = true;
                update_compute_pipeline_settings();
            }
            if (ImGui::SliderFloat("Altitude blend", &m_shared_config_ubo->data.m_snow_settings_alt.z, 0.0f, 1000.0f)) {
                m_needs_redraw = true;
                update_compute_pipeline_settings();
            }
            if (ImGui::SliderFloat("Specular", &m_shared_config_ubo->data.m_snow_settings_alt.w, 0.0f, 5.0f)) {
                m_needs_redraw = true;
                update_compute_pipeline_settings();
            }
        }
    }

    if (ImGui::CollapsingHeader("Track", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Button("Open GPX file ...", ImVec2(350, 20))) {
#ifdef __EMSCRIPTEN__
            WebInterop::instance().open_file_dialog(".gpx");
#else
            IGFD::FileDialogConfig config;
            config.path = ".";
            ImGuiFileDialog::Instance()->OpenDialog("ChooseFileDlgKey", "Choose File", ".gpx,.*", config);
#endif
        }

        const char* items = "none\0without depth test\0with depth test\0semi-transparent\0";
        if (ImGui::Combo("Line render mode", (int*)&(m_shared_config_ubo->data.m_track_render_mode), items)) {
            m_needs_redraw = true;
        }
    }

#ifndef __EMSCRIPTEN__
    if (ImGuiFileDialog::Instance()->Display("ChooseFileDlgKey")) {
        if (ImGuiFileDialog::Instance()->IsOk()) { // action if OK
            std::string file_path = ImGuiFileDialog::Instance()->GetFilePathName();
            load_track_and_focus(file_path);
        }
        ImGuiFileDialog::Instance()->Close();
    }
#endif

    paint_compute_pipeline_gui();

#endif
}

void Window::paint_compute_pipeline_gui()
{
    if (ImGui::CollapsingHeader("Compute pipeline", ImGuiTreeNodeFlags_DefaultOpen)) {

        if (ImGui::Button("Run", ImVec2(150, 20))) {
            if (m_compute_pipeline_settings.is_region_select) {
                m_compute_graph->run();
            }
        }

        ImGui::SameLine();

        if (ImGui::Button("Clear", ImVec2(150, 20))) {
            create_and_set_compute_pipeline(m_active_compute_pipeline_type);
            m_needs_redraw = true;
        }

        const uint32_t min_zoomlevel = 1;
        const uint32_t max_zoomlevel = 18;
        ImGui::SliderScalar("Target zoom level", ImGuiDataType_U32, &m_compute_pipeline_settings.target_zoomlevel, &min_zoomlevel, &max_zoomlevel, "%u");
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            recreate_and_rerun_compute_pipeline();
        }

        static int current_item = 2;
        const std::vector<std::pair<std::string, ComputePipelineType>> overlays = {
            { "Normals", ComputePipelineType::NORMALS },
            { "Snow + Normals", ComputePipelineType::NORMALS_AND_SNOW },
            { "Avalanche trajectories + Normals", ComputePipelineType::AVALANCHE_TRAJECTORIES },
            { "Avalanche influence area + Normals", ComputePipelineType::AVALANCHE_INFLUENCE_AREA },
        };
        const char* current_item_label = overlays[current_item].first.c_str();
        if (ImGui::BeginCombo("Type", current_item_label)) {
            for (size_t i = 0; i < overlays.size(); i++) {
                bool is_selected = ((size_t)current_item == i);
                if (ImGui::Selectable(overlays[i].first.c_str(), is_selected)) {
                    current_item = i;
                    create_and_set_compute_pipeline(overlays[i].second);
                }
                if (is_selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        if (m_active_compute_pipeline_type == ComputePipelineType::AVALANCHE_TRAJECTORIES) {
            ImGui::SliderScalar("Source zoom level", ImGuiDataType_U32, &m_compute_pipeline_settings.source_zoomlevel, &min_zoomlevel, &max_zoomlevel, "%u");
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                recreate_and_rerun_compute_pipeline();
            }

            const uint32_t min_steps = 1;
            const uint32_t max_steps = 1024;
            ImGui::SliderScalar("Num steps", ImGuiDataType_U32, &m_compute_pipeline_settings.num_steps, &min_steps, &max_steps, "%u");
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                recreate_and_rerun_compute_pipeline();
            }

            ImGui::SliderFloat("Step length", &m_compute_pipeline_settings.steps_length, 0.01, 5.0, "%.1f");
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                recreate_and_rerun_compute_pipeline();
            }

            static int current_physics_model_item = 0;
            const std::vector<std::pair<std::string, compute::nodes::ComputeAvalancheTrajectoriesNode::PhysicsModelType>> physics_model_types
                = { { "Momentum (simple)", compute::nodes::ComputeAvalancheTrajectoriesNode::PhysicsModelType::MODEL1 },
                      { "Momentum (less simple)", compute::nodes::ComputeAvalancheTrajectoriesNode::PhysicsModelType::MODEL2 },
                      { "Gradients", compute::nodes::ComputeAvalancheTrajectoriesNode::PhysicsModelType::MODEL3 } };
            const char* current_item_label = physics_model_types[current_physics_model_item].first.c_str();
            if (ImGui::BeginCombo("Physics model", current_item_label)) {
                for (size_t i = 0; i < physics_model_types.size(); i++) {
                    bool is_selected = ((size_t)current_physics_model_item == i);
                    if (ImGui::Selectable(physics_model_types[i].first.c_str(), is_selected)) {
                        current_physics_model_item = i;
                        m_compute_pipeline_settings.model_type = physics_model_types[i].second;
                        recreate_and_rerun_compute_pipeline();
                    }
                    if (is_selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            if (m_compute_pipeline_settings.model_type == compute::nodes::ComputeAvalancheTrajectoriesNode::PhysicsModelType::MODEL1) {
                ImGui::SliderFloat("Linear drag coeff##model1", &m_compute_pipeline_settings.model1_velocity_coeff, 0.0f, 1.0f, "%.2f");
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    recreate_and_rerun_compute_pipeline();
                }
                ImGui::SliderFloat("Speedup coeff##model1", &m_compute_pipeline_settings.model1_gradient_coeff, 0.0f, 1.0f, "%.2f");
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    recreate_and_rerun_compute_pipeline();
                }
            } else if (m_compute_pipeline_settings.model_type == compute::nodes::ComputeAvalancheTrajectoriesNode::PhysicsModelType::MODEL2) {
                ImGui::SliderFloat("Gravity##model2", &m_compute_pipeline_settings.model2_gravity, 0.0f, 15.0f, "%.2f");
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    recreate_and_rerun_compute_pipeline();
                }
                ImGui::SliderFloat("Mass##model2", &m_compute_pipeline_settings.model2_mass, 0.0f, 10.0f, "%.2f");
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    recreate_and_rerun_compute_pipeline();
                }
                ImGui::SliderFloat("Drag coeff##model2", &m_compute_pipeline_settings.model2_drag_coeff, 0.0f, 1.0f, "%.2f");
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    recreate_and_rerun_compute_pipeline();
                }
                ImGui::SliderFloat("Friction coeff##model2", &m_compute_pipeline_settings.model2_friction_coeff, 0.0f, 1.0f, "%.2f");
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    recreate_and_rerun_compute_pipeline();
                }
            }
        } else if (m_active_compute_pipeline_type == ComputePipelineType::AVALANCHE_INFLUENCE_AREA) {
            const uint32_t min_steps = 1;
            const uint32_t max_steps = 1024;
            ImGui::SliderScalar("Num steps", ImGuiDataType_U32, &m_compute_pipeline_settings.num_steps, &min_steps, &max_steps, "%u");
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                recreate_and_rerun_compute_pipeline();
            }

            ImGui::SliderFloat("Step length", &m_compute_pipeline_settings.steps_length, 0.01, 5.0, "%.1f");
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                recreate_and_rerun_compute_pipeline();
            }

            ImGui::SliderFloat("Radius", &m_compute_pipeline_settings.radius, 0.0f, 100.0f, "%.1fm");
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                recreate_and_rerun_compute_pipeline();
            }

        } else if (m_active_compute_pipeline_type == ComputePipelineType::NORMALS_AND_SNOW) {

            if (ImGui::Checkbox("Sync with render settings", &m_compute_pipeline_settings.sync_snow_settings_with_render_settings)) {
                update_compute_pipeline_settings();
            }

            if (!m_compute_pipeline_settings.sync_snow_settings_with_render_settings) {
                if (ImGui::DragFloatRange2("Angle limit##compute", &m_compute_pipeline_settings.snow_settings.angle.y,
                        &m_compute_pipeline_settings.snow_settings.angle.z, 0.1f, 0.0f, 90.0f, "Min: %.1f°", "Max: %.1f°", ImGuiSliderFlags_AlwaysClamp)) {
                    update_compute_pipeline_settings();
                }
                if (ImGui::SliderFloat("Angle blend##compute", &m_compute_pipeline_settings.snow_settings.angle.w, 0.0f, 90.0f, "%.1f°")) {
                    update_compute_pipeline_settings();
                }
                if (ImGui::SliderFloat("Altitude limit##compute", &m_compute_pipeline_settings.snow_settings.alt.x, 0.0f, 4000.0f, "%.1fm")) {
                    update_compute_pipeline_settings();
                }
                if (ImGui::SliderFloat("Altitude variation##compute", &m_compute_pipeline_settings.snow_settings.alt.y, 0.0f, 1000.0f, "%.1f°")) {
                    update_compute_pipeline_settings();
                }
                if (ImGui::SliderFloat("Altitude blend##compute", &m_compute_pipeline_settings.snow_settings.alt.z, 0.0f, 1000.0f)) {
                    update_compute_pipeline_settings();
                }
                if (ImGui::SliderFloat("Specular##compute", &m_compute_pipeline_settings.snow_settings.alt.w, 0.0f, 5.0f)) {
                    update_compute_pipeline_settings();
                }
            }
        }
    }
}

glm::vec4 Window::synchronous_position_readback(const glm::dvec2& ndc)
{
    if (m_position_readback_buffer->map_state() == WGPUBufferMapState_Unmapped) {
        // A little bit silly, but we have to transform it back to device coordinates
        glm::uvec2 device_coordinates = { (ndc.x + 1) * 0.5 * m_swapchain_size.x, (1 - (ndc.y + 1) * 0.5) * m_swapchain_size.y };

        // clamp device coordinates to the swapchain size
        device_coordinates = glm::clamp(device_coordinates, glm::uvec2(0), glm::uvec2(m_swapchain_size - glm::vec2(1.0)));

        const auto& src_texture = m_gbuffer->color_texture(1);
        // Need to read a multiple of 16 values to fit requirement for texture_to_buffer copy
        src_texture.copy_to_buffer(m_device, *m_position_readback_buffer.get(), glm::uvec3(device_coordinates.x, device_coordinates.y, 0), glm::uvec2(16, 1));

        std::vector<glm::vec4> pos_buffer;
        WGPUBufferMapAsyncStatus result = m_position_readback_buffer->read_back_sync(m_device, pos_buffer);
        if (result == WGPUBufferMapAsyncStatus_Success) {
            m_last_position_readback = pos_buffer[0];
        }
    } // else qDebug() << "Dropped position readback request, buffer still mapping.";

    // qDebug() << "Position:" << glm::to_string(m_last_position_readback);
    return m_last_position_readback;
}

void Window::create_and_set_compute_pipeline(ComputePipelineType pipeline_type)
{
    qDebug() << "setting new compute pipeline " << static_cast<int>(pipeline_type);
    m_active_compute_pipeline_type = pipeline_type;

    if (pipeline_type == ComputePipelineType::NORMALS) {
        m_compute_graph = compute::nodes::NodeGraph::create_normal_compute_graph(*m_pipeline_manager, m_device);
    } else if (pipeline_type == ComputePipelineType::NORMALS_AND_SNOW) {
        m_compute_graph = compute::nodes::NodeGraph::create_normal_with_snow_compute_graph(*m_pipeline_manager, m_device);
    } else if (pipeline_type == ComputePipelineType::AVALANCHE_TRAJECTORIES) {
        m_compute_graph = compute::nodes::NodeGraph::create_avalanche_trajectories_compute_graph(*m_pipeline_manager, m_device);
    } else if (pipeline_type == ComputePipelineType::AVALANCHE_INFLUENCE_AREA) {
        m_compute_graph = compute::nodes::NodeGraph::create_avalanche_influence_area_compute_graph(*m_pipeline_manager, m_device);
    }

    update_compute_pipeline_settings();

    connect(m_compute_graph.get(), &compute::nodes::NodeGraph::run_finished, this, &Window::request_redraw);
    m_tile_manager->set_node_graph(*m_compute_graph);
}

void Window::update_compute_pipeline_settings()
{
    if (m_active_compute_pipeline_type == ComputePipelineType::NORMALS) {
        // tile selection
        m_compute_graph->get_node_as<compute::nodes::SelectTilesNode>("select_tiles_node")
            .select_tiles_in_world_aabb(m_compute_pipeline_settings.target_region, m_compute_pipeline_settings.target_zoomlevel);
    } else if (m_active_compute_pipeline_type == ComputePipelineType::NORMALS_AND_SNOW) {
        // tile selection
        m_compute_graph->get_node_as<compute::nodes::SelectTilesNode>("select_tiles_node")
            .select_tiles_in_world_aabb(m_compute_pipeline_settings.target_region, m_compute_pipeline_settings.target_zoomlevel);

        // snow settings
        if (m_compute_pipeline_settings.sync_snow_settings_with_render_settings) {
            m_compute_pipeline_settings.snow_settings.alt = m_shared_config_ubo->data.m_snow_settings_alt;
            m_compute_pipeline_settings.snow_settings.angle = m_shared_config_ubo->data.m_snow_settings_angle;
        }
        m_compute_graph->get_node_as<compute::nodes::ComputeSnowNode>("compute_snow_node").set_snow_settings(m_compute_pipeline_settings.snow_settings);
    } else if (m_active_compute_pipeline_type == ComputePipelineType::AVALANCHE_TRAJECTORIES
        || m_active_compute_pipeline_type == ComputePipelineType::AVALANCHE_INFLUENCE_AREA) {
        // tile selection
        m_compute_graph->get_node_as<compute::nodes::SelectTilesNode>("select_target_tiles_node")
            .select_tiles_in_world_aabb(m_compute_pipeline_settings.target_region, m_compute_pipeline_settings.target_zoomlevel);

        // data source tile selection
        m_compute_graph->get_node_as<compute::nodes::SelectTilesNode>("select_source_tiles_node")
            .select_tiles_in_world_aabb(m_compute_pipeline_settings.target_region, m_compute_pipeline_settings.source_zoomlevel);

        // area of influence settings
        auto& area_of_influence_node = m_compute_graph->get_node_as<compute::nodes::ComputeAvalancheTrajectoriesNode>("compute_area_of_influence_node");
        area_of_influence_node.set_reference_point_world(m_compute_pipeline_settings.reference_point);
        area_of_influence_node.set_target_point_world(m_compute_pipeline_settings.target_point);
        area_of_influence_node.set_num_steps(m_compute_pipeline_settings.num_steps);
        area_of_influence_node.set_step_length(m_compute_pipeline_settings.steps_length);
        area_of_influence_node.set_radius(m_compute_pipeline_settings.radius);
        area_of_influence_node.set_source_zoomlevel(m_compute_pipeline_settings.source_zoomlevel);
        area_of_influence_node.set_physics_model_type(m_compute_pipeline_settings.model_type);
        area_of_influence_node.set_model1_downward_acceleration_coeff(m_compute_pipeline_settings.model1_gradient_coeff);
        area_of_influence_node.set_model1_linear_drag_coeff(m_compute_pipeline_settings.model1_velocity_coeff);
        area_of_influence_node.set_model2_gravity(m_compute_pipeline_settings.model2_gravity);
        area_of_influence_node.set_model2_mass(m_compute_pipeline_settings.model2_mass);
        area_of_influence_node.set_model2_friction_coeff(m_compute_pipeline_settings.model2_friction_coeff);
        area_of_influence_node.set_model2_drag_coeff(m_compute_pipeline_settings.model2_drag_coeff);
    } else if (m_active_compute_pipeline_type == ComputePipelineType::AVALANCHE_INFLUENCE_AREA) {
        // tile selection
        m_compute_graph->get_node_as<compute::nodes::SelectTilesNode>("select_target_tiles_node")
            .select_tiles_in_world_aabb(m_compute_pipeline_settings.target_region, m_compute_pipeline_settings.target_zoomlevel);

        // data source tile selection
        m_compute_graph->get_node_as<compute::nodes::SelectTilesNode>("select_source_tiles_node")
            .select_tiles_in_world_aabb(m_compute_pipeline_settings.target_region, m_compute_pipeline_settings.source_zoomlevel);

        // area of influence settings
        auto& area_of_influence_node = m_compute_graph->get_node_as<compute::nodes::ComputeAvalancheTrajectoriesNode>("compute_area_of_influence_node");
        area_of_influence_node.set_reference_point_world(m_compute_pipeline_settings.reference_point);
        area_of_influence_node.set_target_point_world(m_compute_pipeline_settings.target_point);
        area_of_influence_node.set_num_steps(m_compute_pipeline_settings.num_steps);
        area_of_influence_node.set_step_length(m_compute_pipeline_settings.steps_length);
        area_of_influence_node.set_radius(m_compute_pipeline_settings.radius);
        area_of_influence_node.set_source_zoomlevel(m_compute_pipeline_settings.source_zoomlevel);
        area_of_influence_node.set_physics_model_type(m_compute_pipeline_settings.model_type);
        area_of_influence_node.set_model1_downward_acceleration_coeff(m_compute_pipeline_settings.model1_gradient_coeff);
        area_of_influence_node.set_model1_linear_drag_coeff(m_compute_pipeline_settings.model1_velocity_coeff);
        area_of_influence_node.set_model2_gravity(m_compute_pipeline_settings.model2_gravity);
        area_of_influence_node.set_model2_mass(m_compute_pipeline_settings.model2_mass);
        area_of_influence_node.set_model2_friction_coeff(m_compute_pipeline_settings.model2_friction_coeff);
        area_of_influence_node.set_model2_drag_coeff(m_compute_pipeline_settings.model2_drag_coeff);
    }
}

void Window::recreate_and_rerun_compute_pipeline()
{
    create_and_set_compute_pipeline(m_active_compute_pipeline_type);
    update_compute_pipeline_settings();
    if (m_compute_pipeline_settings.is_region_select) {
        m_compute_graph->run();
    }
}

float Window::depth([[maybe_unused]] const glm::dvec2& normalised_device_coordinates)
{
    auto position = synchronous_position_readback(normalised_device_coordinates);
    return position.z;
}

glm::dvec3 Window::position([[maybe_unused]] const glm::dvec2& normalised_device_coordinates)
{
    // If we read position directly no reconstruction is necessary
    // glm::dvec3 reconstructed = m_camera.position() + m_camera.ray_direction(normalised_device_coordinates) * (double)depth(normalised_device_coordinates);
    auto position = synchronous_position_readback(normalised_device_coordinates);
    return m_camera.position() + glm::dvec3(position.x, position.y, position.z);
}

void Window::destroy()
{
    m_pipeline_manager->release_pipelines();
    m_shader_manager->release_shader_modules();
    emit gpu_ready_changed(false);
}

void Window::set_aabb_decorator(const nucleus::tile_scheduler::utils::AabbDecoratorPtr& aabb_decorator) { m_tile_manager->set_aabb_decorator(aabb_decorator); }

void Window::set_quad_limit(unsigned int new_limit) { m_tile_manager->set_quad_limit(new_limit); }

nucleus::camera::AbstractDepthTester* Window::depth_tester()
{
    // Return this object as the depth tester
    return this;
}

nucleus::utils::ColourTexture::Format Window::ortho_tile_compression_algorithm() const
{
    // TODO use compressed textures in the future
    return nucleus::utils::ColourTexture::Format::Uncompressed_RGBA;
}

void Window::set_permissible_screen_space_error([[maybe_unused]] float new_error)
{
    // Logic for setting permissible screen space error, parameter currently unused
}

void Window::update_camera([[maybe_unused]] const nucleus::camera::Definition& new_definition)
{
    // NOTE: Could also just be done on camera or viewport change!
    uboCameraConfig* cc = &m_camera_config_ubo->data;
    cc->position = glm::vec4(new_definition.position(), 1.0);
    cc->view_matrix = new_definition.local_view_matrix();
    cc->proj_matrix = new_definition.projection_matrix();
    cc->view_proj_matrix = cc->proj_matrix * cc->view_matrix;
    cc->inv_view_proj_matrix = glm::inverse(cc->view_proj_matrix);
    cc->inv_view_matrix = glm::inverse(cc->view_matrix);
    cc->inv_proj_matrix = glm::inverse(cc->proj_matrix);
    cc->viewport_size = new_definition.viewport_size();
    cc->distance_scaling_factor = new_definition.distance_scale_factor();
    m_camera_config_ubo->update_gpu_data(m_queue);
    m_camera = new_definition;

    m_needs_redraw = true;
}

void Window::update_debug_scheduler_stats([[maybe_unused]] const QString& stats)
{
    // Logic for updating debug scheduler stats, parameter currently unused
}

void Window::update_gpu_quads([[maybe_unused]] const std::vector<nucleus::tile_scheduler::tile_types::GpuTileQuad>& new_quads,
    [[maybe_unused]] const std::vector<tile::Id>& deleted_quads)
{
    // std::cout << "received " << new_quads.size() << " new quads, should delete " << deleted_quads.size() << " quads" << std::endl;
    m_tile_manager->update_gpu_quads(new_quads, deleted_quads);
    m_needs_redraw = true;
}

void Window::request_redraw() { m_needs_redraw = true; }

void Window::load_track_and_focus(const std::string& path)
{
    std::vector<glm::dvec3> points;

    std::unique_ptr<nucleus::track::Gpx> gpx_track = nucleus::track::parse(QString::fromStdString(path));
    for (const auto& segment : gpx_track->track) {
        points.reserve(points.size() + segment.size());
        for (const auto& point : segment) {
            points.push_back({ point.latitude, point.longitude, point.elevation });
        }
    }
    m_track_renderer->add_track(points);

    const auto track_aabb = nucleus::track::compute_world_aabb(*gpx_track);
    const auto aabb_size = track_aabb.size();

    // add debug axis
    std::vector<glm::vec4> x_axis = { glm::vec4(track_aabb.min, 1), glm::vec4(track_aabb.max.x, track_aabb.min.y, track_aabb.min.z, 1) };
    std::vector<glm::vec4> y_axis = { glm::vec4(track_aabb.min, 1), glm::vec4(track_aabb.min.x, track_aabb.max.y, track_aabb.min.z, 1) };
    std::vector<glm::vec4> z_axis = { glm::vec4(track_aabb.min, 1), glm::vec4(track_aabb.min.x, track_aabb.min.y, track_aabb.max.z, 1) };
    m_track_renderer->add_world_positions(x_axis, { 1.0f, 0.0f, 0.0f, 1.0f });
    m_track_renderer->add_world_positions(y_axis, { 0.0f, 1.0f, 0.0f, 1.0f });
    m_track_renderer->add_world_positions(z_axis, { 0.0f, 0.0f, 1.0f, 1.0f });

    nucleus::camera::Definition new_camera_definition = { track_aabb.centre() + glm::dvec3 { 0, 0, std::max(aabb_size.x, aabb_size.y) }, track_aabb.centre() };
    new_camera_definition.set_viewport_size(m_camera.viewport_size());

    // update pipeline settings
    m_compute_pipeline_settings.is_region_select = true;
    m_compute_pipeline_settings.target_region = track_aabb;
    if (m_compute_graph->exists_node("compute_area_of_influence_node")) {
        m_compute_pipeline_settings.reference_point = track_aabb.min;

        // for now simply always select point in middle of first segment
        const auto& coords = gpx_track->track.at(0).at(gpx_track->track.at(0).size() / 2);
        m_compute_pipeline_settings.target_point = nucleus::srs::lat_long_to_world({ coords.latitude, coords.longitude });
    }
    update_compute_pipeline_settings();

    emit set_camera_definition_requested(new_camera_definition);

    if (m_shared_config_ubo->data.m_track_render_mode == 0) {
        m_shared_config_ubo->data.m_track_render_mode = 1;
    }
    m_needs_redraw = true;
}

void Window::reload_shaders()
{
    qDebug() << "reloading shaders...";
    m_shader_manager->release_shader_modules();
    m_shader_manager->create_shader_modules();
    m_pipeline_manager->release_pipelines();
    m_pipeline_manager->create_pipelines();
    qDebug() << "reloading shaders done";
    request_redraw();
}

void Window::create_buffers()
{
    m_shared_config_ubo = std::make_unique<Buffer<uboSharedConfig>>(m_device, WGPUBufferUsage_CopyDst | WGPUBufferUsage_Uniform);
    m_camera_config_ubo = std::make_unique<Buffer<uboCameraConfig>>(m_device, WGPUBufferUsage_CopyDst | WGPUBufferUsage_Uniform);
    m_position_readback_buffer = std::make_unique<webgpu::raii::RawBuffer<glm::vec4>>(
        m_device, WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead, 256 / sizeof(glm::vec4), "position readback buffer");
}

void Window::create_bind_groups()
{
    m_shared_config_bind_group = std::make_unique<webgpu::raii::BindGroup>(m_device, m_pipeline_manager->shared_config_bind_group_layout(),
        std::initializer_list<WGPUBindGroupEntry> { m_shared_config_ubo->raw_buffer().create_bind_group_entry(0) });

    m_camera_bind_group = std::make_unique<webgpu::raii::BindGroup>(m_device, m_pipeline_manager->camera_bind_group_layout(),
        std::initializer_list<WGPUBindGroupEntry> { m_camera_config_ubo->raw_buffer().create_bind_group_entry(0) });
}

void Window::update_required_gpu_limits(WGPULimits& limits, const WGPULimits& supported_limits)
{
    const uint32_t max_required_bind_groups = 4u;
    const uint32_t min_recommended_max_texture_array_layers = 1024u;
    const uint32_t min_required_max_color_attachment_bytes_per_sample = 32u;
    const uint64_t min_required_max_storage_buffer_binding_size = 268435456u;

    if (supported_limits.maxColorAttachmentBytesPerSample < min_required_max_color_attachment_bytes_per_sample) {
        qFatal() << "Minimum supported maxColorAttachmentBytesPerSample needs to be >=" << min_required_max_color_attachment_bytes_per_sample;
    }
    if (supported_limits.maxTextureArrayLayers < min_recommended_max_texture_array_layers) {
        qWarning() << "Minimum supported maxTextureArrayLayers is " << supported_limits.maxTextureArrayLayers << " ("
                   << min_recommended_max_texture_array_layers << " recommended)!";
    }
    if (supported_limits.maxBindGroups < max_required_bind_groups) {
        qFatal() << "Maximum supported number of bind groups is " << supported_limits.maxBindGroups << " and " << max_required_bind_groups << " are required";
    }
    if (supported_limits.maxStorageBufferBindingSize < min_required_max_storage_buffer_binding_size) {
        qFatal() << "Maximum supported storage buffer binding size is " << supported_limits.maxStorageBufferBindingSize << " and "
                 << min_required_max_storage_buffer_binding_size << " is required";
    }
    limits.maxBindGroups = std::max(limits.maxBindGroups, max_required_bind_groups);
    limits.maxColorAttachmentBytesPerSample = std::max(limits.maxColorAttachmentBytesPerSample, min_required_max_color_attachment_bytes_per_sample);
    limits.maxTextureArrayLayers
        = std::min(std::max(limits.maxTextureArrayLayers, min_recommended_max_texture_array_layers), supported_limits.maxTextureArrayLayers);
    limits.maxStorageBufferBindingSize = std::max(limits.maxStorageBufferBindingSize, supported_limits.maxStorageBufferBindingSize);
}

} // namespace webgpu_engine
