/*****************************************************************************
 * weBIGeo
 * Copyright (C) 2026 Gerald Kimmersdorfer
 * Copyright (C) 2026 Patrick Komon
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

namespace webgpu_app {

class ImGuiManager;

class ImGuiPanel : public QObject {
    Q_OBJECT
    friend class ImGuiManager;

public:
    virtual ~ImGuiPanel() = default;

    // Called once after initialization
    virtual void ready() { }

    // Called outside the main sidebar window
    virtual void draw() { }

    // Called inside the main sidebar ImGui::Begin/End block
    virtual void draw_panel() { }

    // Called when this panel is the active full-screen window.
    // Query m_manager->get_window_size() for the available area
    virtual void draw_window() { }

protected:
    ImGuiManager* m_manager = nullptr;
};

} // namespace webgpu_app
