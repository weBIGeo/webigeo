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

#include "sky_renderer.h"

#include <webgpu/base/RenderResourceRegistry.h>

namespace webgpu_engine::sky::compute {

namespace {
    inline WGPUStringView sv(const char* s) { return WGPUStringView { s, WGPU_STRLEN }; }
} // namespace

SkyWithLutsComputeRenderer::SkyWithLutsComputeRenderer(std::unique_ptr<lut::SkyAtmosphereLutRenderer> lut_renderer,
    std::unique_ptr<webgpu::raii::BindGroupLayout> bind_group_layout, std::unique_ptr<webgpu::raii::PipelineLayout> pipeline_layout,
    std::unique_ptr<webgpu::raii::ComputePipeline> pipeline, config::SkyAtmosphereRendererConfig config, bool is_ray_march_pass)
    : m_lut_renderer(std::move(lut_renderer))
    , m_bind_group_layout(std::move(bind_group_layout))
    , m_pipeline_layout(std::move(pipeline_layout))
    , m_pipeline(std::move(pipeline))
    , m_does_ray_march_distant_sky { config.skyRenderer.defaultToPerPixelRayMarch }
{
    std::vector<std::unique_ptr<webgpu::raii::BindGroup>> bind_groups;
    bind_groups.push_back(make_bind_group(
        m_bind_group_layout->handle(), config.skyRenderer, m_lut_renderer->resources(), m_lut_renderer->uses_custom_uniforms(), is_ray_march_pass));
    // TODO custom uniforms
    // TODO shadows

    glm::uvec3 dispatch_group_dimensions = glm::uvec3(glm::ceil(glm::vec3 {
        float(config.skyRenderer.renderTarget.texture->width()) / 16.0f,
        float(config.skyRenderer.renderTarget.texture->height()) / 16.0f,
        1.0f,
    }));

    m_pass = std::make_unique<util::ComputePass>(m_pipeline->handle(), bind_groups, dispatch_group_dimensions);
}

void SkyWithLutsComputeRenderer::update_uniforms(const uniforms::Uniforms& uniforms) { m_lut_renderer->update_uniforms(uniforms); }

void SkyWithLutsComputeRenderer::update_atmosphere(const params::Atmosphere& atmosphere) { m_lut_renderer->update_atmosphere(atmosphere); }

void SkyWithLutsComputeRenderer::render_transmittance_lut(WGPUComputePassEncoder pass_encoder) { m_lut_renderer->render_transmittance_lut(pass_encoder); }

void SkyWithLutsComputeRenderer::render_multi_scattering_lut(WGPUComputePassEncoder pass_encoder) { m_lut_renderer->render_multi_scattering_lut(pass_encoder); }

void SkyWithLutsComputeRenderer::render_sky_view_lut(WGPUComputePassEncoder pass_encoder) { m_lut_renderer->render_sky_view_lut(pass_encoder); }

void SkyWithLutsComputeRenderer::render_aerial_perspective_lut(WGPUComputePassEncoder pass_encoder)
{
    m_lut_renderer->render_aerial_perspective_lut(pass_encoder);
}

void SkyWithLutsComputeRenderer::render_constant_luts(WGPUComputePassEncoder pass_encoder) { m_lut_renderer->render_constant_luts(pass_encoder); }

void SkyWithLutsComputeRenderer::render_dynamic_luts(WGPUComputePassEncoder pass_encoder)
{
    // The pure ray-march pass samples multi_scattering_lut directly and never reads sky_view_lut/
    // aerial_perspective_lut, so rendering them would be wasted GPU work.
    if (m_does_ray_march_distant_sky) {
        return;
    }
    m_lut_renderer->render_dynamic_luts(pass_encoder);
}

void SkyWithLutsComputeRenderer::render_luts(
    WGPUComputePassEncoder pass_encoder, bool force_constant_lut_rendering, bool skip_dynamic_lut_rendering, bool force_sky_view_rendering)
{
    m_lut_renderer->render_luts(pass_encoder, force_constant_lut_rendering, skip_dynamic_lut_rendering, force_sky_view_rendering);
}

void SkyWithLutsComputeRenderer::render_sky(WGPUComputePassEncoder pass_encoder) { m_pass->encode(pass_encoder); }

void SkyWithLutsComputeRenderer::render_luts_and_sky(WGPUComputePassEncoder pass_encoder, bool force_constant_lut_rendering)
{
    m_lut_renderer->render_luts(pass_encoder, false, force_constant_lut_rendering, false);
    render_sky(pass_encoder);
}

std::vector<WGPUBindGroupLayoutEntry> SkyWithLutsComputeRenderer::make_external_bind_group_layout_entries(config::SkyAtmosphereRendererConfig config)
{
    WGPUBindGroupLayoutEntry depth_buffer_entry {};
    depth_buffer_entry.binding = 5;
    depth_buffer_entry.visibility = WGPUShaderStage_Compute;
    depth_buffer_entry.texture = {};
    depth_buffer_entry.texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;
    depth_buffer_entry.texture.viewDimension = config.skyRenderer.depthBuffer.view->descriptor().dimension;

    WGPUBindGroupLayoutEntry back_buffer_entry {};
    back_buffer_entry.binding = 6;
    back_buffer_entry.visibility = WGPUShaderStage_Compute;
    back_buffer_entry.texture = {};
    back_buffer_entry.texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;
    back_buffer_entry.texture.viewDimension = config.skyRenderer.backBuffer.view->descriptor().dimension;

    WGPUBindGroupLayoutEntry render_target_entry {};
    render_target_entry.binding = 7;
    render_target_entry.visibility = WGPUShaderStage_Compute;
    render_target_entry.storageTexture = {};
    render_target_entry.storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
    render_target_entry.storageTexture.format = config.skyRenderer.renderTarget.texture->descriptor().format;
    render_target_entry.storageTexture.viewDimension = config.skyRenderer.renderTarget.view->descriptor().dimension;

    return { depth_buffer_entry, back_buffer_entry, render_target_entry };
}

std::unique_ptr<webgpu::raii::BindGroup> SkyWithLutsComputeRenderer::make_bind_group(WGPUBindGroupLayout layout,
    config::SkyRendererComputeConfig compute_config, resources::SkyAtmosphereResources& resources, bool use_custom_uniforms, bool is_ray_march_pass)
{
    std::vector<WGPUBindGroupEntry> bind_group_entries {
        resources.atmosphere_buffer().raw_buffer().create_bind_group_entry(0),
    };

    if (!use_custom_uniforms) {
        bind_group_entries.push_back(resources.uniforms_buffer().raw_buffer().create_bind_group_entry(1));
    }

    int custom_uniform_offset = int(!use_custom_uniforms);
    bind_group_entries.push_back(resources.lut_sampler().create_bind_group_entry(1 + custom_uniform_offset));
    bind_group_entries.push_back(resources.transmittance_lut().view().create_bind_group_entry(2 + custom_uniform_offset));

    uint32_t next_binding = 3 + custom_uniform_offset;
    if (is_ray_march_pass) {
        bind_group_entries.push_back(resources.multi_scattering_lut().view().create_bind_group_entry(next_binding));
        next_binding += 1;
    } else {
        bind_group_entries.push_back(resources.sky_view_lut().view().create_bind_group_entry(next_binding));
        bind_group_entries.push_back(resources.aerial_perspective_lut().view().create_bind_group_entry(next_binding + 1));
        next_binding += 2;
    }

    bind_group_entries.push_back(compute_config.depthBuffer.view->create_bind_group_entry(next_binding));
    bind_group_entries.push_back(compute_config.backBuffer.view->create_bind_group_entry(next_binding + 1));
    bind_group_entries.push_back(compute_config.renderTarget.view->create_bind_group_entry(next_binding + 2));

    return std::make_unique<webgpu::raii::BindGroup>(
        resources.device(), layout, bind_group_entries, is_ray_march_pass ? "Render sky (ray march) bind group" : "Render sky with LUTs bind group");
}

std::unique_ptr<webgpu::raii::BindGroupLayout> SkyWithLutsComputeRenderer::make_bind_group_layout(
    WGPUDevice device, config::SkyAtmosphereRendererConfig config, bool is_ray_march_pass)
{
    WGPUBindGroupLayoutEntry atmosphere_buffer_entry {};
    atmosphere_buffer_entry.visibility = WGPUShaderStage_Compute;
    atmosphere_buffer_entry.buffer.type = WGPUBufferBindingType_Uniform;
    atmosphere_buffer_entry.buffer.hasDynamicOffset = false;
    atmosphere_buffer_entry.buffer.minBindingSize = 0;

    WGPUBindGroupLayoutEntry uniforms_buffer_entry {};
    uniforms_buffer_entry.visibility = WGPUShaderStage_Compute;
    uniforms_buffer_entry.buffer.type = WGPUBufferBindingType_Uniform;
    uniforms_buffer_entry.buffer.hasDynamicOffset = false;
    uniforms_buffer_entry.buffer.minBindingSize = 0;

    WGPUBindGroupLayoutEntry sampler_entry {};
    sampler_entry.visibility = WGPUShaderStage_Compute;
    sampler_entry.sampler.type = WGPUSamplerBindingType_Filtering;

    WGPUBindGroupLayoutEntry transmittance_lut_texture_entry {};
    transmittance_lut_texture_entry.visibility = WGPUShaderStage_Compute;
    transmittance_lut_texture_entry.texture.multisampled = false;
    transmittance_lut_texture_entry.texture.sampleType = WGPUTextureSampleType_Float;
    transmittance_lut_texture_entry.texture.viewDimension = WGPUTextureViewDimension_2D;

    WGPUBindGroupLayoutEntry sky_view_lut_texture_entry {};
    sky_view_lut_texture_entry.visibility = WGPUShaderStage_Compute;
    sky_view_lut_texture_entry.texture.multisampled = false;
    sky_view_lut_texture_entry.texture.sampleType = WGPUTextureSampleType_Float;
    sky_view_lut_texture_entry.texture.viewDimension = WGPUTextureViewDimension_2D;

    WGPUBindGroupLayoutEntry aerial_perspective_lut_texture_entry {};
    aerial_perspective_lut_texture_entry.visibility = WGPUShaderStage_Compute;
    aerial_perspective_lut_texture_entry.texture.multisampled = false;
    aerial_perspective_lut_texture_entry.texture.sampleType = WGPUTextureSampleType_Float;
    aerial_perspective_lut_texture_entry.texture.viewDimension = WGPUTextureViewDimension_3D;

    // Pure ray marching samples the multi-scattering LUT directly instead of the sky-view/aerial-perspective LUTs.
    WGPUBindGroupLayoutEntry multi_scattering_lut_texture_entry {};
    multi_scattering_lut_texture_entry.visibility = WGPUShaderStage_Compute;
    multi_scattering_lut_texture_entry.texture.multisampled = false;
    multi_scattering_lut_texture_entry.texture.sampleType = WGPUTextureSampleType_Float;
    multi_scattering_lut_texture_entry.texture.viewDimension = WGPUTextureViewDimension_2D;

    WGPUBindGroupLayoutEntry depth_buffer_entry {};
    depth_buffer_entry.visibility = WGPUShaderStage_Compute;
    depth_buffer_entry.texture = {};
    depth_buffer_entry.texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;
    depth_buffer_entry.texture.viewDimension = config.skyRenderer.depthBuffer.view->descriptor().dimension;

    WGPUBindGroupLayoutEntry back_buffer_entry {};
    back_buffer_entry.visibility = WGPUShaderStage_Compute;
    back_buffer_entry.texture = {};
    back_buffer_entry.texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;
    back_buffer_entry.texture.viewDimension = config.skyRenderer.backBuffer.view->descriptor().dimension;

    WGPUBindGroupLayoutEntry render_target_entry {};
    render_target_entry.visibility = WGPUShaderStage_Compute;
    render_target_entry.storageTexture = {};
    render_target_entry.storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
    render_target_entry.storageTexture.format = config.skyRenderer.renderTarget.texture->descriptor().format;
    render_target_entry.storageTexture.viewDimension = config.skyRenderer.renderTarget.view->descriptor().dimension;

    std::vector<WGPUBindGroupLayoutEntry> entries {};
    entries.push_back(atmosphere_buffer_entry);
    if (!config.useCustomUniformSources) {
        entries.push_back(uniforms_buffer_entry);
    }
    entries.push_back(sampler_entry);
    entries.push_back(transmittance_lut_texture_entry);
    if (is_ray_march_pass) {
        entries.push_back(multi_scattering_lut_texture_entry);
    } else {
        entries.push_back(sky_view_lut_texture_entry);
        entries.push_back(aerial_perspective_lut_texture_entry);
    }
    entries.push_back(depth_buffer_entry);
    entries.push_back(back_buffer_entry);
    entries.push_back(render_target_entry);
    for (uint32_t i = 0; i < entries.size(); i++) {
        entries[i].binding = i;
    }

    return std::make_unique<webgpu::raii::BindGroupLayout>(
        device, entries, is_ray_march_pass ? "Render sky (ray march) bind group layout" : "Render sky with LUTs bind group layout");
}

std::unique_ptr<webgpu::raii::ComputePipeline> SkyWithLutsComputeRenderer::make_compute_pipeline(WGPUDevice device, config::SkyAtmosphereRendererConfig config,
    WGPUPipelineLayout pipeline_layout, WGPUShaderModule shader_module, lut::SkyAtmosphereLutRenderer& lut_renderer, bool is_ray_march_pass)
{
    // Constants shared by both the LUT-sampling and pure-ray-march shader variants.
    std::vector<WGPUConstantEntry> constants = {
        {
            .nextInChain = nullptr,
            .key = sv("IS_REVERSE_Z"),
            .value = double(config.skyRenderer.depthBuffer.reverseZ),
        },
        {
            .nextInChain = nullptr,
            .key = sv("FROM_KM_SCALE"),
            .value = double(config.fromKilometersScale),
        },
        {
            .nextInChain = nullptr,
            .key = sv("RENDER_SUN_DISK"),
            .value = double(config.lights.renderSunDisk),
        },
        {
            .nextInChain = nullptr,
            .key = sv("RENDER_MOON_DISK"),
            .value = double(config.lights.renderMoonDisk && config.lights.useMoon),
        },
        {
            .nextInChain = nullptr,
            .key = sv("LIMB_DARKENING_ON_SUN"),
            .value = double(config.lights.applyLimbDarkeningOnSun),
        },
        {
            .nextInChain = nullptr,
            .key = sv("LIMB_DARKENING_ON_MOON"),
            .value = double(config.lights.applyLimbDarkeningOnMoon),
        },
        {
            .nextInChain = nullptr,
            .key = sv("USE_MOON"),
            .value = double(config.lights.useMoon),
        },
    };

    if (is_ray_march_pass) {
        constants.push_back({
            .nextInChain = nullptr,
            .key = sv("INV_DISTANCE_TO_MAX_SAMPLE_COUNT"),
            .value = 1.0 / double(config.skyRenderer.distanceToMaxSampleCount),
        });
        constants.push_back({
            .nextInChain = nullptr,
            .key = sv("USE_COLORED_TRANSMISSION"),
            .value = double(config.skyRenderer.rayMarch.useColoredTransmittance),
        });
        constants.push_back({
            .nextInChain = nullptr,
            .key = sv("RANDOMIZE_SAMPLE_OFFSET"),
            .value = double(config.skyRenderer.rayMarch.randomizeRayOffsets),
        });
    } else {
        constants.push_back({
            .nextInChain = nullptr,
            .key = sv("AP_SLICE_COUNT"),
            .value = double(lut_renderer.resources().aerial_perspective_lut().texture().depth_or_num_layers()),
        });
        constants.push_back({
            .nextInChain = nullptr,
            .key = sv("AP_DISTANCE_PER_SLICE"),
            .value = double(lut_renderer.pipelines().aerial_perspective_lut_pipeline().aerial_perspective_distance_per_slice()),
        });
        constants.push_back({
            .nextInChain = nullptr,
            .key = sv("AP_INV_DISTANCE_PER_SLICE"),
            .value = double(lut_renderer.pipelines().aerial_perspective_lut_pipeline().aerial_perspective_inv_distance_per_slice()),
        });
        constants.push_back({
            .nextInChain = nullptr,
            .key = sv("SKY_VIEW_LUT_RES_X"),
            .value = double(lut_renderer.resources().sky_view_lut().texture().width()),
        });
        constants.push_back({
            .nextInChain = nullptr,
            .key = sv("SKY_VIEW_LUT_RES_Y"),
            .value = double(lut_renderer.resources().sky_view_lut().texture().height()),
        });
    }

    WGPUComputePipelineDescriptor descriptor {};
    descriptor.label = is_ray_march_pass ? sv("Render sky (ray march) pipeline") : sv("Render sky with LUTs pipeline");
    descriptor.layout = pipeline_layout;
    descriptor.compute = {};
    descriptor.compute.entryPoint = sv("render_sky_atmosphere");
    descriptor.compute.module = shader_module;
    descriptor.compute.constantCount = constants.size();
    descriptor.compute.constants = constants.data();
    return std::make_unique<webgpu::raii::ComputePipeline>(device, descriptor);
}

std::unique_ptr<SkyWithLutsComputeRenderer> SkyWithLutsComputeRenderer::create(
    WGPUDevice device, webgpu::RenderResourceRegistry& registry, config::SkyAtmosphereRendererConfig config)
{
    auto lut_renderer = lut::SkyAtmosphereLutRenderer::create(device, registry, config);

    // defaultToPerPixelRayMarch selects the pure ray-march pass (samples multi_scattering_lut directly)
    // instead of the LUT-sampling pass (samples sky_view_lut/aerial_perspective_lut). Constant LUTs
    // (transmittance, multi-scattering) are always needed; the dynamic LUTs are skipped for the ray-march
    // pass (see render_dynamic_luts) since it doesn't sample them.
    // TODO(chunk 5): rayMarchDistantSky=false should instead select the hybrid render_sky_luts_and_raymarch
    // pass (ray-march near geometry, sky-view LUT for distant sky) rather than pure ray marching.
    const bool is_ray_march_pass = config.skyRenderer.defaultToPerPixelRayMarch;

    auto bind_group_layout = make_bind_group_layout(device, config, is_ray_march_pass);

    std::vector<WGPUBindGroupLayout> layouts { bind_group_layout->handle() };
    for (const auto& custom_bind_group_layout : config.customUniformsSource.bindGroupLayouts) {
        layouts.push_back(custom_bind_group_layout);
    }
    auto pipeline_layout = std::make_unique<webgpu::raii::PipelineLayout>(
        device, layouts, is_ray_march_pass ? "Render sky (ray march) pipeline layout" : "Render sky with LUTs pipeline layout");

    const char* shader_name = is_ray_march_pass ? "sky_render_raymarching" : "sky_render_with_luts";
    const char* shader_path = is_ray_march_pass ? "webgpu_engine::sky/render_sky_raymarching" : "webgpu_engine::sky/render_sky_with_luts";
    registry.register_shader(shader_name, shader_path);

    auto pipeline
        = make_compute_pipeline(device, config, pipeline_layout->handle(), registry.shader(shader_name).handle(), *lut_renderer, is_ray_march_pass);
    return std::make_unique<SkyWithLutsComputeRenderer>(
        std::move(lut_renderer), std::move(bind_group_layout), std::move(pipeline_layout), std::move(pipeline), config, is_ray_march_pass);
}

} // namespace webgpu_engine::sky::compute
