/*
 *
 * Copyright (c) 2024 Lukas Herzberger
 * SPDX-License-Identifier: MIT
 */

// TODO copyright notice?

#pragma once

#include "config.h"
#include "pipelines.h"
#include "resources.h"
#include <memory>

namespace webgpu_engine::atmosphere::lut {

class SkyAtmosphereLutRenderer {
public:
    SkyAtmosphereLutRenderer(std::unique_ptr<resources::SkyAtmosphereResources> resources, std::unique_ptr<pipelines::SkyAtmospherePipelines> pipelines,
        bool skip_dynamic_lut_rendering, bool uses_custom_uniforms, std::unique_ptr<atmosphere::util::ComputePass> transmittance_lut_pass,
        std::unique_ptr<atmosphere::util::ComputePass> multi_scattering_lut_pass, std::unique_ptr<atmosphere::util::ComputePass> sky_view_lut_pass,
        std::unique_ptr<atmosphere::util::ComputePass> aerial_perspective_lut_pass);

public:
    void update_atmosphere(params::Atmosphere atmosphere);
    void update_uniforms(uniforms::Uniforms uniforms);

    void render_transmittance_lut(WGPUComputePassEncoder pass_encoder);
    void render_multi_scattering_lut(WGPUComputePassEncoder pass_encoder);
    void render_sky_view_lut(WGPUComputePassEncoder pass_encoder);
    void render_aerial_perspective_lut(WGPUComputePassEncoder pass_encoder);

    void render_constant_luts(WGPUComputePassEncoder pass_encoder);
    void render_constant_luts(WGPUComputePassEncoder pass_encoder, params::Atmosphere atmosphere);
    void render_dynamic_luts(WGPUComputePassEncoder pass_encoder);
    void render_dynamic_luts(WGPUComputePassEncoder pass_encoder, uniforms::Uniforms uniforms);

    void render_luts(WGPUComputePassEncoder pass_encoder, bool force_constant_lut_rendering = false, bool skip_dynamic_lut_rendering = false,
        bool force_sky_view_rendering = false);

    resources::SkyAtmosphereResources& resources();
    const resources::SkyAtmosphereResources& resources() const;

    pipelines::SkyAtmospherePipelines& pipelines();
    const pipelines::SkyAtmospherePipelines& pipelines() const;

    bool uses_custom_uniforms() const;

public:
    static std::unique_ptr<SkyAtmosphereLutRenderer> create(WGPUDevice device, const config::SkyAtmosphereRendererConfig& config);

private:
    std::unique_ptr<resources::SkyAtmosphereResources> m_resources;
    std::unique_ptr<pipelines::SkyAtmospherePipelines> m_pipelines;
    bool m_skip_dynamic_lut_rendering;
    bool m_uses_custom_uniforms;
    std::unique_ptr<atmosphere::util::ComputePass> m_transmittance_lut_pass;
    std::unique_ptr<atmosphere::util::ComputePass> m_multi_scattering_lut_pass;
    std::unique_ptr<atmosphere::util::ComputePass> m_sky_view_lut_pass;
    std::unique_ptr<atmosphere::util::ComputePass> m_aerial_perspective_lut_pass;
};

} // namespace webgpu_engine::atmosphere::lut
