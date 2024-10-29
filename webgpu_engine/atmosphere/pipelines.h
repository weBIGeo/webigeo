/*
 * Copyright (c) 2024 Lukas Herzberger
 * SPDX-License-Identifier: MIT
 */

// TODO copyright notice?

#pragma once

#include "ShaderModuleManager.h"
#include "resources.h"
#include "shaders.h"
#include "util.h"
#include <QDebug>
#include <cstdint>
#include <memory>
#include <webgpu/raii/BindGroup.h>
#include <webgpu/raii/BindGroupLayout.h>
#include <webgpu/raii/PipelineLayout.h>
#include <webgpu/webgpu.h>

namespace webgpu_engine::atmosphere::pipelines {

const uint32_t DEFAULT_TRANSMITTANCE_LUT_SAMPLE_COUNT = 40u;
const uint32_t DEFAULT_MULTI_SCATTERING_LUT_SAMPLE_COUNT = 20u;
const uint32_t MULTI_SCATTERING_LUT_MIN_SAMPLE_COUNT = 10u;

class TransmittanceLutPipeline {
public:
    TransmittanceLutPipeline(WGPUDevice device, std::unique_ptr<webgpu::raii::BindGroupLayout> bind_group_layout,
        std::unique_ptr<webgpu::raii::PipelineLayout> pipeline_layout, std::unique_ptr<webgpu::raii::ComputePipeline> pipeline,
        WGPUTextureFormat transmittance_lut_format);

    static std::string make_shader_code([[maybe_unused]] WGPUTextureFormat transmittance_lut_format);

    static std::unique_ptr<webgpu::raii::BindGroupLayout> make_bind_group_layout(WGPUDevice device, WGPUTextureFormat transmittance_lut_format);

    static std::unique_ptr<webgpu::raii::ComputePipeline> make_compute_pipeline(
        WGPUDevice device, WGPUPipelineLayout pipeline_layout, WGPUShaderModule shader_module, uint32_t sample_count);

    static std::unique_ptr<TransmittanceLutPipeline> create(WGPUDevice device, WGPUTextureFormat transmittance_lut_format, uint32_t sample_count);

    std::unique_ptr<util::ComputePass> make_compute_pass(const resources::SkyAtmosphereResources& resources);

private:
    WGPUDevice m_device;
    std::unique_ptr<webgpu::raii::BindGroupLayout> m_bind_group_layout;
    std::unique_ptr<webgpu::raii::PipelineLayout> m_pipeline_layout;
    std::unique_ptr<webgpu::raii::ComputePipeline> m_pipeline;
    WGPUTextureFormat m_transmittance_lut_format;
};

class MultiScatteringLutPipeline {
public:
    MultiScatteringLutPipeline(WGPUDevice device, std::unique_ptr<webgpu::raii::BindGroupLayout> bind_group_layout,
        std::unique_ptr<webgpu::raii::PipelineLayout> pipeline_layout, std::unique_ptr<webgpu::raii::ComputePipeline> pipeline,
        WGPUTextureFormat multi_scattering_lut_format);

    static std::string make_shader_code(WGPUTextureFormat multi_scattering_lut_format);

    static std::unique_ptr<webgpu::raii::BindGroupLayout> make_bind_group_layout(WGPUDevice device, WGPUTextureFormat multi_scattering_lut_format);

    static std::unique_ptr<webgpu::raii::ComputePipeline> make_compute_pipeline(
        WGPUDevice device, WGPUPipelineLayout pipeline_layout, WGPUShaderModule shader_module, uint32_t sample_count);

    static std::unique_ptr<MultiScatteringLutPipeline> create(WGPUDevice device, WGPUTextureFormat multi_scattering_lut_format, uint32_t sample_count);

    std::unique_ptr<util::ComputePass> make_compute_pass(const resources::SkyAtmosphereResources& resources);

private:
    WGPUDevice m_device;
    std::unique_ptr<webgpu::raii::BindGroupLayout> m_bind_group_layout;
    std::unique_ptr<webgpu::raii::PipelineLayout> m_pipeline_layout;
    std::unique_ptr<webgpu::raii::ComputePipeline> m_pipeline;
    WGPUTextureFormat m_multi_scattering_lut_format;
};

std::vector<std::pair<std::string, double>> makeMiePhaseOverrides(std::optional<config::MieHgDPhaseConfig> miePhaseConfig);

class SkyViewLutPipeline {
public:
    SkyViewLutPipeline(WGPUDevice device, std::unique_ptr<webgpu::raii::BindGroupLayout> bind_group_layout,
        std::unique_ptr<webgpu::raii::PipelineLayout> pipeline_layout, std::unique_ptr<webgpu::raii::ComputePipeline> pipeline,
        WGPUTextureFormat sky_view_lut_format, glm::uvec2 sky_view_lut_size, glm::uvec2 multi_scattering_lut_size);

    static std::string make_shader_code([[maybe_unused]] WGPUTextureFormat sky_view_lut_format, const std::string& shadow_code,
        const std::string& custom_uniforms_code, std::optional<float> const_droplet_diameter);

    static std::unique_ptr<webgpu::raii::BindGroupLayout> make_bind_group_layout(
        WGPUDevice device, WGPUTextureFormat sky_view_lut_format, bool use_custom_uniforms_config = false);

    static std::unique_ptr<webgpu::raii::ComputePipeline> make_compute_pipeline(WGPUDevice device, WGPUPipelineLayout pipeline_layout,
        WGPUShaderModule shader_module, glm::uvec2 skyViewLutSize, glm::uvec2 multiscatteringLutSize, float distanceToMaxSampleCount,
        float fromKilometersScaleFactor, bool use_moon, config::MieHgDPhaseConfig miePhaseConfig);

    static std::unique_ptr<SkyViewLutPipeline> create(WGPUDevice device, WGPUTextureFormat sky_view_lut_format, glm::uvec2 sky_view_lut_size,
        glm::uvec2 multi_scattering_lut_size, float distanceToMaxSampleCount, float fromKilometersScaleFactor, bool useMoon, config::ShadowConfig shadowConfig,
        config::CustomUniformsSourceConfig customUniformsConfig, config::MieHgDPhaseConfig miePhaseConfig);

    std::unique_ptr<util::ComputePass> make_compute_pass(const resources::SkyAtmosphereResources& resources,
        const std::vector<WGPUBindGroup>& shadow_bind_groups, const std::vector<WGPUBindGroup>& custom_uniforms_bind_groups);

private:
    WGPUDevice m_device;
    std::unique_ptr<webgpu::raii::BindGroupLayout> m_bind_group_layout;
    std::unique_ptr<webgpu::raii::PipelineLayout> m_pipeline_layout;
    std::unique_ptr<webgpu::raii::ComputePipeline> m_pipeline;
    WGPUTextureFormat m_sky_view_lut_format;
    glm::uvec2 m_sky_view_lut_size;
    glm::uvec2 m_multi_scattering_lut_size;
};

// TODO LEFT OFF HERE - PORT NEXT PIPELINE!

class AerialPerspectiveLutPipeline {
public:
    AerialPerspectiveLutPipeline(WGPUDevice device, std::unique_ptr<webgpu::raii::BindGroupLayout> bind_group_layout,
        std::unique_ptr<webgpu::raii::PipelineLayout> pipeline_layout, std::unique_ptr<webgpu::raii::ComputePipeline> pipeline,
        WGPUTextureFormat aerial_perspective_lut_format, float aerial_perspective_slice_count, float aerial_perspective_distance_per_slice,
        glm::uvec2 multi_scattering_lut_size);

    static std::string make_shader_code([[maybe_unused]] WGPUTextureFormat aerial_perspective_lut_format, const std::string& shadow_code,
        const std::string& custom_uniforms_code, std::optional<float> const_droplet_diameter);

    static std::unique_ptr<webgpu::raii::BindGroupLayout> make_bind_group_layout(
        WGPUDevice device, WGPUTextureFormat aerial_perspective_lut_format, bool use_custom_uniforms_config = false);

    static std::unique_ptr<webgpu::raii::ComputePipeline> make_compute_pipeline(WGPUDevice device, WGPUPipelineLayout pipeline_layout,
        WGPUShaderModule shader_module, float aerial_perspective_slice_count, float aerial_perspective_distance_per_slice, glm::uvec2 multi_scattering_lut_size,
        float from_kilometers_scale_factor, bool randomize_sample_offsets, bool use_moon, config::MieHgDPhaseConfig mie_phase_config);

    static std::unique_ptr<AerialPerspectiveLutPipeline> create(WGPUDevice device, WGPUTextureFormat aerial_perspective_lut_format,
        float aerial_perspective_slice_count, float aerial_perspective_distance_per_slice, glm::uvec2 multi_scattering_lut_size,
        float from_kilometers_scale_factor, bool randomize_sample_offsets, bool use_moon, config::ShadowConfig shadow_config,
        config::CustomUniformsSourceConfig custom_uniforms_config, config::MieHgDPhaseConfig mie_phase_config);

    std::unique_ptr<util::ComputePass> make_compute_pass(const resources::SkyAtmosphereResources& resources,
        const std::vector<WGPUBindGroup>& shadow_bind_groups, const std::vector<WGPUBindGroup>& custom_uniforms_bind_groups);

    float aerial_perspective_distance_per_slice() const;
    float aerial_perspective_inv_distance_per_slice() const;

private:
    WGPUDevice m_device;
    std::unique_ptr<webgpu::raii::BindGroupLayout> m_bind_group_layout;
    std::unique_ptr<webgpu::raii::PipelineLayout> m_pipeline_layout;
    std::unique_ptr<webgpu::raii::ComputePipeline> m_pipeline;
    WGPUTextureFormat m_aerial_perspective_lut_format;
    float m_aerial_perspective_slice_count;
    float m_aerial_perspective_distance_per_slice;
    glm::uvec2 m_multi_scattering_lut_size;
};

class SkyAtmospherePipelines {
public:
    SkyAtmospherePipelines(std::unique_ptr<TransmittanceLutPipeline> transmittance_lut_pipeline,
        std::unique_ptr<MultiScatteringLutPipeline> multi_scattering_lut_pipeline, std::unique_ptr<SkyViewLutPipeline> sky_view_lut_pipeline,
        std::unique_ptr<AerialPerspectiveLutPipeline> aerial_perspective_lut_pipeline);

    static std::unique_ptr<SkyAtmospherePipelines> create(WGPUDevice device, config::SkyAtmosphereRendererConfig config);

    TransmittanceLutPipeline& transmittance_lut_pipeline();
    const TransmittanceLutPipeline& transmittance_lut_pipeline() const;

    MultiScatteringLutPipeline& multi_scattering_lut_pipeline();
    const MultiScatteringLutPipeline& multi_scattering_lut_pipeline() const;

    SkyViewLutPipeline& sky_view_lut_pipeline();
    const SkyViewLutPipeline& sky_view_lut_pipeline() const;

    AerialPerspectiveLutPipeline& aerial_perspective_lut_pipeline();
    const AerialPerspectiveLutPipeline& aerial_perspective_lut_pipeline() const;

private:
    std::unique_ptr<TransmittanceLutPipeline> m_transmittance_lut_pipeline;
    std::unique_ptr<MultiScatteringLutPipeline> m_multi_scattering_lut_pipeline;
    std::unique_ptr<SkyViewLutPipeline> m_sky_view_lut_pipeline;
    std::unique_ptr<AerialPerspectiveLutPipeline> m_aerial_perspective_lut_pipeline;
};

} // namespace webgpu_engine::atmosphere::pipelines
