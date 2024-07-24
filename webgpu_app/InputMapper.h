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

#include "GuiManager.h"
#include "nucleus/event_parameter.h"
#include <QKeyCombination>
#include <QObject>
#include <QPoint>

#ifdef __EMSCRIPTEN__
#include "WebInterop.h"
#endif

namespace nucleus::camera {
    class Controller;
}

namespace webgpu_app {

class InputMapper : public QObject {
    Q_OBJECT

public:
    InputMapper(QObject* parent, nucleus::camera::Controller* camera_controller, GuiManager* gui_manager);
    void on_key_callback(int key, int scancode, int action, int mods);
    void on_cursor_position_callback(double xpos, double ypos);
    void on_mouse_button_callback(int button, int action, int mods, double xpos, double ypos);
    void on_scroll_callback(double xoffset, double yoffset);

signals:
    void key_pressed(QKeyCombination key);
    void key_released(QKeyCombination key);
    void mouse_moved(nucleus::event_parameter::Mouse mouse);
    void mouse_pressed(nucleus::event_parameter::Mouse mouse);
    void wheel_turned(nucleus::event_parameter::Wheel wheel);
    void touch(nucleus::event_parameter::Touch touch);


public slots:
#ifdef __EMSCRIPTEN__
    void touch_event(const WebInterop::JsTouchEvent& event);
#endif


private:
    nucleus::event_parameter::Mouse m_mouse;
    std::array<Qt::Key, 349> m_keymap; // 349 to cover all GLFW keys
    std::array<Qt::MouseButton, 8> m_buttonmap; // 8 to cover all GLFW mouse buttons
    std::map<int, nucleus::event_parameter::EventPoint> m_touchmap;
    GuiManager* m_gui_manager = nullptr;
    bool m_ongoing_touch_interaction = false;
};

} // namespace webgpu_app
