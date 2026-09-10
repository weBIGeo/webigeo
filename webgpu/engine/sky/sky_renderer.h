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

/*
 * Copyright (c) 2024 Lukas Herzberger
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "lut_renderer.h"
#include "util.h"

namespace webgpu_engine::sky::compute {

// Which final sky-compositing shader/bind-group layout is in use. Mirrors the two upstream config
// flags (defaultToPerPixelRayMarch, rayMarch.rayMarchDistantSky) as one 3-way choice:
//  - LutOnly:         render_sky_with_luts.wgsl        (samples sky_view_lut + aerial_perspective_lut)
//  - PureRayMarch:    render_sky_raymarching.wgsl       (samples multi_scattering_lut, ray-marches every pixel)
//  - HybridRayMarch:  render_sky_luts_and_raymarch.wgsl (ray-marches valid-depth pixels, sky_view_lut for the rest)
enum class SkyPassVariant { LutOnly, PureRayMarch, HybridRayMarch };

class SkyWithLutsComputeRenderer {

public:
    SkyWithLutsComputeRenderer(std::unique_ptr<lut::SkyAtmosphereLutRenderer> lut_renderer, std::unique_ptr<webgpu::raii::BindGroupLayout> bind_group_layout,
        std::unique_ptr<webgpu::raii::PipelineLayout> pipeline_layout, std::unique_ptr<webgpu::raii::ComputePipeline> pipeline,
        config::SkyAtmosphereRendererConfig config, SkyPassVariant variant = SkyPassVariant::LutOnly);

    void update_uniforms(const uniforms::Uniforms& uniforms);
    void update_atmosphere(const params::Atmosphere& atmosphere);

    void render_transmittance_lut(WGPUComputePassEncoder pass_encoder);
    void render_multi_scattering_lut(WGPUComputePassEncoder pass_encoder);
    void render_sky_view_lut(WGPUComputePassEncoder pass_encoder);
    void render_aerial_perspective_lut(WGPUComputePassEncoder pass_encoder);
    void render_constant_luts(WGPUComputePassEncoder pass_encoder);
    void render_dynamic_luts(WGPUComputePassEncoder pass_encoder);
    void render_luts(WGPUComputePassEncoder pass_encoder, bool force_constant_lut_rendering = false, bool skip_dynamic_lut_rendering = false,
        bool force_sky_view_rendering = false);

    void render_sky(WGPUComputePassEncoder pass_encoder);
    void render_luts_and_sky(WGPUComputePassEncoder pass_encoder, bool force_constant_lut_rendering = false);

    /// Rebuilds only the final sky-compositing bind group against a new depth/back/render target
    /// (e.g. switching output textures) without touching the LUT pipelines/resources - no shader
    /// recompilation, no LUT re-render.
    void rebind_output(config::SkyRendererComputeConfig compute_config);

    resources::SkyAtmosphereResources& resources() { return m_lut_renderer->resources(); }
    const resources::SkyAtmosphereResources& resources() const { return m_lut_renderer->resources(); }

    SkyPassVariant variant() const { return m_variant; }

public:
    static std::vector<WGPUBindGroupLayoutEntry> make_external_bind_group_layout_entries(config::SkyAtmosphereRendererConfig config);
    static std::unique_ptr<webgpu::raii::BindGroup> make_bind_group(WGPUBindGroupLayout layout, config::SkyRendererComputeConfig compute_config,
        resources::SkyAtmosphereResources& resources, bool use_custom_uniforms = false, SkyPassVariant variant = SkyPassVariant::LutOnly);
    static std::unique_ptr<webgpu::raii::BindGroupLayout> make_bind_group_layout(
        WGPUDevice device, config::SkyAtmosphereRendererConfig config, SkyPassVariant variant = SkyPassVariant::LutOnly);
    static std::unique_ptr<webgpu::raii::ComputePipeline> make_compute_pipeline(WGPUDevice device, config::SkyAtmosphereRendererConfig config,
        WGPUPipelineLayout pipeline_layout, WGPUShaderModule shader_module, lut::SkyAtmosphereLutRenderer& lut_renderer,
        SkyPassVariant variant = SkyPassVariant::LutOnly);
    static std::unique_ptr<SkyWithLutsComputeRenderer> create(
        WGPUDevice device, webgpu::RenderResourceRegistry& registry, config::SkyAtmosphereRendererConfig config);

private:
    std::unique_ptr<lut::SkyAtmosphereLutRenderer> m_lut_renderer;

    std::unique_ptr<webgpu::raii::BindGroupLayout> m_bind_group_layout;
    std::unique_ptr<webgpu::raii::PipelineLayout> m_pipeline_layout;
    std::unique_ptr<webgpu::raii::ComputePipeline> m_pipeline;

    std::unique_ptr<util::ComputePass> m_pass;
    SkyPassVariant m_variant = SkyPassVariant::LutOnly;
};

} // namespace webgpu_engine::sky::compute
