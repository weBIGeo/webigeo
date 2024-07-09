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

#include "InputMapper.h"
#include "dps/PropertyManager.h"
#include "gui/GuiManager.h"
#include "nucleus/AbstractRenderWindow.h"
#include "nucleus/Controller.h"
#include "nucleus/camera/Controller.h"
#include <GLFW/glfw3.h>
#include <memory>

#include <webgpu/webgpu.h>
#include <webgpu_engine/Window.h>

class TerrainRenderer : public QObject {
    Q_OBJECT

public:
    TerrainRenderer();
    ~TerrainRenderer() = default;

    struct GuiPipelineUBO {
        glm::vec2 resolution;
    };

    void init_window();
    void start();
    void render();
    void on_window_resize(int width, int height);

    [[nodiscard]] InputMapper* get_input_mapper() { return m_inputMapper.get(); }
    [[nodiscard]] GuiManager* get_gui_manager() { return m_gui_manager.get(); }

signals:
    void update_camera_requested();

private slots:
    void set_glfw_window_size(int width, int height);

private:
    GLFWwindow* m_window;
    std::unique_ptr<webgpu_engine::Window> m_webgpu_window;
    std::unique_ptr<nucleus::Controller> m_controller;
    std::unique_ptr<InputMapper> m_inputMapper;
    std::unique_ptr<GuiManager> m_gui_manager;
    std::shared_ptr<dps::PropertyManager> m_property_manager;

    WGPUInstance m_instance = nullptr;
    WGPUSurface m_surface = nullptr;
    WGPUAdapter m_adapter = nullptr;
    WGPUDevice m_device = nullptr;
    WGPUQueue m_queue = nullptr;
    WGPUSwapChain m_swapchain = nullptr;
    WGPUTextureFormat m_swapchain_format = WGPUTextureFormat::WGPUTextureFormat_Undefined;
    WGPUTextureFormat m_depth_texture_format = WGPUTextureFormat::WGPUTextureFormat_Depth24Plus;
    WGPUPresentMode m_swapchain_presentmode = WGPUPresentMode::WGPUPresentMode_Fifo;

    glm::uvec2 m_viewport_size = glm::uvec2(1280u, 1024u);
    bool m_initialized = false;
    GuiPipelineUBO m_gui_ubo_data = { glm::vec2(1280.0, 1024.0) };

    // ToDo: Use framebuffer object instead of individual textures
    std::unique_ptr<webgpu_engine::raii::Texture> m_backbuffer_color_texture;
    std::unique_ptr<webgpu_engine::raii::TextureView> m_backbuffer_color_texture_view;
    std::unique_ptr<webgpu_engine::raii::Texture> m_backbuffer_depth_texture;
    std::unique_ptr<webgpu_engine::raii::TextureView> m_backbuffer_depth_texture_view;
    std::unique_ptr<webgpu_engine::raii::Texture> m_depth_texture;
    std::unique_ptr<webgpu_engine::raii::TextureView> m_depth_texture_view;
    void create_framebuffer(uint32_t width, uint32_t height);
    void create_swapchain(uint32_t width, uint32_t height);

    long m_repaint_count = 0;
    bool m_force_repaint = false;

    std::unique_ptr<webgpu_engine::raii::GenericRenderPipeline> m_gui_pipeline;
    std::unique_ptr<webgpu_engine::raii::BindGroupLayout> m_gui_bind_group_layout;
    std::unique_ptr<webgpu_engine::raii::BindGroup> m_gui_bind_group;
    std::unique_ptr<webgpu_engine::raii::RawBuffer<GuiPipelineUBO>> m_gui_ubo;

    void webgpu_create_context();
    void webgpu_release_context();
};
