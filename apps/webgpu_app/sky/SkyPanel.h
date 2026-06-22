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

namespace webgpu_engine {
class Context;
namespace sky {
    class SkyRenderer;
}
} // namespace webgpu_engine

namespace webgpu_app {

/**
 * GUI for the LUT-based sky renderer: a floating toggle to switch between the legacy gradient
 * atmosphere and the LUT sky (for performance comparison) plus live atmosphere parameter controls.
 */
class SkyPanel : public ImGuiPanel {
public:
    SkyPanel(webgpu_engine::Context* context, webgpu_engine::sky::SkyRenderer* sky_renderer);

    void draw() override;
    void draw_panel() override;

private:
    webgpu_engine::Context* m_context;
    webgpu_engine::sky::SkyRenderer* m_sky_renderer;
};

} // namespace webgpu_app
