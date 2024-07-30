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

#include "InputMapper.h"
#include <GLFW/glfw3.h>
#include "nucleus/camera/Controller.h"
#include <QDebug>

namespace webgpu_app {

InputMapper::InputMapper(QObject* parent, nucleus::camera::Controller* camera_controller, GuiManager* gui_manager)
    : QObject(parent)
    , m_gui_manager(gui_manager)
{
    // Initialize keymap
    m_keymap.fill(Qt::Key_unknown);
    // 32-90 seem to be the same in both GLFW and Qt
    for (int i = 32; i <= 90; i++) {
        m_keymap[i] = static_cast<Qt::Key>(i);
    }
    m_keymap[GLFW_KEY_LEFT_CONTROL] = Qt::Key_Control;
    m_keymap[GLFW_KEY_LEFT_SHIFT] = Qt::Key_Shift;
    m_keymap[GLFW_KEY_LEFT_ALT] = Qt::Key_Alt;

    // Initialize buttonmap
    m_buttonmap.fill(Qt::NoButton);
    m_buttonmap[GLFW_MOUSE_BUTTON_LEFT] = Qt::LeftButton;
    m_buttonmap[GLFW_MOUSE_BUTTON_RIGHT] = Qt::RightButton;
    m_buttonmap[GLFW_MOUSE_BUTTON_MIDDLE] = Qt::MiddleButton;

    if (camera_controller) {
        connect(this, &InputMapper::key_pressed, camera_controller, &nucleus::camera::Controller::key_press);
        connect(this, &InputMapper::key_released, camera_controller, &nucleus::camera::Controller::key_release);
        connect(this, &InputMapper::mouse_moved, camera_controller, &nucleus::camera::Controller::mouse_move);
        connect(this, &InputMapper::mouse_pressed, camera_controller, &nucleus::camera::Controller::mouse_press);
        connect(this, &InputMapper::wheel_turned, camera_controller, &nucleus::camera::Controller::wheel_turn);
        connect(this, &InputMapper::touch, camera_controller, &nucleus::camera::Controller::touch);
    }
#ifdef __EMSCRIPTEN__
    connect(&WebInterop::instance(), &WebInterop::touch_event, this, &InputMapper::touch_event);
#endif

}


void InputMapper::on_key_callback(int key, [[maybe_unused]]int scancode, int action, [[maybe_unused]]int mods) {
    assert(key >= 0 && (size_t)key < m_keymap.size());
    if (m_ongoing_touch_interaction) return;
    if (m_gui_manager && m_gui_manager->want_capture_keyboard())
        return;

    const auto qtKey = m_keymap[key];
    if (qtKey == Qt::Key_unknown) {
        // qWarning() << "Key not mapped " << key;
        return;
    }

    QKeyCombination combination(qtKey);
    if (action == GLFW_PRESS) {
        emit key_pressed(combination);
    } else if (action == GLFW_RELEASE) {
        emit key_released(combination);
    }
}

void InputMapper::on_cursor_position_callback(double xpos, double ypos) {
    if (m_ongoing_touch_interaction) return;
    if (m_gui_manager && m_gui_manager->want_capture_mouse())
        return;
    m_mouse.point.last_position = m_mouse.point.position;
    m_mouse.point.position = { xpos, ypos };
    emit mouse_moved(m_mouse);
}

void InputMapper::on_mouse_button_callback(int button, int action, [[maybe_unused]]int mods, double xpos, double ypos) {
    assert(button >= 0 && (size_t)button < m_buttonmap.size());
    if (m_ongoing_touch_interaction) return;
    if (m_gui_manager && m_gui_manager->want_capture_mouse())
        return;

    m_mouse.point.last_position = m_mouse.point.position;
    m_mouse.point.position = { xpos, ypos };

    const auto qtButton = m_buttonmap[button];

    if (action == GLFW_RELEASE) {
        m_mouse.buttons &= ~qtButton;
    } else if (action == GLFW_PRESS) {
        m_mouse.buttons |= m_buttonmap[button];
    }

    emit mouse_pressed(m_mouse);
}

void InputMapper::on_scroll_callback(double xoffset, double yoffset)
{
    if (m_gui_manager && m_gui_manager->want_capture_mouse())
        return;
    nucleus::event_parameter::Wheel wheel {};
    wheel.angle_delta = QPoint(static_cast<int>(xoffset), static_cast<int>(yoffset) * 50.0f);
    wheel.point.position = m_mouse.point.position;
    emit wheel_turned(wheel);
}

#ifdef __EMSCRIPTEN__
// NOTE: ABOUT MAPPING JS TOUCH EVENTS TO QT TOUCH EVENTS
// Qt offers states for each individual touch point, while js only offers a list of touch points
// a state for the event and a list of changed touchpoints. That means we have to keep track of
// the touchpoints ourselves and update the states accordingly. Furthermore the limit of touchpoints
// is limited by our WebInterop implementation. Almost none of this code is ideal but good enough until
// we find time to switch from glfw to sdl which comes with integrated touch support.
void InputMapper::touch_event(const WebInterop::JsTouchEvent& event) {

    const auto& jsToEventPoint([](const WebInterop::JsTouch& jsTouch) -> nucleus::event_parameter::EventPoint {
        nucleus::event_parameter::EventPoint point {};
        point.state = nucleus::event_parameter::TouchPointPressed;
        point.position = glm::vec2(jsTouch.clientX, jsTouch.clientY);
        point.last_position = point.position;
        point.press_position = point.position;
        return point;
    });

    nucleus::event_parameter::Touch touchParams;

    // First step: Remove all touch points that are not longer active (state = Released)
    for (auto it = m_touchmap.begin(); it != m_touchmap.end(); ) {
        if (it->second.state == nucleus::event_parameter::TouchPointReleased) it = m_touchmap.erase(it);
        else ++it;
    }

    if (event.type() == WebInterop::JS_TOUCH_START) {
        // add new touch points to the map
        for (int i = 0; i < JS_MAX_TOUCHES; i++) {
            if (!event.changedTouches[i].is_valid()) continue;
            const int id = event.changedTouches[i].identifier;
            m_touchmap[id] = jsToEventPoint(event.changedTouches[i]);
        }
        touchParams.is_begin_event = true;
        m_ongoing_touch_interaction = true;
    } else if (event.type() == WebInterop::JS_TOUCH_MOVE) {
        // go through all current touch points and update their state to Stationary
        for (int i = 0; i < JS_MAX_TOUCHES; i++) {
            if (!event.touches[i].is_valid()) continue;
            const auto& it = m_touchmap.find(event.touches[i].identifier);
            if (it != m_touchmap.end()) {
                auto& eventPoint = it->second;
                eventPoint.last_position = eventPoint.press_position = eventPoint.position;
                eventPoint.state = nucleus::event_parameter::TouchPointStationary;
            } // else: Error since this touchpoint should be in the list
        }
        // go through all changed touch points and update their state and position
        for (int i = 0; i < JS_MAX_TOUCHES; i++) {
            if (!event.changedTouches[i].is_valid()) continue;
            const auto& it = m_touchmap.find(event.changedTouches[i].identifier);
            if (it != m_touchmap.end()) {
                auto& eventPoint = it->second;
                eventPoint.last_position = eventPoint.position;
                eventPoint.position = eventPoint.press_position = glm::vec2(event.changedTouches[i].clientX, event.changedTouches[i].clientY);
                eventPoint.state = nucleus::event_parameter::TouchPointMoved;
            } // else: Error since this touchpoint should be in the list
        }
        touchParams.is_update_event = true;
    } else if (event.type() == WebInterop::JS_TOUCH_END) {
        // set the changed touch points to released
        size_t deactivatedCount = 0;
        for (int i = 0; i < JS_MAX_TOUCHES; i++) {
            if (!event.changedTouches[i].is_valid()) continue;
            const auto& it = m_touchmap.find(event.changedTouches[i].identifier);
            if (it != m_touchmap.end()) {
                auto& eventPoint = it->second;
                eventPoint.last_position = eventPoint.position;
                eventPoint.position = eventPoint.press_position = glm::vec2(event.changedTouches[i].clientX, event.changedTouches[i].clientY);
                eventPoint.state = nucleus::event_parameter::TouchPointReleased;
                deactivatedCount++;
            } // else: Error since this touchpoint should be in the list
        }
        if (deactivatedCount == m_touchmap.size()) { // All touch points are released
            m_ongoing_touch_interaction = false;
        }
        touchParams.is_end_event = true;
    } else if (event.type() == WebInterop::JS_TOUCH_CANCEL) {
        // Note: Not sure if theres an equivalent in Qt. Lets just release all touch points
        for (auto& [id, eventPoint] : m_touchmap) {
            eventPoint.state = nucleus::event_parameter::TouchPointReleased;
        }
        touchParams.is_end_event = true;
        m_ongoing_touch_interaction = false;
    }
    touchParams.points.reserve(m_touchmap.size());
    for (const auto& [key, value] : m_touchmap) {
        touchParams.points.push_back(value);
    }
    emit touch(touchParams);
}

#endif

} // namespace webgpu_app
