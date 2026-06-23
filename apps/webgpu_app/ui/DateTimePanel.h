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
#include <imgui.h>

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
    enum class CloudLinkState { Green, Yellow, Red };

    void recalculate_and_apply();

    App* m_terrain_renderer;
    webgpu_engine::Context* m_context;
    clouds::Manager* m_clouds_manager;

    bool m_sun_linked = true;
    bool m_cloud_linked = true;

    // Cached — updated only in recalculate_and_apply()
    CloudLinkState m_cloud_link_state = CloudLinkState::Red;

    // month is 0-based (combo index); QDate expects 1-based
    int m_year = 2026, m_month = 5, m_day = 21;
    int m_hour = 12, m_minute = 0;
};

} // namespace webgpu_app
