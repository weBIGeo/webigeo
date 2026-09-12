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
#include <webgpu/base/Buffer.h>
#include <webgpu/base/raii/BindGroup.h>
#include <webgpu/base/raii/PipelineLayout.h>
#include <webgpu/base/raii/Texture.h>
#include <webgpu/base/raii/TextureView.h>
#include <webgpu/engine/UniformBufferObjects.h>
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
    // Auto blends Hybrid and Lut smoothly across a camera-altitude band, since both are correct near
    // the ground but only one of them looks right from very high up.
    enum class Mode { Lut, RayMarch, Hybrid, Auto };

    /// Stores the device + shader registry. Call once after the webgpu context is initialised.
    void init(webgpu::Context& context);

    /// (Re)creates the render target + compute renderer for the given viewport size.
    /// @param depth_texture / depth_view the gbuffer depth (depth-only-aspect view bound as texture_2d<f32>)
    /// @param back_buffer_texture / back_buffer_view the composed scene color used as background
    void resize(uint32_t width, uint32_t height, const webgpu::raii::Texture& depth_texture, const webgpu::raii::TextureView& depth_view,
        const webgpu::raii::Texture& back_buffer_texture, const webgpu::raii::TextureView& back_buffer_view);

    /// Updates the per-frame uniforms (camera, sun direction, radius/height/sky-enabled) from shared_config,
    /// and writes the resolved planet center (derived from radius + camera position)
    /// IMPORTANT: Call before @ref render.
    void update(const nucleus::camera::Definition& camera, uboSharedConfig& shared_config);

    /// Encodes the LUT + sky compute pass (or, in Auto mode, both passes plus the blend pass) into the
    /// given command encoder.
    void render(WGPUCommandEncoder command_encoder);

    /// Request a re-render of the constant LUTs on the next @ref render (call after changing atmosphere params).
    void mark_atmosphere_dirty();

    /// Switches which sky rendering technique is used. Rebuilds the compute renderer(s) immediately if
    /// already resized.
    void set_mode(Mode mode);
    Mode mode() const { return m_mode; }

    /// Bumped every time the atmosphere buffer / transmittance LUT get rebuilt (resize, or a mode
    /// switch). Callers that cache bind groups referencing those resources (e.g.
    /// Window::m_compose_output_bind_group) must recreate them when this changes.
    uint64_t resource_generation() const { return m_resource_generation; }

    /// The full-resolution RGBA16Float texture the sky pass writes into (nullptr before the first resize).
    const webgpu::raii::TextureView* result_view() const;

    /// LUT resources for cloud lighting — valid after the first resize, regardless of m_sky_enabled.
    const webgpu::raii::TextureView* transmittance_lut_view() const;
    const webgpu::raii::Sampler* transmittance_lut_sampler() const;
    const webgpu::raii::TextureView* aerial_perspective_lut_view() const;
    const webgpu::raii::TextureView* sky_view_lut_view() const;

    params::Atmosphere& atmosphere();
    const params::Atmosphere& atmosphere() const;
    uniforms::Uniforms& uniforms();
    const uniforms::Uniforms& uniforms() const;

private:
    // The renderer whose resources back the cloud-lighting accessors above: m_renderer normally, or
    // (in Auto mode, where m_renderer is null) whichever auto sub-renderer is available.
    const compute::SkyWithLutsComputeRenderer* lut_source() const;

    void rebuild_renderer();
    void rebuild_single_renderer(compute::SkyPassVariant variant);
    void rebuild_auto_renderers();
    void rebuild_blend_pipeline();

    void render_single(WGPUCommandEncoder command_encoder);
    void render_auto(WGPUCommandEncoder command_encoder);

    // 0 = pure Lut, 1 = pure Hybrid; derived from m_camera_altitude_m.
    float auto_blend_factor() const;

    // Builds the compute config for one auto sub-renderer. When direct is true its output target is
    // m_render_target itself (the sole active side, outside the blend band); otherwise its own scratch
    // target (m_auto_hybrid_target / m_auto_lut_target), as used while both sides are blended.
    config::SkyRendererComputeConfig make_auto_compute_config(bool is_hybrid, bool direct) const;

    // Rebinds a renderer's output target only when it doesn't already match, via
    // compute::SkyWithLutsComputeRenderer::rebind_output (no LUT re-render).
    void ensure_auto_renderer_target(compute::SkyWithLutsComputeRenderer& renderer, bool is_hybrid, bool direct, bool& current_is_direct);

    WGPUDevice m_device = nullptr;
    webgpu::RenderResourceRegistry* m_registry = nullptr;
    bool m_sky_enabled = true;
    Mode m_mode = Mode::Auto;
    uint64_t m_resource_generation = 0;

    // Stored from the last resize() call so rebuild_renderer() can be invoked independently of it.
    const webgpu::raii::Texture* m_depth_texture = nullptr;
    const webgpu::raii::TextureView* m_depth_view = nullptr;
    const webgpu::raii::Texture* m_back_buffer_texture = nullptr;
    const webgpu::raii::TextureView* m_back_buffer_view = nullptr;
    uint32_t m_width = 0;
    uint32_t m_height = 0;

    std::unique_ptr<webgpu::raii::Texture> m_render_target;
    std::unique_ptr<webgpu::raii::TextureView> m_render_target_view;
    std::unique_ptr<compute::SkyWithLutsComputeRenderer> m_renderer; // Lut / RayMarch / Hybrid

    // Auto mode: inside the blend band, hybrid and LUT each render into their own scratch target and a
    // small compute pass blends the two into m_render_target. Outside the band only one side is active;
    // that side is rebound (see ensure_auto_renderer_target) to write directly into m_render_target,
    // so no scratch target or blend/copy pass is needed at all. Scratch targets allocated only while
    // m_mode == Auto.
    std::unique_ptr<webgpu::raii::Texture> m_auto_hybrid_target;
    std::unique_ptr<webgpu::raii::TextureView> m_auto_hybrid_target_view;
    std::unique_ptr<webgpu::raii::Texture> m_auto_lut_target;
    std::unique_ptr<webgpu::raii::TextureView> m_auto_lut_target_view;
    std::unique_ptr<compute::SkyWithLutsComputeRenderer> m_auto_hybrid_renderer;
    std::unique_ptr<compute::SkyWithLutsComputeRenderer> m_auto_lut_renderer;
    bool m_auto_hybrid_targets_render_target = false;
    bool m_auto_lut_targets_render_target = false;

    std::unique_ptr<webgpu::raii::BindGroupLayout> m_blend_bind_group_layout;
    std::unique_ptr<webgpu::raii::PipelineLayout> m_blend_pipeline_layout;
    std::unique_ptr<webgpu::raii::ComputePipeline> m_blend_pipeline;
    std::unique_ptr<webgpu::Buffer<float>> m_blend_factor_buffer;
    std::unique_ptr<webgpu::raii::BindGroup> m_blend_bind_group;

    float m_camera_altitude_m = 0.0f;

    params::Atmosphere m_atmosphere;
    uniforms::Uniforms m_uniforms;
    bool m_atmosphere_dirty = false;
};

} // namespace webgpu_engine::sky
