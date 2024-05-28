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

#include "nucleus/AbstractRenderWindow.h"
#include "nucleus/Controller.h"
#include "nucleus/camera/Controller.h"
#include <GLFW/glfw3.h>
#include <memory>
#include "InputMapper.h"

class TerrainRenderer : public QObject {
    Q_OBJECT

public:
    TerrainRenderer();
    ~TerrainRenderer() = default;

    void init_window();
    void start();
    void render();
    void on_window_resize(int width, int height);

    [[nodiscard]] InputMapper* get_input_mapper()
    {
        return m_inputMapper.get();
    }

signals:
    void update_camera_requested();

private slots:
    void set_glfw_window_size(int width, int height);

private:
    GLFWwindow* m_window;
    std::unique_ptr<nucleus::AbstractRenderWindow> m_webgpu_window;
    std::unique_ptr<nucleus::Controller> m_controller;
    std::unique_ptr<InputMapper> m_inputMapper;

    uint32_t m_width = 1280;
    uint32_t m_height = 1024;
    bool m_initialized = false;

    nucleus::event_parameter::Mouse m_mouse;
};
