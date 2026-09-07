/*****************************************************************************
 * weBIGeo
 * Copyright (C) 2026 Gerald Kimmersdorfer
 * Copyright (C) 2025 Patrick Komon
 * Copyright (C) 2024 Lukas Herzberger
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

#include "sky_renderer.h"
#include <glm/vec3.hpp>
#include <memory>
#include <webgpu/base/raii/Texture.h>
#include <webgpu/base/raii/TextureView.h>
#include <webgpu/webgpu.h>

namespace webgpu {
class Context;
class RenderResourceRegistry;
} // namespace webgpu

namespace nucleus::camera {
class Definition;
}

namespace webgpu_engine::sky {

/**
 * Thin wrapper around the ported LUT-based sky renderer (@ref compute::SkyWithLutsComputeRenderer).
 *
 * Owns the full-resolution RGBA16Float render target the sky compute pass writes into and recreates
 * the underlying compute renderer on resize (the back buffer / depth views change with the viewport).
 *
 * Constant LUTs (transmittance, multiple-scattering) are rendered once when the compute renderer is
 * (re)created and again only when @ref mark_atmosphere_dirty was called (i.e. on atmosphere param
 * change). Dynamic LUTs (sky-view, aerial-perspective) and the final sky pass run every @ref render.
 */
class SkyRenderer {
public:
    /// Stores the device + shader registry. Call once after the webgpu context is initialised.
    void init(webgpu::Context& context);

    /// (Re)creates the render target + compute renderer for the given viewport size.
    /// @param depth_texture / depth_view the gbuffer depth (depth-only-aspect view bound as texture_2d<f32>)
    /// @param back_buffer_texture / back_buffer_view the composed scene color used as background
    void resize(uint32_t width, uint32_t height, const webgpu::raii::Texture& depth_texture, const webgpu::raii::TextureView& depth_view,
        const webgpu::raii::Texture& back_buffer_texture, const webgpu::raii::TextureView& back_buffer_view);

    /// Updates the per-frame uniforms (camera + sun direction). Call before @ref render.
    void update(const nucleus::camera::Definition& camera, const glm::vec3& sun_direction);

    /// Encodes the LUT + sky compute pass into the given command encoder.
    void render(WGPUCommandEncoder command_encoder);

    /// Request a re-render of the constant LUTs on the next @ref render (call after changing atmosphere params).
    void mark_atmosphere_dirty();

    /// Enable/disable the sky render pass. LUT textures remain valid when disabled.
    void set_sky_enabled(bool enabled) { m_sky_enabled = enabled; }

    /// Debug/test toggle (chunk 4): switch between the LUT-sampling pass and the full-resolution
    /// per-pixel ray-march pass. Rebuilds the compute renderer immediately if already resized.
    void set_default_to_per_pixel_ray_march(bool enabled);
    bool default_to_per_pixel_ray_march() const { return m_default_to_per_pixel_ray_march; }

    /// Bumped every time rebuild_renderer() replaces the atmosphere buffer / transmittance LUT (resize,
    /// or the ray-march toggle). Callers that cache bind groups referencing those resources (e.g.
    /// Window::m_compose_output_bind_group) must recreate them when this changes.
    uint64_t resource_generation() const { return m_resource_generation; }

    /// The full-resolution RGBA16Float texture the sky pass writes into (nullptr before the first resize).
    const webgpu::raii::TextureView* result_view() const;

    /// LUT resources for cloud lighting — valid after the first resize, regardless of m_sky_enabled.
    const webgpu::raii::TextureView* transmittance_lut_view() const;
    const webgpu::raii::Sampler* transmittance_lut_sampler() const;
    WGPUBuffer atmosphere_uniform_buffer() const;
    const webgpu::raii::TextureView* aerial_perspective_lut_view() const;
    const webgpu::raii::TextureView* sky_view_lut_view() const;

    params::Atmosphere& atmosphere();
    const params::Atmosphere& atmosphere() const;
    uniforms::Uniforms& uniforms();
    const uniforms::Uniforms& uniforms() const;

private:
    /// (Re)creates m_renderer from the current atmosphere/render-target/depth/back-buffer state.
    /// Shared by resize() and set_default_to_per_pixel_ray_march() so toggling ray-march mode at
    /// runtime doesn't need a full resize().
    void rebuild_renderer();

    WGPUDevice m_device = nullptr;
    webgpu::RenderResourceRegistry* m_registry = nullptr;
    bool m_sky_enabled = true;
    bool m_default_to_per_pixel_ray_march = false;
    uint64_t m_resource_generation = 0;

    // Stored from the last resize() call so rebuild_renderer() can be invoked independently of it.
    const webgpu::raii::Texture* m_depth_texture = nullptr;
    const webgpu::raii::TextureView* m_depth_view = nullptr;
    const webgpu::raii::Texture* m_back_buffer_texture = nullptr;
    const webgpu::raii::TextureView* m_back_buffer_view = nullptr;

    std::unique_ptr<webgpu::raii::Texture> m_render_target;
    std::unique_ptr<webgpu::raii::TextureView> m_render_target_view;
    std::unique_ptr<compute::SkyWithLutsComputeRenderer> m_renderer;

    params::Atmosphere m_atmosphere;
    uniforms::Uniforms m_uniforms;
    bool m_atmosphere_dirty = false;
};

} // namespace webgpu_engine::sky
