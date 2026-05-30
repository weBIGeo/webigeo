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

#include <glm/glm.hpp>
#include <webgpu/Context.h>
#include <webgpu/raii/TextureView.h>
#include <webgpu/webgpu.h>

namespace webgpu_engine {

/// Abstract base class for screen-space overlays rendered by OverlayRenderer.
class Overlay {
public:
    float opacity = 1.0f;
    // NOTE: z_index < 0 -> preshading, z_index -> postshading
    int z_index = 0;

    virtual ~Overlay() = default;
    virtual void init(webgpu::Context& ctx) = 0;
    virtual void draw(const WGPUCommandEncoder& command_encoder,
        const webgpu::raii::TextureView& position_view,
        const webgpu::raii::TextureView& normal_view,
        const WGPUBindGroup& shared_config_bg,
        const WGPUBindGroup& camera_bg,
        const webgpu::raii::TextureView& output_view,
        glm::uvec2 output_size) = 0;
};

} // namespace webgpu_engine
