/*****************************************************************************
 * Alpine Terrain Renderer
 * Copyright (C) 2023 Gerald Kimmersdorfer
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

#include <QDebug>
#include <QList>
#include <glm/glm.hpp>
#include <memory>
#include <vector>

#include "TimerInterface.h"
#include <memory>

namespace webgpu_app::timing {

struct GuiTimerWrapper {
    std::shared_ptr<TimerInterface> timer;
    std::string name;
    std::string group;
    glm::vec4 color;
};

struct GuiTimerGroup {
    std::string name;
    std::vector<GuiTimerWrapper> timers;
};

class GuiTimerManager : public QObject {
    Q_OBJECT

public:
    // adds the given timer
    std::shared_ptr<TimerInterface> add_timer(std::shared_ptr<TimerInterface> tmr);

    template <typename T, typename = std::enable_if_t<std::is_base_of<TimerInterface, T>::value>>
    void add_timer(std::shared_ptr<T> tmr, const std::string& name, const std::string& group = "", const glm::vec4& color = glm::vec4(-1.0f))
    {
        std::shared_ptr<TimerInterface> timer = std::dynamic_pointer_cast<TimerInterface>(tmr);
        if (timer) {
            auto it = std::find_if(m_groups.begin(), m_groups.end(), [&](const GuiTimerGroup& g) { return g.name == group; });
            if (it != m_groups.end()) {
                it->timers.push_back({ timer, name, group, color });
            } else {
                GuiTimerGroup newGroup { group, { { timer, name, group, color } } };
                m_groups.push_back(newGroup);
            }
        } else {
            qCritical() << "Timer can't be added as it's not initialized correctly";
        }
    }

    [[nodiscard]] const std::vector<GuiTimerGroup>& get_groups() const { return m_groups; }

    GuiTimerManager();

private:
    // Contains the timer groups
    std::vector<GuiTimerGroup> m_groups;
};

} // namespace webgpu_app::timing
