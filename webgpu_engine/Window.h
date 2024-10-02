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
#pragma once

#include "PipelineManager.h"
#include "ShaderModuleManager.h"
#include "TileManager.h"
#include "TrackRenderer.h"
#include "UniformBufferObjects.h"
#include "compute/nodes/ComputeAreaOfInfluenceNode.h"
#include "compute/nodes/ComputeSnowNode.h"
#include "compute/nodes/NodeGraph.h"
#include "nucleus/AbstractRenderWindow.h"
#include "nucleus/camera/AbstractDepthTester.h"
#include "nucleus/camera/Controller.h"
#include "nucleus/track/GPX.h"
#include "nucleus/utils/ColourTexture.h"
#include <webgpu/raii/BindGroup.h>
#include <webgpu/webgpu.h>

class QOpenGLFramebufferObject;

namespace webgpu_engine {

// for preserving settings upon switching graph
// TODO quite ugly solution
struct ComputePipelineSettings {
    geometry::Aabb<3, double> target_region = {}; // select tiles node
    unsigned int target_zoomlevel = 18; // select tiles node
    glm::dvec3 reference_point = {}; // area of influence node
    glm::dvec2 target_point = {}; // area of influence node
    uint32_t num_steps = 128u; // area of influence node
    float steps_length = 0.5f; // area of influence node
    float radius = 20.0f; // area of influence node
    uint32_t source_zoomlevel = 17; // area of influence node
    bool sync_snow_settings_with_render_settings = true; // snow node
    compute::nodes::ComputeSnowNode::SnowSettings snow_settings; // snow node

    compute::nodes::ComputeAreaOfInfluenceNode::PhysicsModelType model_type = compute::nodes::ComputeAreaOfInfluenceNode::PhysicsModelType::MODEL1;
    float model1_velocity_coeff = 0.02f;
    float model1_gradient_coeff = 0.07f;
    float model2_gravity = 9.81f;
    float model2_mass = 5.0f;
    float model2_friction_coeff = 0.01f;
    float model2_drag_coeff = 0.2f;
};

class Window : public nucleus::AbstractRenderWindow, public nucleus::camera::AbstractDepthTester {
    Q_OBJECT
public:
    enum class ComputePipelineType { NORMALS = 0, NORMALS_AND_SNOW = 1, AREA_OF_INFLUENCE = 2 };

public:
    Window();

    ~Window() override;

    void set_wgpu_context(WGPUInstance instance, WGPUDevice device, WGPUAdapter adapter, WGPUSurface surface, WGPUQueue queue);
    void initialise_gpu() override;
    void resize_framebuffer(int w, int h) override;
    void paint(webgpu::Framebuffer* framebuffer, WGPUCommandEncoder encoder);
    // void paint(WGPUTextureView target_color_texture, WGPUTextureView target_depth_texture, WGPUCommandEncoder encoder);
    void paint([[maybe_unused]] QOpenGLFramebufferObject* framebuffer = nullptr) override { throw std::runtime_error("Not implemented"); }

    [[nodiscard]] float depth(const glm::dvec2& normalised_device_coordinates) override;
    [[nodiscard]] glm::dvec3 position(const glm::dvec2& normalised_device_coordinates) override;
    void destroy() override;
    void set_aabb_decorator(const nucleus::tile_scheduler::utils::AabbDecoratorPtr&) override;
    void set_quad_limit(unsigned new_limit) override;
    [[nodiscard]] nucleus::camera::AbstractDepthTester* depth_tester() override;
    nucleus::utils::ColourTexture::Format ortho_tile_compression_algorithm() const override;
    void set_permissible_screen_space_error(float new_error) override;
    bool needs_redraw() { return m_needs_redraw; }

    void update_required_gpu_limits(WGPULimits& limits, const WGPULimits& supported_limits);
    void paint_gui();
    void paint_compute_pipeline_gui();

public slots:
    void update_camera(const nucleus::camera::Definition& new_definition) override;
    void update_debug_scheduler_stats(const QString& stats) override;
    void update_gpu_quads(const std::vector<nucleus::tile_scheduler::tile_types::GpuTileQuad>& new_quads, const std::vector<tile::Id>& deleted_quads) override;
    void request_redraw();
    void reload_shaders();

signals:
    void set_camera_definition_requested(nucleus::camera::Definition definition);

private:
    std::unique_ptr<webgpu::raii::RawBuffer<glm::vec4>> m_position_readback_buffer;
    glm::vec4 m_last_position_readback;

    void create_buffers();
    void create_bind_groups();

    // A helper function for the depth and position method.
    // ATTENTION: This function is synchronous and will hold rendering. Use with caution!
    // Note: Depth aswell as the position is saved in the gbuffer. In contrast to the gl version
    // we can directly readback the content of the position buffer and don't need the readback depth
    // buffer anymore. May actually increase performance as we don't need to fill the seperate buffer.
    glm::vec4 synchronous_position_readback(const glm::dvec2& normalised_device_coordinates);

    void load_track_and_focus(const QString& path);
    void select_last_loaded_track_region();
    void refresh_compute_pipeline_settings(const geometry::Aabb3d& world_aabb, const nucleus::track::Point& focused_track_point_coords);
    void create_and_set_compute_pipeline(ComputePipelineType pipeline_type);
    void update_compute_pipeline_settings();

private:
    WGPUInstance m_instance = nullptr;
    WGPUDevice m_device = nullptr;
    WGPUAdapter m_adapter = nullptr;
    WGPUSurface m_surface = nullptr;
    WGPUQueue m_queue = nullptr;

    std::unique_ptr<ShaderModuleManager> m_shader_manager;
    std::unique_ptr<PipelineManager> m_pipeline_manager;

    std::unique_ptr<Buffer<uboSharedConfig>> m_shared_config_ubo;
    std::unique_ptr<Buffer<uboCameraConfig>> m_camera_config_ubo;

    std::unique_ptr<webgpu::raii::BindGroup> m_shared_config_bind_group;
    std::unique_ptr<webgpu::raii::BindGroup> m_camera_bind_group;
    std::unique_ptr<webgpu::raii::BindGroup> m_compose_bind_group;
    std::unique_ptr<webgpu::raii::BindGroup> m_depth_texture_bind_group;

    nucleus::camera::Definition m_camera;

    std::unique_ptr<TileManager> m_tile_manager;

    webgpu::FramebufferFormat m_gbuffer_format;
    std::unique_ptr<webgpu::Framebuffer> m_gbuffer;

    std::unique_ptr<webgpu::Framebuffer> m_atmosphere_framebuffer;

    // ToDo: Swapchain should get a raii class and the size could be saved in there
    glm::vec2 m_swapchain_size = glm::vec2(0.0f);
    WGPUPresentMode m_swapchain_presentmode = WGPUPresentMode::WGPUPresentMode_Fifo;

    bool m_needs_redraw = true;

    std::unique_ptr<TrackRenderer> m_track_renderer;

    std::unique_ptr<compute::nodes::NodeGraph> m_compute_graph;
    ComputePipelineType m_active_compute_pipeline_type;
    ComputePipelineSettings m_compute_pipeline_settings;
};

} // namespace webgpu_engine
