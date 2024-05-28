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

#include "TerrainRenderer.h"

#include <QFile>
#include <webgpu/webgpu_interface.hpp>
#include <iostream>

#ifdef ALP_WEBGPU_APP_ENABLE_IMGUI
#include <imgui.h>
#include "backends/imgui_impl_glfw.h"
#endif

#include "webgpu_engine/Window.h"

#ifdef __EMSCRIPTEN__
#include "WebInterop.h"
#include <emscripten/emscripten.h>
#else
#include "nucleus/stb/stb_image_loader.h"
#endif

static void windowResizeCallback(GLFWwindow* window, int width, int height) {
    auto terrainRenderer = static_cast<TerrainRenderer*>(glfwGetWindowUserPointer(window));
    terrainRenderer->on_window_resize(width, height);
}

static void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    auto renderer = static_cast<TerrainRenderer*>(glfwGetWindowUserPointer(window));
    renderer->get_input_mapper()->on_key_callback(key, scancode, action, mods);
}

static void cursor_position_callback(GLFWwindow* window, double xpos, double ypos) {
    auto renderer = static_cast<TerrainRenderer*>(glfwGetWindowUserPointer(window));
    renderer->get_input_mapper()->on_cursor_position_callback(xpos, ypos);
}

static void mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
    auto renderer = static_cast<TerrainRenderer*>(glfwGetWindowUserPointer(window));
    double xpos, ypos;
    glfwGetCursorPos(window, &xpos, &ypos);
    renderer->get_input_mapper()->on_mouse_button_callback(button, action, mods, xpos, ypos);
}

static void scroll_callback(GLFWwindow* window, double xoffset, double yoffset)
{
    auto renderer = static_cast<TerrainRenderer*>(glfwGetWindowUserPointer(window));
    renderer->get_input_mapper()->on_scroll_callback(xoffset, yoffset);
}

TerrainRenderer::TerrainRenderer() {
#ifdef __EMSCRIPTEN__
    // execute on window resize when canvas size changes
    QObject::connect(&WebInterop::instance(), &WebInterop::canvas_size_changed, this, &TerrainRenderer::set_glfw_window_size);
#endif
}

void TerrainRenderer::init_window() {

    if (!glfwInit()) {
        std::cerr << "Could not initialize GLFW!" << std::endl;
        //throw std::runtime_error("Could not initialize GLFW");
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    //glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    m_window = glfwCreateWindow(m_width, m_height, "weBIGeo - Geospatial Visualization Tool", NULL, NULL);
    if (!m_window) {
        std::cerr << "Could not open window!" << std::endl;
        glfwTerminate();
        throw std::runtime_error("Could not open window");
    }
    glfwSetWindowUserPointer(m_window, this);
    glfwSetWindowSizeCallback(m_window, windowResizeCallback);
    glfwSetKeyCallback(m_window, key_callback);
    glfwSetCursorPosCallback(m_window, cursor_position_callback);
    glfwSetMouseButtonCallback(m_window, mouse_button_callback);
    glfwSetScrollCallback(m_window, scroll_callback);

#ifndef __EMSCRIPTEN__
    // Load Icon for Window
    auto icon = nucleus::stb::load_8bit_rgba_image_from_file(":/icons/logo32.png");
    GLFWimage image = { int(icon.width()), int(icon.height()), icon.bytes() };
    glfwSetWindowIcon(m_window, 1, &image);
#endif
}

void TerrainRenderer::render() {
    glfwPollEvents();
    m_webgpu_window->paint(nullptr);
}

void TerrainRenderer::start() {
    std::cout << "before initWindow()" << std::endl;
    init_window();

    std::cout << "before initWebGPU()" << std::endl;
    auto glfwGetWGPUSurfaceFunctor = [this](WGPUInstance instance) { return glfwGetWGPUSurface(instance, m_window); };

#ifdef ALP_WEBGPU_APP_ENABLE_IMGUI
    m_webgpu_window = std::make_unique<webgpu_engine::Window>(glfwGetWGPUSurfaceFunctor,
        [this] () { ImGui_ImplGlfw_InitForOther(m_window, true); },
        ImGui_ImplGlfw_NewFrame,
        ImGui_ImplGlfw_Shutdown
    );
#else
    m_webgpu_window = std::make_unique<webgpu_engine::Window>(glfwGetWGPUSurfaceFunctor);
#endif

    // TODO: THIS TAKES FOREVER ON FIRST LOAD. LETS CHECK OUT WHY!
    m_controller = std::make_unique<nucleus::Controller>(m_webgpu_window.get());

    nucleus::camera::Controller* camera_controller = m_controller->camera_controller();
    m_inputMapper = std::make_unique<InputMapper>(this, camera_controller);


    connect(this, &TerrainRenderer::update_camera_requested, camera_controller, &nucleus::camera::Controller::update_camera_request);

    m_webgpu_window->initialise_gpu();
    m_webgpu_window->resize_framebuffer(m_width, m_height);

    camera_controller->set_viewport({ m_width, m_height });
    camera_controller->update();

    glfwSetWindowSize(m_window, m_width, m_height);
    m_initialized = true;


#if defined(__EMSCRIPTEN__)
    emscripten_set_main_loop_arg(
        [](void *userData) {
            TerrainRenderer& renderer = *reinterpret_cast<TerrainRenderer*>(userData);
            renderer.render();
        },
        (void*)this,
        0, true
    );
#else
    while (!glfwWindowShouldClose(m_window)) {
        // Do nothing, this checks for ongoing asynchronous operations and call their callbacks

        glfwPollEvents();
        m_webgpu_window->paint(nullptr);
    }
#endif

    // NOTE: Ressources are freed by the browser when the page is closed. Also keep in mind
    // that this part of code will be executed immediately since the main loop is not blocking.
#ifndef __EMSCRIPTEN__
    m_webgpu_window->deinit_gpu();

    glfwDestroyWindow(m_window);
    glfwTerminate();
#endif
}

void TerrainRenderer::set_glfw_window_size(int width, int height) {
    m_width = width;
    m_height = height;
    if (m_initialized) {
        glfwSetWindowSize(m_window, width, height);
    }
}

void TerrainRenderer::on_window_resize(int width, int height) {
    m_width = width;
    m_height = height;
    m_webgpu_window->resize_framebuffer(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
    m_controller->camera_controller()->set_viewport({ width, height });
    m_controller->camera_controller()->update();
}
