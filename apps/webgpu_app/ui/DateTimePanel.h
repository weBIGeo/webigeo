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

    void recalculate_and_apply(bool load_cloud);

    App* m_terrain_renderer;
    webgpu_engine::Context* m_context;
    clouds::Manager* m_clouds_manager;

    bool m_sun_linked = true;
    bool m_cloud_linked = true;

    bool m_playing = false;
    float m_play_speed = 60.0f; // simulated seconds advanced per real second (60x = 1 sim-minute per real second)
    float m_play_fixed_accum = 0.0f; // leftover real time (s) not yet consumed by the fixed 30Hz animation step
    double m_play_sim_accum = 0.0; // leftover fractional simulated seconds not yet applied to m_second

    CloudLinkState m_cloud_link_state = CloudLinkState::Unavailable;
    int m_cloud_tileset_local_hour = -1; // -1 when no same-day tileset found; local time

    tm m_date {}; // tm_year = years since 1900, tm_mon = 0-based, tm_mday = 1-based
    int m_hour = 12, m_minute = 0;
    int m_second = 0; // only advanced while playing; reset to 0 on manual edits
};

} // namespace webgpu_app
