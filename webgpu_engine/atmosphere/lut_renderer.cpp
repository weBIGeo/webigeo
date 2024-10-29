/*
 *
 * Copyright (c) 2024 Lukas Herzberger
 * SPDX-License-Identifier: MIT
 */

#include "lut_renderer.h"

namespace webgpu_engine::atmosphere::lut {

SkyAtmosphereLutRenderer::SkyAtmosphereLutRenderer(std::unique_ptr<resources::SkyAtmosphereResources> resources,
    std::unique_ptr<pipelines::SkyAtmospherePipelines> pipelines, bool skip_dynamic_lut_rendering, bool uses_custom_uniforms,
    std::unique_ptr<util::ComputePass> transmittance_lut_pass, std::unique_ptr<util::ComputePass> multi_scattering_lut_pass,
    std::unique_ptr<util::ComputePass> sky_view_lut_pass, std::unique_ptr<util::ComputePass> aerial_perspective_lut_pass)
    : m_resources(std::move(resources))
    , m_pipelines(std::move(pipelines))
    , m_skip_dynamic_lut_rendering { skip_dynamic_lut_rendering }
    , m_uses_custom_uniforms { uses_custom_uniforms }
    , m_transmittance_lut_pass(std::move(transmittance_lut_pass))
    , m_multi_scattering_lut_pass(std::move(multi_scattering_lut_pass))
    , m_sky_view_lut_pass(std::move(sky_view_lut_pass))
    , m_aerial_perspective_lut_pass(std::move(aerial_perspective_lut_pass))
{
}

void SkyAtmosphereLutRenderer::update_atmosphere(params::Atmosphere atmosphere) { m_resources->updateAtmosphere(atmosphere); }

void SkyAtmosphereLutRenderer::update_uniforms(uniforms::Uniforms uniforms)
{
    if (!this->m_uses_custom_uniforms) {
        m_resources->updateUniforms(uniforms);
    }
}

void SkyAtmosphereLutRenderer::render_transmittance_lut(WGPUComputePassEncoder pass_encoder) { m_transmittance_lut_pass->encode(pass_encoder); }

void SkyAtmosphereLutRenderer::render_multi_scattering_lut(WGPUComputePassEncoder pass_encoder) { m_multi_scattering_lut_pass->encode(pass_encoder); }

void SkyAtmosphereLutRenderer::render_sky_view_lut(WGPUComputePassEncoder pass_encoder) { m_sky_view_lut_pass->encode(pass_encoder); }

void SkyAtmosphereLutRenderer::render_aerial_perspective_lut(WGPUComputePassEncoder pass_encoder) { m_aerial_perspective_lut_pass->encode(pass_encoder); }

void SkyAtmosphereLutRenderer::render_constant_luts(WGPUComputePassEncoder pass_encoder)
{
    render_transmittance_lut(pass_encoder);
    render_multi_scattering_lut(pass_encoder);
}

void SkyAtmosphereLutRenderer::render_constant_luts(WGPUComputePassEncoder pass_encoder, params::Atmosphere atmosphere)
{
    update_atmosphere(atmosphere);
    render_constant_luts(pass_encoder);
}

void SkyAtmosphereLutRenderer::render_dynamic_luts(WGPUComputePassEncoder pass_encoder)
{
    render_sky_view_lut(pass_encoder);
    render_aerial_perspective_lut(pass_encoder);
}

void SkyAtmosphereLutRenderer::render_dynamic_luts(WGPUComputePassEncoder pass_encoder, uniforms::Uniforms uniforms)
{
    update_uniforms(uniforms);
    render_dynamic_luts(pass_encoder);
}

void SkyAtmosphereLutRenderer::render_luts(
    WGPUComputePassEncoder pass_encoder, bool force_constant_lut_rendering, bool skip_dynamic_lut_rendering, bool force_sky_view_rendering)
{
    if (force_constant_lut_rendering) {
        render_constant_luts(pass_encoder);
    }
    if (skip_dynamic_lut_rendering) {
        if (force_sky_view_rendering) {
            render_sky_view_lut(pass_encoder);
        }
    } else {
        render_dynamic_luts(pass_encoder);
    }
}

resources::SkyAtmosphereResources& SkyAtmosphereLutRenderer::resources() { return *m_resources; }

const resources::SkyAtmosphereResources& SkyAtmosphereLutRenderer::resources() const { return *m_resources; }

pipelines::SkyAtmospherePipelines& SkyAtmosphereLutRenderer::pipelines() { return *m_pipelines; }

const pipelines::SkyAtmospherePipelines& SkyAtmosphereLutRenderer::pipelines() const { return *m_pipelines; }

bool SkyAtmosphereLutRenderer::uses_custom_uniforms() const { return m_uses_custom_uniforms; }

std::unique_ptr<SkyAtmosphereLutRenderer> SkyAtmosphereLutRenderer::create(WGPUDevice device, const config::SkyAtmosphereRendererConfig& config)
{
    auto pipelines = pipelines::SkyAtmospherePipelines::create(device, config);
    auto resources = std::make_unique<atmosphere::resources::SkyAtmosphereResources>(device, config);

    auto transmittance_pass = pipelines->transmittance_lut_pipeline().make_compute_pass(*resources);
    auto multi_scattering_pass = pipelines->multi_scattering_lut_pipeline().make_compute_pass(*resources);
    auto sky_view_lut_pass = pipelines->sky_view_lut_pipeline().make_compute_pass(*resources, {}, {});
    auto aerial_perspective_lut_pass = pipelines->aerial_perspective_lut_pipeline().make_compute_pass(*resources, {}, {});

    bool skip_dynamic_lut_rendering = config.skyRenderer.defaultToPerPixelRayMarch;
    bool uses_custom_uniforms = false;

    auto lut_renderer = std::make_unique<SkyAtmosphereLutRenderer>(std::move(resources), std::move(pipelines), skip_dynamic_lut_rendering, uses_custom_uniforms,
        std::move(transmittance_pass), std::move(multi_scattering_pass), std::move(sky_view_lut_pass), std::move(aerial_perspective_lut_pass));

    if (config.initializeConstantLuts) {
        WGPUCommandEncoderDescriptor descriptor {};
        descriptor.label = "LUT command encoder";
        webgpu::raii::CommandEncoder encoder(device, descriptor);
        {
            WGPUComputePassDescriptor compute_pass_desc {};
            compute_pass_desc.label = "LUT compute pass";
            webgpu::raii::ComputePassEncoder compute_pass_encoder(encoder.handle(), compute_pass_desc);
            lut_renderer->render_constant_luts(compute_pass_encoder.handle());
        }
        WGPUCommandBufferDescriptor cmd_buffer_descriptor {};
        cmd_buffer_descriptor.label = "LUT command buffer";
        WGPUCommandBuffer command = wgpuCommandEncoderFinish(encoder.handle(), &cmd_buffer_descriptor);
        wgpuQueueSubmit(wgpuDeviceGetQueue(device), 1, &command);
        wgpuCommandBufferRelease(command);
    }

    return std::move(lut_renderer);
}

} // namespace webgpu_engine::atmosphere::lut
