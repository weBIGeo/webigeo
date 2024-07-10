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

#pragma once

#include <GLFW/glfw3.h>
#include <webgpu/webgpu.h>

class TerrainRenderer;

namespace webgpu_engine {
class Window;
}

class GuiManager {

public:
    GuiManager(webgpu_engine::Window* webgpu_window, TerrainRenderer* terrain_renderer)
        : m_webgpu_window(webgpu_window)
        , m_terrain_renderer(terrain_renderer)
    {
    }

    void init(GLFWwindow* window, WGPUDevice device, WGPUTextureFormat swapchainFormat, WGPUTextureFormat depthTextureFormat);
    void render(WGPURenderPassEncoder renderPass);
    void shutdown();
    bool want_capture_keyboard();
    bool want_capture_mouse();

private:
    GLFWwindow* m_window;
    WGPUDevice m_device;
    webgpu_engine::Window* m_webgpu_window = nullptr;
    TerrainRenderer* m_terrain_renderer = nullptr;
    bool m_showNodeEditor = false;

    void draw();
};
