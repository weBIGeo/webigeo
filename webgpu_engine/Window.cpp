/*****************************************************************************
 * weBIGeo
 * Copyright (C) 2024 Patrick Komon
 * Copyright (C) 2024 Gerald Kimmersdorfer
 * Copyright (C) 2025 Markus Rampp
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

#include "Window.h"
#include "compute/nodes/OverlayNode.h"
#include "compute/nodes/SelectTilesNode.h"
#include "gpu_utils.h"
#include "nucleus/tile/drawing.h"
#include "nucleus/utils/geopng_decoder.h"
#include "nucleus/utils/image_loader.h"
#include "overlay/OverlayRenderer.h"
#include "overlay/TextureOverlay.h"
#include "webgpu/raii/RenderPassEncoder.h"
#include "webgpu_engine/Context.h"
#include <ktx.h>
#include <webgpu/RenderResourceRegistry.h>
#include <webgpu/util/VertexBufferInfo.h>

#include <webgpu/webgpu.h>

#ifdef ALP_WEBGPU_APP_ENABLE_IMGUI
#include "imgui.h"
#include "imgui_internal.h"
#ifndef __EMSCRIPTEN__
#include "ImGuiFileDialog.h"
#endif
// TODO: Remove ImGuiFileDialog dependency on Web-build

#include <IconsFontAwesome5.h>
#endif

namespace webgpu_engine {

Window::Window() { }

Window::~Window()
{
    // Destructor cleanup logic here
}

void Window::set_context(Context* context)
{
    m_context = context;
    connect(m_context, &Context::redraw_requested, this, &Window::request_redraw);
    connect(m_context->track_renderer(), &TrackRenderer::track_loaded, this, &Window::on_track_loaded);
}

void Window::initialise_gpu()
{
    assert(m_context != nullptr); // just make sure that context is set

    create_buffers();

    auto& reg = m_context->webgpu_ctx().resource_registry();
    reg.register_shader("compose_pass", "compose_pass.wgsl");
    reg.register_pipeline([this](WGPUDevice dev, const webgpu::RenderResourceRegistry& reg) {
        webgpu::FramebufferFormat format {};
        format.depth_format = WGPUTextureFormat_Depth24Plus;
        format.color_formats.emplace_back(WGPUTextureFormat_BGRA8Unorm);
        m_compose_pipeline = std::make_unique<webgpu::raii::GenericRenderPipeline>(dev,
            reg.shader("compose_pass"),
            reg.shader("compose_pass"),
            std::vector<webgpu::util::SingleVertexBufferInfo> {},
            format,
            std::vector<const webgpu::raii::BindGroupLayout*> {
                &reg.bind_group_layout("shared_config"),
                &reg.bind_group_layout("camera"),
                &reg.bind_group_layout("compose"),
            });
    });

    m_context->atmosphere_renderer()->init(m_context->webgpu_ctx());

    m_context->webgpu_ctx().resource_registry().recreate_all(m_context->webgpu_ctx().device());

    create_bind_groups();

    m_shadow_texture = create_shadow_texture(1, 1, 1);

    create_and_set_compute_pipeline(ComputePipelineType::AVALANCHE_TRAJECTORIES, false);

    qInfo() << "gpu_ready_changed";
    // emit gpu_ready_changed(true); //TODO remove/find replacement
}

std::unique_ptr<webgpu::raii::TextureWithSampler> Window::create_shadow_texture(uint32_t width, uint32_t height, uint32_t mip_levels)
{
    WGPUTextureDescriptor texture_desc {};
    texture_desc.label = WGPUStringView { .data = "shadow texture", .length = WGPU_STRLEN };
    texture_desc.dimension = WGPUTextureDimension::WGPUTextureDimension_2D;
    texture_desc.size = { width, height, 1 };
    texture_desc.mipLevelCount = mip_levels;
    texture_desc.sampleCount = 1;
    texture_desc.format = WGPUTextureFormat::WGPUTextureFormat_R16Float;
    texture_desc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;

    WGPUSamplerDescriptor sampler_desc {};
    sampler_desc.label = WGPUStringView { .data = "shadow sampler", .length = WGPU_STRLEN };
    sampler_desc.addressModeU = WGPUAddressMode::WGPUAddressMode_ClampToEdge;
    sampler_desc.addressModeV = WGPUAddressMode::WGPUAddressMode_ClampToEdge;
    sampler_desc.addressModeW = WGPUAddressMode::WGPUAddressMode_ClampToEdge;
    sampler_desc.magFilter = WGPUFilterMode::WGPUFilterMode_Linear;
    sampler_desc.minFilter = WGPUFilterMode::WGPUFilterMode_Linear;
    sampler_desc.mipmapFilter = WGPUMipmapFilterMode::WGPUMipmapFilterMode_Nearest;
    sampler_desc.maxAnisotropy = 1.0;

    return std::make_unique<webgpu::raii::TextureWithSampler>(m_context->webgpu_ctx().device(), texture_desc, sampler_desc);
}

void Window::on_shadow_texture_updated(const QByteArray& data)
{
    ktxTexture* ktx_texture;
    KTX_error_code result = ktxTexture_CreateFromMemory(
        reinterpret_cast<const ktx_uint8_t*>(data.constData()), data.size(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &ktx_texture);

    if (result != KTX_SUCCESS) {
        qWarning() << "Failed to create ktx texture from memory";
        return;
    }

    m_shadow_texture = create_shadow_texture(ktx_texture->baseWidth, ktx_texture->baseHeight, ktx_texture->numLevels);

    size_t level_0_size = ktxTexture_GetLevelSize(ktx_texture, 0);
    size_t level_0_offset = 0;
    ktxTexture_GetImageOffset(ktx_texture, 0, 0, 0, &level_0_offset);
    std::span byte_span { ktxTexture_GetData(ktx_texture) + level_0_offset, level_0_size };

    WGPUTexelCopyTextureInfo image_copy_texture {};
    image_copy_texture.texture = m_shadow_texture->texture().handle();
    image_copy_texture.aspect = WGPUTextureAspect::WGPUTextureAspect_All;
    image_copy_texture.mipLevel = 0;
    image_copy_texture.origin = WGPUOrigin3D { 0, 0, 0 };

    WGPUTexelCopyBufferLayout texture_data_layout {};
    texture_data_layout.bytesPerRow = 2 * ktx_texture->baseWidth;
    texture_data_layout.rowsPerImage = ktx_texture->baseHeight;
    texture_data_layout.offset = 0;

    WGPUExtent3D copy_extent { ktx_texture->baseWidth, ktx_texture->baseHeight, 1 };
    wgpuQueueWriteTexture(m_context->webgpu_ctx().queue(), &image_copy_texture, byte_span.data(), byte_span.size_bytes(), &texture_data_layout, &copy_extent);

    ktxTexture_Destroy(ktx_texture);

    recreate_compose_bind_group();
}

void Window::resize_framebuffer(int w, int h)
{
    m_swapchain_size = glm::vec2(w, h);

    m_gbuffer_format = webgpu::FramebufferFormat(m_context->tile_mesh_renderer()->render_tiles_pipeline().framebuffer_format());
    m_gbuffer_format.size = glm::uvec2 { w, h };
    m_gbuffer = std::make_unique<webgpu::Framebuffer>(m_context->webgpu_ctx().device(), m_gbuffer_format);

    m_context->atmosphere_renderer()->resize(w, h);

    m_depth_texture_bind_group = std::make_unique<webgpu::raii::BindGroup>(m_context->webgpu_ctx().device(),
        m_context->webgpu_ctx().resource_registry().bind_group_layout("depth_texture"),
        std::initializer_list<WGPUBindGroupEntry> {
            m_gbuffer->depth_texture_view().create_bind_group_entry(0), // depth
        });

    m_context->cloud_renderer()->resize(w, h);
    m_context->overlay_renderer()->resize(w, h);

    // Do late
    recreate_compose_bind_group();
}

std::unique_ptr<webgpu::raii::RenderPassEncoder> begin_render_pass(
    WGPUCommandEncoder encoder, WGPUTextureView color_attachment, WGPUTextureView depth_attachment)
{
    return std::make_unique<webgpu::raii::RenderPassEncoder>(encoder, color_attachment, depth_attachment);
}

void Window::paint(webgpu::Framebuffer* framebuffer, WGPUCommandEncoder command_encoder)
{
    m_needs_redraw = false;

    // ToDo only update on change?
    m_shared_config_ubo->data = m_context->shared_config();
    m_shared_config_ubo->update_gpu_data(m_context->webgpu_ctx().queue());

    // render atmosphere to color buffer
    m_context->atmosphere_renderer()->draw(command_encoder, m_camera_bind_group->handle());

    // render tiles to geometry buffers
    {
        std::unique_ptr<webgpu::raii::RenderPassEncoder> render_pass = m_gbuffer->begin_render_pass(command_encoder);
        wgpuRenderPassEncoderSetBindGroup(render_pass->handle(), 0, m_shared_config_bind_group->handle(), 0, nullptr);
        wgpuRenderPassEncoderSetBindGroup(render_pass->handle(), 1, m_camera_bind_group->handle(), 0, nullptr);

        using namespace nucleus::tile;
        const auto draw_list = drawing::compute_bounds(
            drawing::limit(drawing::generate_list(m_camera, m_context->aabb_decorator(), m_max_zoom_level), 1024), m_context->aabb_decorator());
        const auto culled_draw_list = drawing::sort(drawing::cull(draw_list, m_camera), m_camera.position());

        m_context->tile_mesh_renderer()->draw(render_pass->handle(), m_camera, culled_draw_list);
    }

    // render clouds
    if (m_context->shared_config().m_clouds_enabled) {
        m_context->cloud_renderer()->draw(
            command_encoder, m_depth_texture_bind_group->handle(), m_shared_config_bind_group->handle(), m_camera, m_paint_number);
        m_needs_redraw |= m_context->cloud_renderer()->needs_redraw(); // Repaint for TAAU
    }

    // render overlay textures (height lines, tile debug, etc.)
    m_context->overlay_renderer()->draw(command_encoder,
        m_gbuffer->color_texture_view(1),
        m_gbuffer->color_texture_view(2),
        m_gbuffer->color_texture_view(3),
        m_shared_config_bind_group->handle(),
        m_camera_bind_group->handle());

    // render geometry buffers to target framebuffer
    {
        std::unique_ptr<webgpu::raii::RenderPassEncoder> render_pass = framebuffer->begin_render_pass(command_encoder);
        wgpuRenderPassEncoderSetPipeline(render_pass->handle(), m_compose_pipeline->pipeline().handle());
        wgpuRenderPassEncoderSetBindGroup(render_pass->handle(), 0, m_shared_config_bind_group->handle(), 0, nullptr);
        wgpuRenderPassEncoderSetBindGroup(render_pass->handle(), 1, m_camera_bind_group->handle(), 0, nullptr);
        wgpuRenderPassEncoderSetBindGroup(render_pass->handle(), 2, m_compose_bind_groups[m_paint_number % 2]->handle(), 0, nullptr);
        wgpuRenderPassEncoderDraw(render_pass->handle(), 3, 1, 0, 0);
    }

    // render lines to color buffer
    if (m_context->shared_config().m_track_render_mode > 0) {
        m_context->track_renderer()->render(
            command_encoder, *m_shared_config_bind_group, *m_camera_bind_group, *m_depth_texture_bind_group, framebuffer->color_texture_view(0));
    }

    m_paint_number++;
}

void Window::paint_gui()
{
#ifdef ALP_WEBGPU_APP_ENABLE_IMGUI

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

        if (ImGui::Button("Run", ImVec2(250, 0))) {
            if (m_is_region_selected) {
                update_settings_and_rerun_pipeline();
            } else {
                display_message("Cannot run pipeline - No region selected");
            }
        }

        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(150 / 255.0f, 10 / 255.0f, 10 / 255.0f, 1.00f));
        if (ImGui::Button("Clear", ImVec2(100, 0))) {
            create_and_set_compute_pipeline(m_active_compute_pipeline_type);
            m_needs_redraw = true;
        }
        ImGui::PopStyleColor(1);

        const uint32_t min_zoomlevel = 1;
        const uint32_t max_zoomlevel = 18;
        ImGui::SliderScalar("Zoom level", ImGuiDataType_U32, &m_compute_zoomlevel, &min_zoomlevel, &max_zoomlevel, "%u");
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            update_settings_and_rerun_pipeline();
        }

        static int overlays_current_item = 1;
        const std::vector<std::pair<std::string, ComputePipelineType>> overlays = {
            { "Snow", ComputePipelineType::SNOW },
            { "Avalanche trajectories", ComputePipelineType::AVALANCHE_TRAJECTORIES },
            { "Iterative simulation (WIP)", ComputePipelineType::ITERATIVE_SIMULATION },
        };
        const char* current_item_label = overlays[overlays_current_item].first.c_str();
        if (ImGui::BeginCombo("Type", current_item_label)) {
            for (size_t i = 0; i < overlays.size(); i++) {
                bool is_selected = ((size_t)overlays_current_item == i);
                if (ImGui::Selectable(overlays[i].first.c_str(), is_selected)) {
                    overlays_current_item = int(i);
                    create_and_set_compute_pipeline(overlays[i].second);
                }
                if (is_selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    }

    {
        ImVec2 button_pos(10 + 58, ImGui::GetIO().DisplaySize.y - 48 * 2 - 40 - 10);
        ImGui::SetNextWindowPos(button_pos, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.5f);
        ImGui::SetNextWindowSize(ImVec2(48, 48));

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

        ImGui::Begin("ToggleGraphRenderWindow", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f)); // fully transparent
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.0f, 0.0f, 0.0f, 0.2f)); // black with alpha 0.2
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.0f, 0.0f, 0.0f, 0.2f)); // same for active

        if (ImGui::Button(ICON_FA_NETWORK_WIRED "###ToggleGraphRenderer", ImVec2(48, 48))) {
            m_should_render_node_graph = !m_should_render_node_graph;
        }

        ImGui::PopStyleColor(3);
        ImGui::End();
        ImGui::PopStyleVar();
    }

    // render node graph
    if (m_should_render_node_graph) {
        m_node_graph_renderer->render();
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
        src_texture.copy_to_buffer(
            m_context->webgpu_ctx().device(), *m_position_readback_buffer.get(), glm::uvec3(device_coordinates.x, device_coordinates.y, 0), glm::uvec2(16, 1));

        std::vector<glm::vec4> pos_buffer;
        WGPUMapAsyncStatus result
            = m_position_readback_buffer->read_back_sync(m_context->webgpu_ctx().instance(), m_context->webgpu_ctx().device(), pos_buffer);
        if (result == WGPUMapAsyncStatus_Success) {
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

    // In link mode the overlay borrows a texture owned by the (about-to-be-replaced) graph;
    // unlink it so draw() doesn't reference a destroyed texture until the new graph runs.
    // (No-op in copy mode, where the overlay owns its texture and keeps showing the last result.)
    if (auto overlay = m_compute_result_overlay.lock())
        overlay->link_texture(nullptr);

    if (pipeline_type == ComputePipelineType::SNOW) {
        m_compute_graph = compute::nodes::NodeGraph::create_snow_compute_graph(m_context->webgpu_ctx());
    } else if (pipeline_type == ComputePipelineType::AVALANCHE_TRAJECTORIES) {
        m_compute_graph = compute::nodes::NodeGraph::create_trajectories_with_export_compute_graph(m_context->webgpu_ctx());
        m_compute_graph->set_enabled_for_nodes_with_name("export", false);
    } else if (pipeline_type == ComputePipelineType::ITERATIVE_SIMULATION) {
        m_compute_graph = compute::nodes::NodeGraph::create_iterative_simulation_compute_graph(m_context->webgpu_ctx());
    }

    update_compute_pipeline_settings();

    connect(m_compute_graph.get(), &compute::nodes::NodeGraph::run_completed, this, [this](compute::GraphRunContext) { request_redraw(); });

    connect(m_compute_graph.get(), &compute::nodes::NodeGraph::run_failed, this, [this](compute::nodes::GraphRunFailureInfo info) {
        qWarning() << "graph run failed. " << info.node_name() << ": " << info.node_run_failure_info().message();
        std::string message = "Execution of pipeline failed.\n\nNode \"" + info.node_name() + "\" reported \"" + info.node_run_failure_info().message() + "\"";
        this->display_message(message);
    });

    // The graph's OverlayNode pushes its result texture + aabb here; we forward it to a
    // TextureOverlay managed by the OverlayRenderer (visible/editable in the OverlaysPanel).
    if (m_compute_graph->exists_node("overlay_node")) {
        auto& overlay_node = m_compute_graph->get_node_as<compute::nodes::OverlayNode>("overlay_node");
        overlay_node.set_update_func([this](const webgpu::raii::TextureWithSampler* texture, const radix::geometry::Aabb<2, double>& aabb, bool copy) {
            auto overlay = m_compute_result_overlay.lock();
            if (!overlay) { // first run, or the user deleted it from the panel -> (re)create
                overlay = std::make_shared<TextureOverlay>();
                overlay->name = "Compute Result";
                m_context->overlay_renderer()->add_overlay(overlay);
                m_compute_result_overlay = overlay;
            }
            // TODO: the stitch node ignores the last col/row; trim the aabb to match. This
            // correction should eventually move into the stitch node's "region aabb" output.
            radix::geometry::Aabb<2, double> trimmed = aabb;
            trimmed.max -= glm::dvec2(nucleus::srs::tile_width(18) / 65, nucleus::srs::tile_height(18) / 65);
            overlay->settings.aabb = trimmed;
            if (texture) {
                if (copy)
                    overlay->load_texture(*texture);
                else
                    overlay->link_texture(texture);
            }
            overlay->update_gpu_settings();
            request_redraw();
        });
    }

    if (should_recreate_compose_bind_group) {
        // we usually need to recreate the compose bind group, because it might have now-outdated texture bindings from the last (now-destroyed) pipeline
        // however, we dont want this to happen when initializing, because at that point we dont have a gbuffer yet (which is required for creating the bind
        // group)
        recreate_compose_bind_group();
    }

#ifdef ALP_WEBGPU_APP_ENABLE_IMGUI
    m_node_graph_renderer = std::make_unique<compute::NodeGraphRenderer>();
    m_node_graph_renderer->init(*m_compute_graph.get());
#endif

    m_is_first_pipeline_run = true;
}

void Window::update_compute_pipeline_settings()
{
    // TODO: this function should go
    if (m_compute_graph->exists_node("select_tiles_node")) {
        m_compute_graph->get_node_as<compute::nodes::SelectTilesNode>("select_tiles_node").select_tiles_in_world_aabb(m_selected_region, m_compute_zoomlevel);
    }
}

void Window::update_settings_and_rerun_pipeline(const std::string& entry_node)
{
    // TODO: This should go -> Node Graph Renderer is in charge for that
    update_compute_pipeline_settings();
    if (m_is_region_selected) {
        if (!entry_node.empty() && !m_is_first_pipeline_run) {
            if (m_compute_graph->exists_node(entry_node)) {
                m_compute_graph->get_node_as<compute::nodes::Node>(entry_node).rerun();
            } else {
                qCritical() << "Entry node" << entry_node << "does not exist.";
            }
        } else {
            m_is_first_pipeline_run = false;
            m_compute_graph->run();
        }
    } else {
        qWarning() << "No region selected. Please load track.";
    }
}

void Window::set_max_zoom_level(uint32_t max_zoom_level) { m_max_zoom_level = max_zoom_level; }

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
    m_camera_config_ubo->update_gpu_data(m_context->webgpu_ctx().queue());
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

void Window::on_track_loaded(const radix::geometry::Aabb3d& world_aabb)
{
    focus_region_3d(world_aabb);

    // Auto-enable track rendering on first load (regardless of which entry point triggered it).
    if (m_context->shared_config().m_track_render_mode == 0) {
        m_context->shared_config().m_track_render_mode = 1;
    }

    m_needs_redraw = true;
}

void Window::focus_region_3d(const radix::geometry::Aabb3d& aabb)
{
    m_is_region_selected = true;
    m_selected_region = aabb;
    update_compute_pipeline_settings();

    emit set_camera_definition_requested(nucleus::camera::Definition::looking_down_at_aabb(aabb, m_camera.viewport_size()));
}

void Window::focus_region_2d(const radix::geometry::Aabb<2, double>& aabb)
{
    emit set_camera_definition_requested(nucleus::camera::Definition::looking_down_at_aabb(aabb, m_camera.viewport_size()));
}

void Window::ready()
{
    m_context->overlay_renderer()->ready(m_context->webgpu_ctx());

#if defined(QT_DEBUG)
    m_context->track_renderer()->load_track(TrackRenderer::DEFAULT_GPX_TRACK_PATH);
    // m_compute_graph->run();
#endif
}

void Window::reload_shaders()
{
    m_context->webgpu_ctx().resource_registry().recreate_all(m_context->webgpu_ctx().device());
    recreate_compose_bind_group();
    qDebug() << "reloading shaders done";
    request_redraw();
}

void Window::create_buffers()
{
    m_shared_config_ubo = std::make_unique<Buffer<uboSharedConfig>>(m_context->webgpu_ctx().device(), WGPUBufferUsage_CopyDst | WGPUBufferUsage_Uniform);
    m_camera_config_ubo = std::make_unique<Buffer<uboCameraConfig>>(m_context->webgpu_ctx().device(), WGPUBufferUsage_CopyDst | WGPUBufferUsage_Uniform);
    m_position_readback_buffer = std::make_unique<webgpu::raii::RawBuffer<glm::vec4>>(
        m_context->webgpu_ctx().device(), WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead, 256 / sizeof(glm::vec4), "position readback buffer");
}

void Window::create_bind_groups()
{
    m_shared_config_bind_group = std::make_unique<webgpu::raii::BindGroup>(m_context->webgpu_ctx().device(),
        m_context->webgpu_ctx().resource_registry().bind_group_layout("shared_config"),
        std::initializer_list<WGPUBindGroupEntry> { m_shared_config_ubo->raw_buffer().create_bind_group_entry(0) });

    m_camera_bind_group = std::make_unique<webgpu::raii::BindGroup>(m_context->webgpu_ctx().device(),
        m_context->webgpu_ctx().resource_registry().bind_group_layout("camera"),
        std::initializer_list<WGPUBindGroupEntry> { m_camera_config_ubo->raw_buffer().create_bind_group_entry(0) });
}

void Window::recreate_compose_bind_group()
{
    for (int i = 0; i < 2; ++i) {
        m_compose_bind_groups[i] = std::make_unique<webgpu::raii::BindGroup>(m_context->webgpu_ctx().device(),
            m_context->webgpu_ctx().resource_registry().bind_group_layout("compose"),
            std::initializer_list<WGPUBindGroupEntry> {
                m_gbuffer->color_texture_view(0).create_bind_group_entry(0), // albedo texture
                m_gbuffer->color_texture_view(1).create_bind_group_entry(1), // position texture
                m_gbuffer->color_texture_view(2).create_bind_group_entry(2), // normal texture
                m_context->atmosphere_renderer()->result_view()->create_bind_group_entry(3), // atmosphere texture
                m_gbuffer->color_texture_view(3).create_bind_group_entry(4), // overlay texture
                m_context->cloud_renderer()->result_color_view(i)->create_bind_group_entry(5),
                m_context->cloud_renderer()->result_depth_view()->create_bind_group_entry(6),
                m_shadow_texture->texture_view().create_bind_group_entry(7),
                m_shadow_texture->sampler().create_bind_group_entry(8),
                m_gbuffer->depth_texture_view().create_bind_group_entry(9),
                m_context->overlay_renderer()->result_post_view()->create_bind_group_entry(10), // overlay post-shading output
                m_context->overlay_renderer()->result_pre_view()->create_bind_group_entry(11), // overlay pre-shading output
            });
    }
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
