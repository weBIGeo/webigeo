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

#include <QObject>
#include <emscripten/bind.h>
#include <webgpu/webgpu.h>

#define JS_MAX_TOUCHES 3 // also needs changes in WebInterop.cpp and the shell!

// The WebInterop class acts as bridge between the C++ code and the JavaScript code.
// It maps the exposed Javascript functions to signals on the singleton which can be used in our QObjects.
class WebInterop : public QObject {
    Q_OBJECT

public:

    enum JsTouchType {
        JS_TOUCH_START = 0,
        JS_TOUCH_MOVE = 1,
        JS_TOUCH_END = 2,
        JS_TOUCH_CANCEL = 3
    };

    struct JsTouch {
        int clientX;
        int clientY;
        int identifier;
        inline bool is_valid() const { return identifier >= 0; }
    };

    struct JsTouchEvent {
        JsTouch changedTouches[JS_MAX_TOUCHES];
        JsTouch touches[JS_MAX_TOUCHES];
        int typeint; // int easier to handle with emscripten
        inline JsTouchType type() const { return static_cast<JsTouchType>(typeint); }
    };

    // Deleted copy constructor and copy assignment operator
    WebInterop(const WebInterop&) = delete;
    WebInterop& operator=(const WebInterop&) = delete;

    // Static method to get the instance of the class
    static WebInterop& instance() {
        static WebInterop _instance;
        return _instance;
    }

    static void _canvas_size_changed(int width, int height);
    static void _touch_event(const JsTouchEvent& event);

    static void _mouse_button_event(int button, int action, int mods, double xpos, double ypos);
    static void _mouse_position_event(int button, double xpos, double ypos);

signals:
    void canvas_size_changed(int width, int height);
    void touch_event(const JsTouchEvent& event);

    void mouse_button_event(int button, int action, int mods, double xpos, double ypos);
    void mouse_position_event(double xpos, double ypos);

private:
    // Private constructor
    WebInterop() {}
};

