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
#include <string>
#include <webgpu/Context.h>
#include <webgpu/raii/TextureView.h>
#include <webgpu/raii/TextureWithSampler.h>
#include <webgpu/webgpu.h>

namespace webgpu_engine {

/// Abstract base class for screen-space overlays rendered by OverlayRenderer.
class Overlay {
public:
    // NOTE: z_index < 0 -> pre-shading bucket, z_index >= 0 -> post-shading bucket
    int z_index = 0;

    // User-configurable display name. Empty -> the UI falls back to the overlay type name.
    std::string name;

    virtual ~Overlay() = default;
    virtual void init(webgpu::Context& ctx) = 0;
    // Called once all shared GPU resources (compiled shaders, pipelines, bind group layouts)
    // have been created. From here on it is safe to use them and to upload GPU data (e.g. textures).
    virtual void ready(webgpu::Context& /*ctx*/) {}
    // Called when the output texture is resized. Compute overlays override to resize their copy texture.
    virtual void resize(glm::uvec2 /*size*/) {}
    // output is RGBA8Unorm owned by OverlayRenderer.
    // Compute overlays copy output.texture() → own m_copy_texture, then write to output.texture_view() as rgba8unorm storage.
    // Render overlays begin a render pass on output.texture_view() with loadOp=Load; blend state composites on top.
    virtual void draw(const WGPUCommandEncoder& command_encoder,
        const webgpu::raii::TextureView& position_view,
        const webgpu::raii::TextureView& normal_view,
        const webgpu::raii::TextureView& overlay_view, // GBuffer slot 3 (packed tile-debug data)
        const WGPUBindGroup& shared_config_bg,
        const WGPUBindGroup& camera_bg,
        webgpu::raii::TextureWithSampler& output,
        glm::uvec2 output_size) = 0;
};

} // namespace webgpu_engine
