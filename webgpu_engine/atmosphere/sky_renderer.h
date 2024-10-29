/*
 * Copyright (c) 2024 Lukas Herzberger
 * SPDX-License-Identifier: MIT
 */

#include "lut_renderer.h"
#include "util.h"

// TODO copyright notice?

#pragma once

namespace webgpu_engine::atmosphere::sky {

class SkyWithLutsComputeRenderer {

public:
    SkyWithLutsComputeRenderer(std::unique_ptr<lut::SkyAtmosphereLutRenderer> lut_renderer, std::unique_ptr<webgpu::raii::BindGroupLayout> bind_group_layout,
        std::unique_ptr<webgpu::raii::PipelineLayout> pipeline_layout, std::unique_ptr<webgpu::raii::ComputePipeline> pipeline,
        config::SkyAtmosphereRendererConfig config, bool is_ray_march_pass = false);

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

    resources::SkyAtmosphereResources& resources() { return m_lut_renderer->resources(); }
    const resources::SkyAtmosphereResources& resources() const { return m_lut_renderer->resources(); }

public:
    static std::vector<WGPUBindGroupLayoutEntry> make_external_bind_group_layout_entries(config::SkyAtmosphereRendererConfig config);
    static std::string make_shader_code(WGPUTextureFormat render_target_format, std::string custom_uniforms_code);
    static std::unique_ptr<webgpu::raii::BindGroup> make_bind_group(WGPUBindGroupLayout layout, config::SkyRendererComputeConfig compute_config,
        resources::SkyAtmosphereResources& resources, bool use_custom_uniforms = false);
    static std::unique_ptr<webgpu::raii::BindGroupLayout> make_bind_group_layout(WGPUDevice device, config::SkyAtmosphereRendererConfig config);
    static std::unique_ptr<webgpu::raii::ComputePipeline> make_compute_pipeline(WGPUDevice device, config::SkyAtmosphereRendererConfig config,
        WGPUPipelineLayout pipeline_layout, WGPUShaderModule shader_module, lut::SkyAtmosphereLutRenderer& lut_renderer);
    static std::unique_ptr<SkyWithLutsComputeRenderer> create(WGPUDevice device, config::SkyAtmosphereRendererConfig config);

private:
    std::unique_ptr<lut::SkyAtmosphereLutRenderer> m_lut_renderer;

    std::unique_ptr<webgpu::raii::BindGroupLayout> m_bind_group_layout;
    std::unique_ptr<webgpu::raii::PipelineLayout> m_pipeline_layout;
    std::unique_ptr<webgpu::raii::ComputePipeline> m_pipeline;

    std::unique_ptr<util::ComputePass> m_pass;
    bool m_does_ray_march_distant_sky = false; // not implemented
};

} // namespace webgpu_engine::atmosphere::sky
