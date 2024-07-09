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
#include <vector>
#include <webgpu/webgpu.h>

class GuiManager {
public:
    void init(GLFWwindow* window, WGPUDevice device, WGPUTextureFormat swapchainFormat, WGPUTextureFormat depthTextureFormat);
    void render(WGPURenderPassEncoder renderPass);
    void shutdown();
    bool wantCaptureKeyboard();
    bool wantCaptureMouse();

private:
    GLFWwindow* m_window;
    WGPUDevice m_device;
    bool m_showNodeEditor = false;
    bool m_forceRepaint = false;
    long m_repaintCount = 0;
    bool m_firstFrame = true;
    float m_frameTime = 0.0f;
    std::vector<std::pair<int, int>> m_links;

    void updateUI();
};
