/*****************************************************************************
 * weBIGeo
 * Copyright (C) 2026 Gerald Kimmersdorfer
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
#include "ui/ImGuiPanel.h"
#include <ctime>

namespace webgpu_engine {
class Context;
}

namespace webgpu_app::clouds {
class Manager;
}

namespace webgpu_app {

class App;

class DateTimePanel : public ImGuiPanel {
    Q_OBJECT
public:
    explicit DateTimePanel(App* terrain_renderer, webgpu_engine::Context* context, clouds::Manager* clouds_manager);
    void draw() override;
    void ready() override;

public slots:
    void disable_sun_link();
    void disable_cloud_link();

private:
    enum class CloudLinkState { Unavailable, Green, Yellow, Red };

    // load_cloud=false: update sun dir + indicator only (real-time while dragging)
    // load_cloud=true:  also call select_time_slot (on release / discrete changes)
    void recalculate_and_apply(bool load_cloud);

    App* m_terrain_renderer;
    webgpu_engine::Context* m_context;
    clouds::Manager* m_clouds_manager;

    bool m_sun_linked = true;
    bool m_cloud_linked = true;

    // Cached — updated every recalculate_and_apply call
    CloudLinkState m_cloud_link_state = CloudLinkState::Unavailable;
    int m_cloud_tileset_local_hour = -1;  // -1 when no same-day tileset found; local time

    tm m_date {};   // tm_year = years since 1900, tm_mon = 0-based, tm_mday = 1-based
    int m_hour = 12, m_minute = 0;
};

} // namespace webgpu_app
