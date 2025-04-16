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
#include "compute/nodes/ComputeReleasePointsNode.h"
#include "compute/nodes/ComputeSnowNode.h"
#include "compute/nodes/LoadRegionAabbNode.h"
#include "compute/nodes/LoadTextureNode.h"
#include "compute/nodes/SelectTilesNode.h"
#include "nucleus/track/GPX.h"
#include "nucleus/utils/image_loader.h"
#include "webgpu/raii/RenderPassEncoder.h"
#include "webgpu_engine/Context.h"
#include <QFile>

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
{
#ifdef __EMSCRIPTEN__
    connect(&WebInterop::instance(), &WebInterop::file_uploaded, this, &Window::file_upload_handler);
#endif
}

Window::~Window()
{
    // Destructor cleanup logic here
}

void Window::set_wgpu_context(WGPUInstance instance, WGPUDevice device, WGPUAdapter adapter, WGPUSurface surface, WGPUQueue queue, Context* context)
{
    m_instance = instance;
    m_device = device;
    m_adapter = adapter;
    m_surface = surface;
    m_queue = queue;
    m_context = context;
}

void Window::initialise_gpu()
{
    assert(m_device != nullptr); // just make sure that wgpu context is set

    create_buffers();
    create_bind_groups();

    m_track_renderer = std::make_unique<TrackRenderer>(m_device, *m_context->pipeline_manager());

    m_image_overlay_settings_uniform_buffer = std::make_unique<Buffer<ImageOverlaySettings>>(m_device, WGPUBufferUsage_CopyDst | WGPUBufferUsage_Uniform);
    m_image_overlay_settings_uniform_buffer->data.aabb_min = glm::fvec2(0);
    m_image_overlay_settings_uniform_buffer->data.aabb_max = glm::fvec2(0);
    m_image_overlay_settings_uniform_buffer->update_gpu_data(m_queue);
    m_image_overlay_texture = create_overlay_texture(1, 1);

    m_compute_overlay_settings_uniform_buffer = std::make_unique<Buffer<ImageOverlaySettings>>(m_device, WGPUBufferUsage_CopyDst | WGPUBufferUsage_Uniform);
    m_compute_overlay_settings_uniform_buffer->data.aabb_min = glm::fvec2(0);
    m_compute_overlay_settings_uniform_buffer->data.aabb_max = glm::fvec2(0);
    m_compute_overlay_settings_uniform_buffer->data.mode = 0u;
    m_compute_overlay_settings_uniform_buffer->data.alpha = 1.0f;
    m_compute_overlay_settings_uniform_buffer->update_gpu_data(m_queue);
    m_compute_overlay_dummy_texture = create_overlay_texture(1, 1);

    init_compute_pipeline_presets();
    create_and_set_compute_pipeline(ComputePipelineType::AVALANCHE_TRAJECTORIES, false);

    qInfo() << "gpu_ready_changed";
    // emit gpu_ready_changed(true); //TODO remove/find replacement
}

void Window::resize_framebuffer(int w, int h)
{
    m_swapchain_size = glm::vec2(w, h);

    m_gbuffer_format = webgpu::FramebufferFormat(m_context->pipeline_manager()->render_tiles_pipeline().framebuffer_format());
    m_gbuffer_format.size = glm::uvec2 { w, h };
    m_gbuffer = std::make_unique<webgpu::Framebuffer>(m_device, m_gbuffer_format);

    webgpu::FramebufferFormat atmosphere_framebuffer_format(m_context->pipeline_manager()->render_atmosphere_pipeline().framebuffer_format());
    atmosphere_framebuffer_format.size = glm::uvec2(1, h);
    m_atmosphere_framebuffer = std::make_unique<webgpu::Framebuffer>(m_device, atmosphere_framebuffer_format);

    recreate_compose_bind_group();

    m_depth_texture_bind_group = std::make_unique<webgpu::raii::BindGroup>(m_device,
        m_context->pipeline_manager()->depth_texture_bind_group_layout(),
        std::initializer_list<WGPUBindGroupEntry> {
            m_gbuffer->depth_texture_view().create_bind_group_entry(0), // depth
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
    // emit update_camera_requested(); //TODO remove/find replacement

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
        wgpuRenderPassEncoderSetPipeline(render_pass->handle(), m_context->pipeline_manager()->render_atmosphere_pipeline().pipeline().handle());
        wgpuRenderPassEncoderDraw(render_pass->handle(), 3, 1, 0, 0);
    }

    // render tiles to geometry buffers
    {
        std::unique_ptr<webgpu::raii::RenderPassEncoder> render_pass = m_gbuffer->begin_render_pass(command_encoder);
        wgpuRenderPassEncoderSetBindGroup(render_pass->handle(), 0, m_shared_config_bind_group->handle(), 0, nullptr);
        wgpuRenderPassEncoderSetBindGroup(render_pass->handle(), 1, m_camera_bind_group->handle(), 0, nullptr);

        const auto tile_set = m_context->tile_geometry()->generate_tilelist(m_camera);
        const auto culled_tile_set = m_context->tile_geometry()->cull(tile_set, m_camera.frustum());
        m_context->tile_geometry()->draw(render_pass->handle(), m_camera, culled_tile_set, true, m_camera.position());
    }

    // render geometry buffers to target framebuffer
    {
        std::unique_ptr<webgpu::raii::RenderPassEncoder> render_pass = framebuffer->begin_render_pass(command_encoder);
        wgpuRenderPassEncoderSetPipeline(render_pass->handle(), m_context->pipeline_manager()->compose_pipeline().pipeline().handle());
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

    if (m_first_paint) {
        after_first_frame();
    }
    m_needs_redraw = false;
    m_first_paint = false;
}

void Window::paint_gui()
{
#ifdef ALP_WEBGPU_APP_ENABLE_IMGUI

    if (ImGui::Combo("Normal Mode", (int*)&m_shared_config_ubo->data.m_normal_mode, "None\0Flat\0Smooth\0\0")) {
        m_needs_redraw = true;
    }
    {
        static int currentItem = 0;
        static const std::vector<std::pair<std::string, int>> overlays
            = { { "None", 0 }, { "Normals", 1 }, { "Tiles", 2 }, { "Zoomlevel", 3 }, { "Vertex-ID", 4 }, { "Vertex Height-Sample", 5 },
                  { "Decoded Normals", 100 }, { "Steepness", 101 }, { "SSAO Buffer", 102 }, { "Shadow Cascades", 103 } };
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

        if (m_shared_config_ubo->data.m_overlay_mode > 0) {
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

    if (ImGui::CollapsingHeader("Image overlay")) {
        if (ImGui::Button("Open overlay image file ...", ImVec2(350, 20))) {
#ifdef __EMSCRIPTEN__
            WebInterop::instance().open_file_dialog(".png", "overlay_png");
#else
            IGFD::FileDialogConfig config;
            config.path = ".";
            ImGuiFileDialog::Instance()->OpenDialog("OverlayImageFileDialog", "Choose File", ".png,.*", config);
#endif
        }

#ifndef __EMSCRIPTEN__
        if (ImGuiFileDialog::Instance()->Display("OverlayImageFileDialog")) {
            if (ImGuiFileDialog::Instance()->IsOk()) { // action if OK
                std::string filename_str = ImGuiFileDialog::Instance()->GetFilePathName();
                auto filename = std::filesystem::path(filename_str);

                // Construct the default AABB file path
                auto aabb_filepath = filename.parent_path() / (filename.stem().string() + "_aabb.txt");

                // If the default AABB file does not exist, try with the trackname
                if (!std::filesystem::exists(aabb_filepath)) {
                    // Extract trackname from the filename until the first '_'
                    std::string filename_stem = filename.stem().string();
                    size_t underscore_pos = filename_stem.find('_');
                    std::string trackname = (underscore_pos != std::string::npos) ? filename_stem.substr(0, underscore_pos) // Before the first '_'
                                                                                  : filename_stem; // Entire stem if no '_'

                    // Construct the new AABB file path using trackname
                    aabb_filepath = filename.parent_path() / (trackname + "_aabb.txt");
                }

                // If the AABB file exists, call the appropriate functions
                if (std::filesystem::exists(aabb_filepath)) {
                    update_image_overlay_texture(filename_str);
                    update_image_overlay_aabb_and_focus(aabb_filepath.string());
                    m_needs_redraw = true;
                } else {
                    qCritical() << "No AABB file found for image overlay.";
                }
            }
            ImGuiFileDialog::Instance()->Close();
        }
#endif

#ifdef __EMSCRIPTEN__
        // NOTE: In the web we can't check the filesystem for the aabb file so the user has to open it separately
        if (ImGui::Button("Open overlay aabb file ...", ImVec2(350, 20))) {
            WebInterop::instance().open_file_dialog(".txt", "overlay_aabb_txt");
        }
#endif
        if (ImGui::SliderFloat("Strength##image overlay", &m_image_overlay_settings_uniform_buffer->data.alpha, 0.0f, 1.0f, "%.2f")) {
            m_image_overlay_settings_uniform_buffer->update_gpu_data(m_queue);
            m_needs_redraw = true;
        }

        if (ImGui::Combo("Mode##image overlay", (int*)&(m_image_overlay_settings_uniform_buffer->data.mode), "Alpha-Blend\0Encoded Float\0")) {
            m_image_overlay_settings_uniform_buffer->update_gpu_data(m_queue);
            m_needs_redraw = true;
        }

        if (m_image_overlay_settings_uniform_buffer->data.mode == 1) {
            if (ImGui::DragFloatRange2("Float Map Range", &m_image_overlay_settings_uniform_buffer->data.float_decoding_lower_bound,
                    &m_image_overlay_settings_uniform_buffer->data.float_decoding_upper_bound, 1.0f, -10000, 10000)) {
                m_image_overlay_settings_uniform_buffer->update_gpu_data(m_queue);
                m_needs_redraw = true;
            }
        }
    }

    if (ImGui::CollapsingHeader("Track", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Button("Open GPX file ...", ImVec2(250, 20))) {
#ifdef __EMSCRIPTEN__
            WebInterop::instance().open_file_dialog(".gpx", "track");
#else
            IGFD::FileDialogConfig config;
            config.path = ".";
            ImGuiFileDialog::Instance()->OpenDialog("ChooseFileDlgKey", "Choose File", ".gpx,.*", config);
#endif
        }
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(106 / 255.0f, 112 / 255.0f, 115 / 255.0f, 1.00f));
        if (ImGui::Button("Open Preset ...", ImVec2(100, 20))) {
            load_track_and_focus(DEFAULT_GPX_TRACK_PATH);
        }
        ImGui::PopStyleColor(1);

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

    if (m_gui_error_state.should_open_modal) {
        ImGui::OpenPopup("Error");
        m_gui_error_state.should_open_modal = false;
    }

    // Always center this window when appearing
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal("Error", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {

        ImGui::PushTextWrapPos(30.0f * ImGui::GetFontSize());
        ImGui::Text("%s", m_gui_error_state.text.c_str());
        ImGui::PopTextWrapPos();

        ImGui::Separator();
        if (ImGui::Button("OK", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

#endif
}

void Window::paint_compute_pipeline_gui()
{
#if ALP_WEBGPU_APP_ENABLE_IMGUI
    if (ImGui::CollapsingHeader("Compute pipeline", ImGuiTreeNodeFlags_DefaultOpen)) {

        if (ImGui::Button("Run", ImVec2(250, 20))) {
            if (m_is_region_selected) {
                m_compute_graph->run();
            }
        }

        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(150 / 255.0f, 10 / 255.0f, 10 / 255.0f, 1.00f));
        if (ImGui::Button("Clear", ImVec2(100, 20))) {
            create_and_set_compute_pipeline(m_active_compute_pipeline_type);
            m_needs_redraw = true;
        }
        ImGui::PopStyleColor(1);

        if (ImGui::SliderFloat("Strength##compute overlay", &m_compute_overlay_settings_uniform_buffer->data.alpha, 0.0f, 1.0f, "%.2f")) {
            m_compute_overlay_settings_uniform_buffer->update_gpu_data(m_queue);
            m_needs_redraw = true;
        }

        const char* tile_source_items = "DTM tiles\0DSM tiles\0";
        if (ImGui::Combo("Tile source", &m_compute_pipeline_settings.tile_source_index, tile_source_items)) {
            update_settings_and_rerun_pipeline();
            m_needs_redraw = true;
        }

        const uint32_t min_zoomlevel = 1;
        const uint32_t max_zoomlevel = 18;
        ImGui::SliderScalar("Zoom level", ImGuiDataType_U32, &m_compute_pipeline_settings.zoomlevel, &min_zoomlevel, &max_zoomlevel, "%u");
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            update_settings_and_rerun_pipeline();
        }

        static int overlays_current_item = 2;
        const std::vector<std::pair<std::string, ComputePipelineType>> overlays = {
            { "Normals", ComputePipelineType::NORMALS },
            { "Snow + Normals", ComputePipelineType::NORMALS_AND_SNOW },
            { "Avalanche trajectories", ComputePipelineType::AVALANCHE_TRAJECTORIES },
            { "Avalanche trajectories (eval)", ComputePipelineType::AVALANCHE_TRAJECTORIES_EVAL },
            { "D8 directions", ComputePipelineType::D8_DIRECTIONS },
            { "Release points", ComputePipelineType::RELEASE_POINTS },
            { "Iterative simulation (WIP)", ComputePipelineType::ITERATIVE_SIMULATION },
        };
        const char* current_item_label = overlays[overlays_current_item].first.c_str();
        if (ImGui::BeginCombo("Type", current_item_label)) {
            for (size_t i = 0; i < overlays.size(); i++) {
                bool is_selected = ((size_t)overlays_current_item == i);
                if (ImGui::Selectable(overlays[i].first.c_str(), is_selected)) {
                    overlays_current_item = i;
                    create_and_set_compute_pipeline(overlays[i].second);
                }
                if (is_selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        if (ImGui::TreeNodeEx("Pipeline-specific settings", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::PushItemWidth(15.0f * ImGui::GetFontSize());
            if (m_active_compute_pipeline_type == ComputePipelineType::AVALANCHE_TRAJECTORIES) {

                // names must align with contents of presets array (class member)
                // TODO refactor
                static int current_preset_index = 0;
                const std::vector<std::string> presets = { "Default values", "Preset A", "Preset B" };
                const char* current_preset_item_label = presets[current_preset_index].c_str();
                if (ImGui::BeginCombo("Preset", current_preset_item_label)) {
                    for (size_t i = 0; i < presets.size(); i++) {
                        bool is_selected = ((size_t)current_preset_index == i);
                        if (ImGui::Selectable(presets[i].c_str(), is_selected)) {
                            current_preset_index = i;
                            apply_compute_pipeline_preset(current_preset_index);
                            update_settings_and_rerun_pipeline();
                        }
                        if (is_selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }

                const uint32_t min_resolution_multiplier = 1;
                const uint32_t max_resolution_multiplier = 32;
                ImGui::SliderScalar("Resolution", ImGuiDataType_U32, &m_compute_pipeline_settings.trajectory_resolution_multiplier, &min_resolution_multiplier,
                    &max_resolution_multiplier, "%ux");
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    update_settings_and_rerun_pipeline();
                }

                ImGui::SliderInt("Release point interval", &m_compute_pipeline_settings.release_point_interval, 1, 64, "%u");
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    update_settings_and_rerun_pipeline();
                }

                ImGui::DragFloatRange2("Release point steepness", &m_compute_pipeline_settings.trigger_point_min_slope_angle,
                    &m_compute_pipeline_settings.trigger_point_max_slope_angle, 0.1f, 0.0f, 90.0f, "Min: %.1f°", "Max: %.1f°", ImGuiSliderFlags_AlwaysClamp);
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    update_settings_and_rerun_pipeline();
                }

                const uint32_t min_steps = 1;
                const uint32_t max_steps = 1024;
                ImGui::DragScalar("Num steps", ImGuiDataType_U32, &m_compute_pipeline_settings.num_steps, 1.0f, &min_steps, &max_steps, "%u");
                // ImGui::SliderScalar("Num steps", ImGuiDataType_U32, &m_compute_pipeline_settings.num_steps, &min_steps, &max_steps, "%u");
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    update_settings_and_rerun_pipeline();
                }

                const uint32_t min_num_samples = 1;
                const uint32_t max_num_samples = 1024;
                ImGui::DragScalar("Paths per release point", ImGuiDataType_U32, &m_compute_pipeline_settings.num_paths_per_release_cell, 1.0f, &min_num_samples,
                    &max_num_samples, "%u");
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    update_settings_and_rerun_pipeline();
                }

                ImGui::DragFloat("Random contribution", &m_compute_pipeline_settings.random_contribution, 0.01f, 0.0f, 1.0f, "%.3f");
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    update_settings_and_rerun_pipeline();
                }

                ImGui::DragFloat("Persistence", &m_compute_pipeline_settings.persistence_contribution, 0.01f, 0.0f, 1.0f, "%.3f");
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    update_settings_and_rerun_pipeline();
                }

                static int runout_models_current_item = 1;
                const std::vector<std::pair<std::string, int>> runout_models = {
                    { "None", 0 },
                    { "FlowPy (Alpha)", 2 },
                };
                const char* current_item_label = runout_models[runout_models_current_item].first.c_str();
                if (ImGui::BeginCombo("Runout model", current_item_label)) {
                    for (size_t i = 0; i < runout_models.size(); i++) {
                        bool is_selected = ((size_t)runout_models_current_item == i);
                        if (ImGui::Selectable(runout_models[i].first.c_str(), is_selected)) {
                            runout_models_current_item = i;
                            m_compute_pipeline_settings.runout_model_type = runout_models[i].second;
                            update_settings_and_rerun_pipeline();
                        }
                        if (is_selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }

                if (m_compute_pipeline_settings.runout_model_type == compute::nodes::ComputeAvalancheTrajectoriesNode::RunoutModelType::PERLA) {
                    ImGui::SliderFloat("My##runout_perla", &m_compute_pipeline_settings.perla.my, 0.004f, 0.6f, "%.2f");
                    if (ImGui::IsItemDeactivatedAfterEdit()) {
                        update_settings_and_rerun_pipeline();
                    }
                    ImGui::SliderFloat("M/D##runout_perla", &m_compute_pipeline_settings.perla.md, 20.0f, 150.0f, "%.2f");
                    if (ImGui::IsItemDeactivatedAfterEdit()) {
                        update_settings_and_rerun_pipeline();
                    }
                    ImGui::SliderFloat("L##runout_perla", &m_compute_pipeline_settings.perla.l, 1.0f, 15.0f, "%.2f");
                    if (ImGui::IsItemDeactivatedAfterEdit()) {
                        update_settings_and_rerun_pipeline();
                    }
                    ImGui::SliderFloat("Gravity##runout_perla", &m_compute_pipeline_settings.perla.g, 0.0f, 15.0f, "%.2f");
                    if (ImGui::IsItemDeactivatedAfterEdit()) {
                        update_settings_and_rerun_pipeline();
                    }
                } else if (m_compute_pipeline_settings.runout_model_type == compute::nodes::ComputeAvalancheTrajectoriesNode::RunoutModelType::FLOWPY) {
                    ImGui::DragFloat("Alpha##runout_flowpy", &m_compute_pipeline_settings.runout_flowpy_alpha, 0.01f, 0.0f, 90.0f, "%.2f");
                    if (ImGui::IsItemDeactivatedAfterEdit()) {
                        update_settings_and_rerun_pipeline();
                    }
                }
            } else if (m_active_compute_pipeline_type == ComputePipelineType::AVALANCHE_TRAJECTORIES_EVAL) {

                const uint32_t min_resolution_multiplier = 1;
                const uint32_t max_resolution_multiplier = 32;
                ImGui::SliderScalar("Resolution", ImGuiDataType_U32, &m_compute_pipeline_settings.trajectory_resolution_multiplier, &min_resolution_multiplier, &max_resolution_multiplier, "%ux");
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    update_settings_and_rerun_pipeline();
                }

                const uint32_t min_steps = 1;
                const uint32_t max_steps = 1024;
                ImGui::DragScalar("Num steps", ImGuiDataType_U32, &m_compute_pipeline_settings.num_steps, 1.0f, &min_steps, &max_steps, "%u");
                // ImGui::SliderScalar("Num steps", ImGuiDataType_U32, &m_compute_pipeline_settings.num_steps, &min_steps, &max_steps, "%u");
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    update_settings_and_rerun_pipeline();
                }

                const uint32_t min_num_samples = 1;
                const uint32_t max_num_samples = 1024;
                ImGui::DragScalar("Paths per release point", ImGuiDataType_U32, &m_compute_pipeline_settings.num_paths_per_release_cell, 1.0f, &min_num_samples, &max_num_samples, "%u");
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    update_settings_and_rerun_pipeline();
                }

                ImGui::DragFloat("Random contribution", &m_compute_pipeline_settings.random_contribution, 0.01f, 0.0f, 1.0f, "%.3f");
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    update_settings_and_rerun_pipeline();
                }

                ImGui::DragFloat("Persistence", &m_compute_pipeline_settings.persistence_contribution, 0.01f, 0.0f, 1.0f, "%.3f");
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    update_settings_and_rerun_pipeline();
                }

                static int runout_models_current_item = 1;
                const std::vector<std::pair<std::string, int>> runout_models = {
                    { "None", 0 },
                    { "FlowPy (Alpha)", 2 },
                };
                const char* current_item_label = runout_models[runout_models_current_item].first.c_str();
                if (ImGui::BeginCombo("Runout model", current_item_label)) {
                    for (size_t i = 0; i < runout_models.size(); i++) {
                        bool is_selected = ((size_t)runout_models_current_item == i);
                        if (ImGui::Selectable(runout_models[i].first.c_str(), is_selected)) {
                            runout_models_current_item = i;
                            m_compute_pipeline_settings.runout_model_type = runout_models[i].second;
                            update_settings_and_rerun_pipeline();
                        }
                        if (is_selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }

                if (m_compute_pipeline_settings.runout_model_type == compute::nodes::ComputeAvalancheTrajectoriesNode::RunoutModelType::PERLA) {
                    ImGui::SliderFloat("My##runout_perla", &m_compute_pipeline_settings.perla.my, 0.004f, 0.6f, "%.2f");
                    if (ImGui::IsItemDeactivatedAfterEdit()) {
                        update_settings_and_rerun_pipeline();
                    }
                    ImGui::SliderFloat("M/D##runout_perla", &m_compute_pipeline_settings.perla.md, 20.0f, 150.0f, "%.2f");
                    if (ImGui::IsItemDeactivatedAfterEdit()) {
                        update_settings_and_rerun_pipeline();
                    }
                    ImGui::SliderFloat("L##runout_perla", &m_compute_pipeline_settings.perla.l, 1.0f, 15.0f, "%.2f");
                    if (ImGui::IsItemDeactivatedAfterEdit()) {
                        update_settings_and_rerun_pipeline();
                    }
                    ImGui::SliderFloat("Gravity##runout_perla", &m_compute_pipeline_settings.perla.g, 0.0f, 15.0f, "%.2f");
                    if (ImGui::IsItemDeactivatedAfterEdit()) {
                        update_settings_and_rerun_pipeline();
                    }
                } else if (m_compute_pipeline_settings.runout_model_type == compute::nodes::ComputeAvalancheTrajectoriesNode::RunoutModelType::FLOWPY) {
                    ImGui::DragFloat("Alpha##runout_flowpy", &m_compute_pipeline_settings.runout_flowpy_alpha, 0.01f, 0.0f, 90.0f, "%.2f");
                    if (ImGui::IsItemDeactivatedAfterEdit()) {
                        update_settings_and_rerun_pipeline();
                    }
                }
#ifndef __EMSCRIPTEN__
                if (ImGui::Button("Open release points file ...", ImVec2(250, 20))) {
                    IGFD::FileDialogConfig config_release_points_file_dialog;
                    config_release_points_file_dialog.path = ".";
                    ImGuiFileDialog::Instance()->OpenDialog("EvalReleasePointsFileDialog", "Choose File", ".png,.*", config_release_points_file_dialog);
                }
                if (ImGui::Button("Open height map file ...", ImVec2(250, 20))) {
                    IGFD::FileDialogConfig config_heightmap_file_dialog;
                    config_heightmap_file_dialog.path = ".";
                    ImGuiFileDialog::Instance()->OpenDialog("EvalHeightmapFileDialog", "Choose File", ".png,.*", config_heightmap_file_dialog);
                }
                if (ImGui::Button("Open AABB file ...", ImVec2(250, 20))) {
                    IGFD::FileDialogConfig config_aabb_file_dialog;
                    config_aabb_file_dialog.path = ".";
                    ImGuiFileDialog::Instance()->OpenDialog("EvalAabbFileDialog", "Choose File", ".txt,.*", config_aabb_file_dialog);
                }
#endif
#ifndef __EMSCRIPTEN__
                if (ImGuiFileDialog::Instance()->Display("EvalReleasePointsFileDialog")) {
                    if (ImGuiFileDialog::Instance()->IsOk()) { // action if OK
                        std::string file_path = ImGuiFileDialog::Instance()->GetFilePathName();
                        m_compute_pipeline_settings.release_points_texture_path = file_path;
                        update_compute_pipeline_settings();
                    }
                    ImGuiFileDialog::Instance()->Close();
                }

                if (ImGuiFileDialog::Instance()->Display("EvalHeightmapFileDialog")) {
                    if (ImGuiFileDialog::Instance()->IsOk()) { // action if OK
                        std::string file_path = ImGuiFileDialog::Instance()->GetFilePathName();
                        m_compute_pipeline_settings.heightmap_texture_path = file_path;
                        update_compute_pipeline_settings();
                    }
                    ImGuiFileDialog::Instance()->Close();
                }

                if (ImGuiFileDialog::Instance()->Display("EvalAabbFileDialog")) {
                    if (ImGuiFileDialog::Instance()->IsOk()) { // action if OK
                        std::string file_path = ImGuiFileDialog::Instance()->GetFilePathName();
                        m_compute_pipeline_settings.aabb_file_path = file_path;
                        update_compute_pipeline_settings();
                    }
                    ImGuiFileDialog::Instance()->Close();
                }
#endif

            } else if (m_active_compute_pipeline_type == ComputePipelineType::NORMALS_AND_SNOW) {
                if (ImGui::Checkbox("Sync with render settings", &m_compute_pipeline_settings.sync_snow_settings_with_render_settings)) {
                    update_compute_pipeline_settings();
                }

                if (!m_compute_pipeline_settings.sync_snow_settings_with_render_settings) {
                    if (ImGui::DragFloatRange2("Angle limit##compute",
                            &m_compute_pipeline_settings.snow_settings.angle.y,
                            &m_compute_pipeline_settings.snow_settings.angle.z,
                            0.1f,
                            0.0f,
                            90.0f,
                            "Min: %.1f°",
                            "Max: %.1f°",
                            ImGuiSliderFlags_AlwaysClamp)) {
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
            } else if (m_active_compute_pipeline_type == ComputePipelineType::RELEASE_POINTS) {
                ImGui::SliderInt("Release point interval", &m_compute_pipeline_settings.release_point_interval, 1, 64, "%u");
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    update_settings_and_rerun_pipeline();
                }

                ImGui::DragFloatRange2("Release point steepness",
                    &m_compute_pipeline_settings.trigger_point_min_slope_angle,
                    &m_compute_pipeline_settings.trigger_point_max_slope_angle,
                    0.1f,
                    0.0f,
                    90.0f,
                    "Min: %.1f°",
                    "Max: %.1f°",
                    ImGuiSliderFlags_AlwaysClamp);
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    update_settings_and_rerun_pipeline();
                }
            } else if (m_active_compute_pipeline_type == ComputePipelineType::ITERATIVE_SIMULATION) {
                // TODO remove duplicate code!

                ImGui::SliderInt("Release point interval##iterative", &m_compute_pipeline_settings.release_point_interval, 1, 64, "%u");
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    update_settings_and_rerun_pipeline();
                }

                ImGui::DragFloatRange2("Release point steepness",
                    &m_compute_pipeline_settings.trigger_point_min_slope_angle,
                    &m_compute_pipeline_settings.trigger_point_max_slope_angle,
                    0.1f,
                    0.0f,
                    90.0f,
                    "Min: %.1f°",
                    "Max: %.1f°",
                    ImGuiSliderFlags_AlwaysClamp);
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    update_settings_and_rerun_pipeline();
                }
            }
            ImGui::PopItemWidth();
            ImGui::TreePop();
        }
    }
#endif
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

void Window::create_and_set_compute_pipeline(ComputePipelineType pipeline_type, bool should_recreate_compose_bind_group)
{
    qDebug() << "setting new compute pipeline " << static_cast<int>(pipeline_type);
    m_active_compute_pipeline_type = pipeline_type;

    if (pipeline_type == ComputePipelineType::NORMALS) {
        m_compute_graph = compute::nodes::NodeGraph::create_normal_compute_graph(*m_context->pipeline_manager(), m_device);
    } else if (pipeline_type == ComputePipelineType::NORMALS_AND_SNOW) {
        m_compute_graph = compute::nodes::NodeGraph::create_normal_with_snow_compute_graph(*m_context->pipeline_manager(), m_device);
    } else if (pipeline_type == ComputePipelineType::AVALANCHE_TRAJECTORIES) {
        m_compute_graph = compute::nodes::NodeGraph::create_trajectories_with_export_compute_graph(*m_context->pipeline_manager(), m_device);
        // m_compute_graph = compute::nodes::NodeGraph::create_fxaa_trajectories_compute_graph(*m_context->pipeline_manager(), m_device);
    } else if (pipeline_type == ComputePipelineType::AVALANCHE_TRAJECTORIES_EVAL) {
        m_compute_graph = compute::nodes::NodeGraph::create_trajectories_evaluation_compute_graph(*m_context->pipeline_manager(), m_device);
    } else if (pipeline_type == ComputePipelineType::D8_DIRECTIONS) {
        m_compute_graph = compute::nodes::NodeGraph::create_d8_compute_graph(*m_context->pipeline_manager(), m_device);
    } else if (pipeline_type == ComputePipelineType::RELEASE_POINTS) {
        m_compute_graph = compute::nodes::NodeGraph::create_release_points_compute_graph(*m_context->pipeline_manager(), m_device);
    } else if (pipeline_type == ComputePipelineType::ITERATIVE_SIMULATION) {
        m_compute_graph = compute::nodes::NodeGraph::create_iterative_simulation_compute_graph(*m_context->pipeline_manager(), m_device);
    }

    update_compute_pipeline_settings();

    connect(m_compute_graph.get(), &compute::nodes::NodeGraph::run_completed, this, &Window::request_redraw);
    connect(m_compute_graph.get(), &compute::nodes::NodeGraph::run_completed, this, &Window::on_pipeline_run_completed);

    connect(m_compute_graph.get(), &compute::nodes::NodeGraph::run_failed, this, [this](compute::nodes::GraphRunFailureInfo info) {
        qWarning() << "graph run failed. " << info.node_name() << ": " << info.node_run_failure_info().message();
        std::string message = "Execution of pipeline failed.\n\nNode \"" + info.node_name() + "\" reported \"" + info.node_run_failure_info().message() + "\"";
        this->display_message(message);
    });

    if (should_recreate_compose_bind_group) {
        // we usually need to recreate the compose bind group, because it might have now-outdated texture bindings from the last (now-destroyed) pipeline
        // however, we dont want this to happen when initializing, because at that point we dont have a gbuffer yet (which is required for creating the bind
        // group)
        clear_compute_overlay();
        recreate_compose_bind_group();
    }
}

void Window::update_compute_pipeline_settings()
{
    if (m_active_compute_pipeline_type == ComputePipelineType::NORMALS || m_active_compute_pipeline_type == ComputePipelineType::RELEASE_POINTS) {
        // tile selection
        m_compute_graph->get_node_as<compute::nodes::SelectTilesNode>("select_tiles_node")
            .select_tiles_in_world_aabb(m_compute_pipeline_settings.target_region, m_compute_pipeline_settings.zoomlevel);

        // tile source
        m_compute_graph->get_node_as<compute::nodes::RequestTilesNode>("request_height_node")
            .set_settings(m_tile_source_settings.at(m_compute_pipeline_settings.tile_source_index));

        if (m_active_compute_pipeline_type == ComputePipelineType::RELEASE_POINTS) {
            compute::nodes::ComputeReleasePointsNode::ReleasePointsSettings settings;
            settings.min_slope_angle = glm::radians(m_compute_pipeline_settings.trigger_point_min_slope_angle);
            settings.max_slope_angle = glm::radians(m_compute_pipeline_settings.trigger_point_max_slope_angle);
            settings.sampling_interval = glm::uvec2(m_compute_pipeline_settings.release_point_interval);
            m_compute_graph->get_node_as<compute::nodes::ComputeReleasePointsNode>("compute_release_points_node").set_settings(settings);
        }

    } else if (m_active_compute_pipeline_type == ComputePipelineType::NORMALS_AND_SNOW) {
        // tile selection
        m_compute_graph->get_node_as<compute::nodes::SelectTilesNode>("select_tiles_node")
            .select_tiles_in_world_aabb(m_compute_pipeline_settings.target_region, m_compute_pipeline_settings.zoomlevel);

        // tile source
        m_compute_graph->get_node_as<compute::nodes::RequestTilesNode>("request_height_node")
            .set_settings(m_tile_source_settings.at(m_compute_pipeline_settings.tile_source_index));

        // snow settings
        if (m_compute_pipeline_settings.sync_snow_settings_with_render_settings) {
            m_compute_pipeline_settings.snow_settings.alt = m_shared_config_ubo->data.m_snow_settings_alt;
            m_compute_pipeline_settings.snow_settings.angle = m_shared_config_ubo->data.m_snow_settings_angle;
        }
        m_compute_graph->get_node_as<compute::nodes::ComputeSnowNode>("compute_snow_node").set_snow_settings(m_compute_pipeline_settings.snow_settings);

    } else if (m_active_compute_pipeline_type == ComputePipelineType::AVALANCHE_TRAJECTORIES) {
        // tile selection
        m_compute_graph->get_node_as<compute::nodes::SelectTilesNode>("select_tiles_node").select_tiles_in_world_aabb(m_compute_pipeline_settings.target_region, m_compute_pipeline_settings.zoomlevel);

        // tile source
        m_compute_graph->get_node_as<compute::nodes::RequestTilesNode>("request_height_node").set_settings(m_tile_source_settings.at(m_compute_pipeline_settings.tile_source_index));

        compute::nodes::ComputeReleasePointsNode::ReleasePointsSettings settings;
        settings.min_slope_angle = glm::radians(m_compute_pipeline_settings.trigger_point_min_slope_angle);
        settings.max_slope_angle = glm::radians(m_compute_pipeline_settings.trigger_point_max_slope_angle);
        settings.sampling_interval = glm::uvec2(m_compute_pipeline_settings.release_point_interval);
        m_compute_graph->get_node_as<compute::nodes::ComputeReleasePointsNode>("compute_release_points_node").set_settings(settings);

        // trajectories settings
        compute::nodes::ComputeAvalancheTrajectoriesNode::AvalancheTrajectoriesSettings trajectory_settings {};
        trajectory_settings.resolution_multiplier = m_compute_pipeline_settings.trajectory_resolution_multiplier;
        trajectory_settings.num_steps = m_compute_pipeline_settings.num_steps;
        trajectory_settings.step_length = m_compute_pipeline_settings.step_length;
        trajectory_settings.num_paths_per_release_cell = m_compute_pipeline_settings.num_paths_per_release_cell;
        trajectory_settings.random_contribution = m_compute_pipeline_settings.random_contribution;
        trajectory_settings.persistence_contribution = m_compute_pipeline_settings.persistence_contribution;

        trajectory_settings.active_runout_model = compute::nodes::ComputeAvalancheTrajectoriesNode::RunoutModelType(m_compute_pipeline_settings.runout_model_type);
        trajectory_settings.runout_perla = m_compute_pipeline_settings.perla;
        trajectory_settings.runout_flowpy.alpha = glm::radians(m_compute_pipeline_settings.runout_flowpy_alpha);

        auto& trajectories_node = m_compute_graph->get_node_as<compute::nodes::ComputeAvalancheTrajectoriesNode>("compute_avalanche_trajectories_node");
        trajectories_node.set_settings(trajectory_settings);
    } else if (m_active_compute_pipeline_type == ComputePipelineType::AVALANCHE_TRAJECTORIES_EVAL) {

        // trajectories settings
        compute::nodes::ComputeAvalancheTrajectoriesNode::AvalancheTrajectoriesSettings trajectory_settings {};
        trajectory_settings.resolution_multiplier = m_compute_pipeline_settings.trajectory_resolution_multiplier;
        trajectory_settings.num_steps = m_compute_pipeline_settings.num_steps;
        trajectory_settings.step_length = m_compute_pipeline_settings.step_length;
        trajectory_settings.num_paths_per_release_cell = m_compute_pipeline_settings.num_paths_per_release_cell;
        trajectory_settings.random_contribution = m_compute_pipeline_settings.random_contribution;
        trajectory_settings.persistence_contribution = m_compute_pipeline_settings.persistence_contribution;

        trajectory_settings.active_runout_model = compute::nodes::ComputeAvalancheTrajectoriesNode::RunoutModelType(m_compute_pipeline_settings.runout_model_type);
        trajectory_settings.runout_perla = m_compute_pipeline_settings.perla;
        trajectory_settings.runout_flowpy.alpha = glm::radians(m_compute_pipeline_settings.runout_flowpy_alpha);

        auto& trajectories_node = m_compute_graph->get_node_as<compute::nodes::ComputeAvalancheTrajectoriesNode>("compute_avalanche_trajectories_node");
        trajectories_node.set_settings(trajectory_settings);

        {
            compute::nodes::LoadTextureNode::LoadTextureNodeSettings settings;
            settings.format = WGPUTextureFormat_RGBA8Unorm;
            settings.file_path = m_compute_pipeline_settings.release_points_texture_path;
            m_compute_graph->get_node_as<compute::nodes::LoadTextureNode>("load_rp_node").set_settings(settings);
            qDebug() << "lel set node settings to " << settings.file_path << "lel";
        }

        {
            compute::nodes::LoadTextureNode::LoadTextureNodeSettings settings;
            settings.file_path = m_compute_pipeline_settings.heightmap_texture_path;
            m_compute_graph->get_node_as<compute::nodes::LoadTextureNode>("load_heights_node").set_settings(settings);
        }

        {
            compute::nodes::LoadRegionAabbNode::LoadRegionAabbNodeSettings settings;
            settings.file_path = m_compute_pipeline_settings.aabb_file_path;
            m_compute_graph->get_node_as<compute::nodes::LoadRegionAabbNode>("load_aabb_node").set_settings(settings);
        }

    } else if (m_active_compute_pipeline_type == ComputePipelineType::D8_DIRECTIONS) {
        // tile selection
        m_compute_graph->get_node_as<compute::nodes::SelectTilesNode>("select_tiles_node").select_tiles_in_world_aabb(m_compute_pipeline_settings.target_region, m_compute_pipeline_settings.zoomlevel);

        // tile source
        m_compute_graph->get_node_as<compute::nodes::RequestTilesNode>("request_height_node").set_settings(m_tile_source_settings.at(m_compute_pipeline_settings.tile_source_index));
    } else if (m_active_compute_pipeline_type == ComputePipelineType::ITERATIVE_SIMULATION) {
        // tile selection
        m_compute_graph->get_node_as<compute::nodes::SelectTilesNode>("select_tiles_node").select_tiles_in_world_aabb(m_compute_pipeline_settings.target_region, m_compute_pipeline_settings.zoomlevel);

        // tile source
        m_compute_graph->get_node_as<compute::nodes::RequestTilesNode>("request_height_node").set_settings(m_tile_source_settings.at(m_compute_pipeline_settings.tile_source_index));

        compute::nodes::ComputeReleasePointsNode::ReleasePointsSettings settings;
        settings.min_slope_angle = glm::radians(m_compute_pipeline_settings.trigger_point_min_slope_angle);
        settings.max_slope_angle = glm::radians(m_compute_pipeline_settings.trigger_point_max_slope_angle);
        settings.sampling_interval = glm::uvec2(m_compute_pipeline_settings.release_point_interval);
        m_compute_graph->get_node_as<compute::nodes::ComputeReleasePointsNode>("compute_release_points_node").set_settings(settings);
    }
}

void Window::update_settings_and_rerun_pipeline()
{
    update_compute_pipeline_settings();
    if (m_is_region_selected) {
        m_compute_graph->run();
    }
}

void Window::init_compute_pipeline_presets()
{
    ComputePipelineSettings default_values;
    ComputePipelineSettings preset_a = {
        .target_region = {}, // select tiles node
        .zoomlevel = 18,
        .num_steps = 512u,
        .step_length = 0.1f,
        .sync_snow_settings_with_render_settings = true, // snow node
        .snow_settings = compute::nodes::ComputeSnowNode::SnowSettings(), // snow node
        .release_point_interval = 16, // trajectories node
        .perla = {},
    };
    ComputePipelineSettings preset_b = {
        .target_region = {}, // select tiles node
        .zoomlevel = 18,
        .num_steps = 2048u,
        .step_length = 0.1f,
        .sync_snow_settings_with_render_settings = true, // snow node
        .snow_settings = compute::nodes::ComputeSnowNode::SnowSettings(), // snow node
        .release_point_interval = 16, // trajectories node
        .perla = {},
    };

    m_compute_pipeline_presets.push_back(default_values);
    m_compute_pipeline_presets.push_back(preset_a);
    m_compute_pipeline_presets.push_back(preset_b);
}

void Window::apply_compute_pipeline_preset(size_t preset_index)
{
    assert(preset_index < m_compute_pipeline_presets.size());

    // replace all parameters except selected region
    const auto old_region = m_compute_pipeline_settings.target_region;
    m_compute_pipeline_settings = m_compute_pipeline_presets.at(preset_index);
    m_compute_pipeline_settings.target_region = old_region;
}

// Equivalent of std::bit_width that is available from C++20 onward
// ToDo: there are intrinsics for this
uint32_t bit_width(uint32_t m)
{
    if (m == 0)
        return 0;
    else {
        uint32_t w = 0;
        while (m >>= 1)
            ++w;
        return w;
    }
}

uint32_t getMaxMipLevelCount(const glm::uvec2 textureSize) { return std::max(1u, bit_width(std::max(textureSize.x, textureSize.y))); }

std::unique_ptr<webgpu::raii::TextureWithSampler> Window::create_overlay_texture(unsigned int width, unsigned int height)
{
    WGPUTextureDescriptor texture_desc {};
    texture_desc.label = "image overlay texture";
    texture_desc.dimension = WGPUTextureDimension::WGPUTextureDimension_2D;
    texture_desc.size = { uint32_t(width), uint32_t(height), uint32_t(1) };
    texture_desc.mipLevelCount = getMaxMipLevelCount(glm::uvec2(width, height));
    texture_desc.sampleCount = 1;
    texture_desc.format = WGPUTextureFormat::WGPUTextureFormat_RGBA8Unorm;
    texture_desc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst | WGPUTextureUsage_StorageBinding;

    qDebug() << "mip level count: " << texture_desc.mipLevelCount;
    qDebug() << "for texture size: " << width << "x" << height << "pixels";

    WGPUSamplerDescriptor sampler_desc {};
    sampler_desc.label = "image overlay sampler";
    sampler_desc.addressModeU = WGPUAddressMode::WGPUAddressMode_ClampToEdge;
    sampler_desc.addressModeV = WGPUAddressMode::WGPUAddressMode_ClampToEdge;
    sampler_desc.addressModeW = WGPUAddressMode::WGPUAddressMode_ClampToEdge;
    sampler_desc.magFilter = WGPUFilterMode::WGPUFilterMode_Linear;
    sampler_desc.minFilter = WGPUFilterMode::WGPUFilterMode_Nearest;
    sampler_desc.mipmapFilter = WGPUMipmapFilterMode::WGPUMipmapFilterMode_Linear;
    sampler_desc.lodMinClamp = 0.0f;
    sampler_desc.lodMaxClamp = 1.0f;
    sampler_desc.compare = WGPUCompareFunction::WGPUCompareFunction_Undefined;
    sampler_desc.maxAnisotropy = 1;

    auto texture = std::make_unique<webgpu::raii::TextureWithSampler>(m_device, texture_desc, sampler_desc);

    return texture;
}

void Window::compute_mipmaps_for_texture(const webgpu::raii::Texture* texture)
{
    glm::uvec2 baseSize = { texture->width(), texture->height() };
    uint32_t mipLevelCount = texture->mip_level_count();

    if (mipLevelCount == 1) {
        qDebug() << "No mipmaps to compute";
        return;
    } else {
        qDebug() << "Computing mipmaps for texture";
    }

    std::vector<std::unique_ptr<webgpu::raii::TextureView>> m_textureMipViews;
    std::vector<WGPUExtent3D> mipSizes(mipLevelCount);

    // Create texture views for each mip level
    for (uint32_t i = 0; i < mipLevelCount; i++) {
        WGPUTextureViewDescriptor viewDesc {};
        viewDesc.dimension = WGPUTextureViewDimension::WGPUTextureViewDimension_2D;
        viewDesc.format = WGPUTextureFormat::WGPUTextureFormat_RGBA8Unorm;
        viewDesc.baseMipLevel = i;
        viewDesc.mipLevelCount = 1;
        viewDesc.baseArrayLayer = 0;
        viewDesc.arrayLayerCount = 1;
        viewDesc.aspect = WGPUTextureAspect::WGPUTextureAspect_All;
        auto textureView = std::make_unique<webgpu::raii::TextureView>(texture->handle(), viewDesc);
        m_textureMipViews.push_back(std::move(textureView));

        mipSizes[i].width = std::max(1u, baseSize.x >> i);
        mipSizes[i].height = std::max(1u, baseSize.y >> i);
        mipSizes[i].depthOrArrayLayers = 1;
    }

    // Create bind groups for each invocation
    std::vector<std::unique_ptr<webgpu::raii::BindGroup>> m_bindGroups;
    for (uint32_t i = 0; i < mipLevelCount - 1; i++) {
        std::vector<WGPUBindGroupEntry> bgEntries {
            m_textureMipViews[i]->create_bind_group_entry(0),
            m_textureMipViews[i + 1]->create_bind_group_entry(1),
        };
        auto bindGroup = std::make_unique<webgpu::raii::BindGroup>(m_device, m_context->pipeline_manager()->mipmap_creation_bind_group_layout(), bgEntries, "mipmap creation bindgroup");
        m_bindGroups.push_back(std::move(bindGroup));
    }

    glm::uvec3 SHADER_WORKGROUP_SIZE = { 8, 8, 1 };
    {
        WGPUCommandEncoderDescriptor descriptor {};
        webgpu::raii::CommandEncoder encoder(m_device, descriptor);

        for (uint32_t i = 0; i < mipLevelCount - 1; i++) {
            WGPUComputePassDescriptor compute_pass_desc {};
            webgpu::raii::ComputePassEncoder compute_pass(encoder.handle(), compute_pass_desc);

            glm::uvec3 workgroup_counts = glm::ceil(glm::vec3(mipSizes[i + 1].width, mipSizes[i + 1].height, 1) / glm::vec3(SHADER_WORKGROUP_SIZE));
            qDebug() << "executing mipmap creation for mip level " << i << " with workgroup counts: " << workgroup_counts.x << "x" << workgroup_counts.y;
            wgpuComputePassEncoderSetBindGroup(compute_pass.handle(), 0, m_bindGroups[i]->handle(), 0, nullptr);
            m_context->pipeline_manager()->mipmap_creation_pipeline().run(compute_pass, workgroup_counts);
        }

        WGPUCommandBufferDescriptor cmd_buffer_descriptor {};
        cmd_buffer_descriptor.label = "MipMap command buffer";
        WGPUCommandBuffer command = wgpuCommandEncoderFinish(encoder.handle(), &cmd_buffer_descriptor);
        wgpuQueueSubmit(m_queue, 1, &command);
        wgpuCommandBufferRelease(command);
    }
}

void Window::display_message(const std::string& message)
{
    m_gui_error_state.text = message;
    m_gui_error_state.should_open_modal = true;
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
    //  emit gpu_ready_changed(false); // TODO find replacement
}

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

    emit update_requested();
    // m_needs_redraw = true;
}

void Window::update_debug_scheduler_stats([[maybe_unused]] const QString& stats)
{
    // Logic for updating debug scheduler stats, parameter currently unused
}

void Window::pick_value([[maybe_unused]] const glm::dvec2& screen_space_coordinate)
{
    // Logic for picking (e.g. read back id buffer for label picking or sth)
}

void Window::request_redraw() { m_needs_redraw = true; }

void Window::file_upload_handler(const std::string& filename, const std::string& tag)
{
    if (tag == "track") {
        load_track_and_focus(filename);
    } else if (tag == "overlay_png") {
        update_image_overlay_texture(filename);
    } else if (tag == "overlay_aabb_txt") {
        update_image_overlay_aabb_and_focus(filename);
    } else {
        qWarning() << "Unknown file upload tag: " << QString::fromStdString(tag);
    }
}

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
    m_is_region_selected = true;
    m_compute_pipeline_settings.target_region = track_aabb;
    update_compute_pipeline_settings();

    emit set_camera_definition_requested(new_camera_definition);

    if (m_shared_config_ubo->data.m_track_render_mode == 0) {
        m_shared_config_ubo->data.m_track_render_mode = 1;
    }
    m_needs_redraw = true;
}

void Window::update_image_overlay_texture(const std::string& image_file_path)
{
    nucleus::Raster<glm::u8vec4> image = nucleus::utils::image_loader::rgba8(QString::fromStdString(image_file_path)).value();
    m_image_overlay_texture = create_overlay_texture(image.width(), image.height());
    m_image_overlay_texture->texture().write(m_queue, image);
    m_image_overlay_settings_uniform_buffer->data.texture_size = glm::uvec2(image.width(), image.height());
    compute_mipmaps_for_texture(&m_image_overlay_texture->texture());
    recreate_compose_bind_group();
}

// TODO duplicate code, also in LoadRegionAabbNode!
bool Window::update_image_overlay_aabb(const std::string& aabb_file_path)
{
    QFile aabb_file(QString::fromStdString(aabb_file_path));
    if (!aabb_file.open(QIODevice::ReadOnly)) {
        qCritical() << "failed to load aabb file from " << aabb_file_path;
        return false;
    }
    QTextStream file_contents(&aabb_file);

    // parse extent file (very barebones rn, in the future we want to use geotiff anyway)
    // extent file contains the aabb of the aabb region (in world coordinates) the image overlay texture is associated with
    // each line contains exactly one floating point number (. as separator) with the following meaning:
    //   min_x
    //   min_y
    //   max_x
    //   max_y
    std::array<float, 4> contents;
    bool float_conversion_ok = false;
    for (size_t i = 0; i < contents.size(); i++) {
        QString line = file_contents.readLine();
        contents[i] = line.toFloat(&float_conversion_ok);
        if (!float_conversion_ok) {
            qCritical() << "failed to parse aabb file " << aabb_file_path << ": could not convert " << line << "to float";
            return false;
        }
    }

    // Make sure the aabb actually changed
    glm::fvec2 new_min = glm::fvec2 { contents[0], contents[1] };
    glm::fvec2 new_max = glm::fvec2 { contents[2], contents[3] };
    if (new_min == m_image_overlay_settings_uniform_buffer->data.aabb_min && new_max == m_image_overlay_settings_uniform_buffer->data.aabb_max) {
        return false;
    }

    m_image_overlay_settings_uniform_buffer->data.aabb_min = new_min;
    m_image_overlay_settings_uniform_buffer->data.aabb_max = new_max;
    m_image_overlay_settings_uniform_buffer->update_gpu_data(m_queue);

    qDebug() << "updated image overlay aabb to [" << m_image_overlay_settings_uniform_buffer->data.aabb_min.x << ", "
             << m_image_overlay_settings_uniform_buffer->data.aabb_min.y << "] [" << m_image_overlay_settings_uniform_buffer->data.aabb_max.x << ", "
             << m_image_overlay_settings_uniform_buffer->data.aabb_max.y << "]";

    return true;
}

void Window::update_image_overlay_aabb_and_focus(const std::string& aabb_file_path)
{
    bool update_successful = update_image_overlay_aabb(aabb_file_path);
    if (!update_successful) {
        return;
    }

    glm::dvec2 pos = glm::dvec2(m_image_overlay_settings_uniform_buffer->data.aabb_min + m_image_overlay_settings_uniform_buffer->data.aabb_max) / 2.0;
    auto size_x = m_image_overlay_settings_uniform_buffer->data.aabb_max.x - m_image_overlay_settings_uniform_buffer->data.aabb_min.x;
    auto size_y = m_image_overlay_settings_uniform_buffer->data.aabb_max.y - m_image_overlay_settings_uniform_buffer->data.aabb_min.y;
    nucleus::camera::Definition new_camera_definition = { glm::dvec3 { pos.x, pos.y, std::max(size_x, size_y) }, { pos.x, pos.y, 0 } };
    new_camera_definition.set_viewport_size(m_camera.viewport_size());
    emit set_camera_definition_requested(new_camera_definition);
}

void Window::clear_compute_overlay()
{
    m_compute_overlay_texture_view = nullptr;
    m_compute_overlay_sampler = nullptr;
    recreate_compose_bind_group();
}

void Window::update_compute_overlay_texture(const webgpu::raii::TextureWithSampler& texture_with_sampler)
{
    m_compute_overlay_texture_view = &texture_with_sampler.texture_view();
    m_compute_overlay_sampler = &texture_with_sampler.sampler();
    m_compute_overlay_settings_uniform_buffer->data.texture_size = glm::uvec2(texture_with_sampler.texture().width(), texture_with_sampler.texture().height());

    compute_mipmaps_for_texture(&texture_with_sampler.texture());
    // update in following update_compute_overlay_aabb
    recreate_compose_bind_group();
}

void Window::update_compute_overlay_aabb(const radix::geometry::Aabb<2, double>& aabb)
{
    m_compute_overlay_settings_uniform_buffer->data.aabb_min = glm::fvec2(aabb.min);
    m_compute_overlay_settings_uniform_buffer->data.aabb_max = glm::fvec2(aabb.max);
    m_compute_overlay_settings_uniform_buffer->update_gpu_data(m_queue);
}

void Window::after_first_frame()
{
#if defined(QT_DEBUG)
    load_track_and_focus(DEFAULT_GPX_TRACK_PATH);
    m_compute_graph->run();
#endif
}

void Window::reload_shaders()
{
    qDebug() << "reloading shaders...";
    m_context->shader_module_manager()->release_shader_modules();
    m_context->shader_module_manager()->create_shader_modules();
    m_context->pipeline_manager()->release_pipelines();
    m_context->pipeline_manager()->create_pipelines();
    qDebug() << "reloading shaders done";
    request_redraw();
}

void Window::on_pipeline_run_completed()
{
    // update compute overlay texture and aabb with compute pipeline outputs
    if (m_active_compute_pipeline_type == ComputePipelineType::NORMALS || m_active_compute_pipeline_type == ComputePipelineType::RELEASE_POINTS
        || m_active_compute_pipeline_type == ComputePipelineType::AVALANCHE_TRAJECTORIES
        || m_active_compute_pipeline_type == ComputePipelineType::ITERATIVE_SIMULATION) {

        const webgpu::raii::TextureWithSampler* texture = nullptr;
        if (m_active_compute_pipeline_type == ComputePipelineType::NORMALS) {
            texture = std::get<const webgpu::raii::TextureWithSampler*>(
                m_compute_graph->get_node("compute_normals_node").output_socket("normal texture").get_data());
        } else if (m_active_compute_pipeline_type == ComputePipelineType::RELEASE_POINTS) {
            texture = std::get<const webgpu::raii::TextureWithSampler*>(
                m_compute_graph->get_node("compute_release_points_node").output_socket("release point texture").get_data());
        } else if (m_active_compute_pipeline_type == ComputePipelineType::AVALANCHE_TRAJECTORIES) {
            texture
                = std::get<const webgpu::raii::TextureWithSampler*>(m_compute_graph->get_node("buffer_to_texture_node").output_socket("texture").get_data());
            // texture = std::get<const webgpu::raii::TextureWithSampler*>(m_compute_graph->get_node("fxaa_node").output_socket("texture").get_data()); // TO
            // ENABLE FXAA
        } else if (m_active_compute_pipeline_type == ComputePipelineType::ITERATIVE_SIMULATION) {
            texture = std::get<const webgpu::raii::TextureWithSampler*>(m_compute_graph->get_node("flowpy").output_socket("texture").get_data());
        }
        assert(texture != nullptr);
        update_compute_overlay_texture(*texture);

        auto& select_tiles_node = m_compute_graph->get_node_as<compute::nodes::SelectTilesNode&>("select_tiles_node");
        radix::geometry::Aabb<2, double> selected_aabb = *std::get<const radix::geometry::Aabb<2, double>*>(select_tiles_node.output_socket("region aabb").get_data());
        selected_aabb.max -= glm::dvec2(nucleus::srs::tile_width(18) / 65, nucleus::srs::tile_height(18) / 65); // stitch node ignores last col/row
        update_compute_overlay_aabb(selected_aabb);
    }
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
    m_shared_config_bind_group = std::make_unique<webgpu::raii::BindGroup>(
        m_device, m_context->pipeline_manager()->shared_config_bind_group_layout(), std::initializer_list<WGPUBindGroupEntry> { m_shared_config_ubo->raw_buffer().create_bind_group_entry(0) });

    m_camera_bind_group = std::make_unique<webgpu::raii::BindGroup>(
        m_device, m_context->pipeline_manager()->camera_bind_group_layout(), std::initializer_list<WGPUBindGroupEntry> { m_camera_config_ubo->raw_buffer().create_bind_group_entry(0) });
}

void Window::recreate_compose_bind_group()
{
    // default bindings - we need to bind something, in case compute graph not finished yet (or has been cleared)
    const webgpu::raii::TextureView& compute_overlay_texture_view
        = m_compute_overlay_texture_view != nullptr ? *m_compute_overlay_texture_view : m_compute_overlay_dummy_texture->texture_view();
    const webgpu::raii::Sampler& compute_overlay_sampler
        = m_compute_overlay_sampler != nullptr ? *m_compute_overlay_sampler : m_compute_overlay_dummy_texture->sampler();
    WGPUBindGroupEntry compute_overlay_texture_entry = compute_overlay_texture_view.create_bind_group_entry(9);
    WGPUBindGroupEntry compute_overlay_sampler_entry = compute_overlay_sampler.create_bind_group_entry(10);

    m_compose_bind_group = std::make_unique<webgpu::raii::BindGroup>(m_device,
        m_context->pipeline_manager()->compose_bind_group_layout(),
        std::initializer_list<WGPUBindGroupEntry> {
            m_gbuffer->color_texture_view(0).create_bind_group_entry(0), // albedo texture
            m_gbuffer->color_texture_view(1).create_bind_group_entry(1), // position texture
            m_gbuffer->color_texture_view(2).create_bind_group_entry(2), // normal texture
            m_atmosphere_framebuffer->color_texture_view(0).create_bind_group_entry(3), // atmosphere texture
            m_gbuffer->color_texture_view(3).create_bind_group_entry(4), // overlay texture
            m_image_overlay_settings_uniform_buffer->raw_buffer().create_bind_group_entry(5), // image overlay aabb
            m_image_overlay_texture->texture_view().create_bind_group_entry(6), // image overlay texture (in uv space)
            m_image_overlay_texture->sampler().create_bind_group_entry(7), // image overlay sampler
            m_compute_overlay_settings_uniform_buffer->raw_buffer().create_bind_group_entry(8), // compute overlay aabb
            compute_overlay_texture_entry, // compute overlay texture (in uv space)
            compute_overlay_sampler_entry, // compute overlay sampler
        });
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
