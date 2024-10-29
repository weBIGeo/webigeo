/*
 * Copyright (c) 2024 Lukas Herzberger
 * SPDX-License-Identifier: MIT
 */

// TODO copyright notice?

#include "sky_renderer.h"

namespace webgpu_engine::atmosphere::sky {

SkyWithLutsComputeRenderer::SkyWithLutsComputeRenderer(std::unique_ptr<lut::SkyAtmosphereLutRenderer> lut_renderer,
    std::unique_ptr<webgpu::raii::BindGroupLayout> bind_group_layout, std::unique_ptr<webgpu::raii::PipelineLayout> pipeline_layout,
    std::unique_ptr<webgpu::raii::ComputePipeline> pipeline, config::SkyAtmosphereRendererConfig config, bool is_ray_march_pass)
    : m_lut_renderer(std::move(lut_renderer))
    , m_bind_group_layout(std::move(bind_group_layout))
    , m_pipeline_layout(std::move(pipeline_layout))
    , m_pipeline(std::move(pipeline))
    , m_does_ray_march_distant_sky { config.skyRenderer.defaultToPerPixelRayMarch }
{ // TODO LEFT OFF HERE
  // create bind groups
  // dispatch dimensions
  // create compute pass
  // then create (forwarding) methods to render luts and sky

    std::vector<std::unique_ptr<webgpu::raii::BindGroup>> bind_groups;
    bind_groups.push_back(
        make_bind_group(m_bind_group_layout->handle(), config.skyRenderer, m_lut_renderer->resources(), m_lut_renderer->uses_custom_uniforms()));
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

void SkyWithLutsComputeRenderer::render_dynamic_luts(WGPUComputePassEncoder pass_encoder) { m_lut_renderer->render_dynamic_luts(pass_encoder); }

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

std::string SkyWithLutsComputeRenderer::make_shader_code(WGPUTextureFormat render_target_format, std::string custom_uniforms_code)
{
    QString base = QString::fromStdString(ShaderModuleManager::load_and_preprocess_without_cache("atmosphere/common/constants.wgsl"))
                       .append(ShaderModuleManager::load_and_preprocess_without_cache("atmosphere/common/intersection.wgsl"))
                       .append(ShaderModuleManager::load_and_preprocess_without_cache("atmosphere/common/medium.wgsl"))
                       .append(ShaderModuleManager::load_and_preprocess_without_cache("atmosphere/common/uv.wgsl"))
                       .append(ShaderModuleManager::load_and_preprocess_without_cache("atmosphere/common/uniforms.wgsl"));
    if (!custom_uniforms_code.empty()) {
        base.append(custom_uniforms_code);
        base.append(ShaderModuleManager::load_and_preprocess_without_cache("atmosphere/common/custom_uniforms.wgsl"));
    }
    base.append(ShaderModuleManager::load_and_preprocess_without_cache("atmosphere/common/coordinate_system.wgsl"))
        .append(ShaderModuleManager::load_and_preprocess_without_cache("atmosphere/common/aerial_perspective.wgsl"))
        .append(ShaderModuleManager::load_and_preprocess_without_cache("atmosphere/common/sky_view.wgsl"))
        .append(ShaderModuleManager::load_and_preprocess_without_cache("atmosphere/common/blend.wgsl"))
        .append(ShaderModuleManager::load_and_preprocess_without_cache("atmosphere/common/sun_disk.wgsl"))
        .append(ShaderModuleManager::load_and_preprocess_without_cache("atmosphere/common/vertex_full_screen.wgsl"))
        .append(ShaderModuleManager::load_and_preprocess_without_cache("atmosphere/common/sample_sagment_t.wgsl"));

    QString shader = QString::fromStdString(ShaderModuleManager::load_and_preprocess_without_cache("atmosphere/render_sky_with_luts.wgsl"));
    // .replace("rgba16float", "rgba8unorm");
    // TODO replace render target format string

    if (!custom_uniforms_code.empty()) {
        shader.replace("let config = config_buffer", "let config = get_uniforms()");
        shader.replace("@group(0) @binding(1) var<uniform> config_buffer: Uniforms;", "");
        for (int i = 2; i < 9; ++i) {
            shader.replace(QString("group(0) @binding(%1)").arg(i), QString("group(0) @binding(%1)").arg(i - 1));
        }
    }
    return base.append(shader).toStdString();
}

std::unique_ptr<webgpu::raii::BindGroup> SkyWithLutsComputeRenderer::make_bind_group(
    WGPUBindGroupLayout layout, config::SkyRendererComputeConfig compute_config, resources::SkyAtmosphereResources& resources, bool use_custom_uniforms)
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
    bind_group_entries.push_back(resources.sky_view_lut().view().create_bind_group_entry(3 + custom_uniform_offset));
    bind_group_entries.push_back(resources.aerial_perspective_lut().view().create_bind_group_entry(4 + custom_uniform_offset));

    bind_group_entries.push_back(compute_config.depthBuffer.view->create_bind_group_entry(5 + custom_uniform_offset));
    bind_group_entries.push_back(compute_config.backBuffer.view->create_bind_group_entry(6 + custom_uniform_offset));
    bind_group_entries.push_back(compute_config.renderTarget.view->create_bind_group_entry(7 + custom_uniform_offset));

    return std::make_unique<webgpu::raii::BindGroup>(resources.device(), layout, bind_group_entries, "Render sky with LUTs bind group");
}

std::unique_ptr<webgpu::raii::BindGroupLayout> SkyWithLutsComputeRenderer::make_bind_group_layout(WGPUDevice device, config::SkyAtmosphereRendererConfig config)
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
    entries.push_back(sky_view_lut_texture_entry);
    entries.push_back(aerial_perspective_lut_texture_entry);
    entries.push_back(depth_buffer_entry);
    entries.push_back(back_buffer_entry);
    entries.push_back(render_target_entry);
    for (uint32_t i = 0; i < entries.size(); i++) {
        entries[i].binding = i;
    }

    return std::make_unique<webgpu::raii::BindGroupLayout>(device, entries, "Render sky with LUTs bind group layout");
}

std::unique_ptr<webgpu::raii::ComputePipeline> SkyWithLutsComputeRenderer::make_compute_pipeline(WGPUDevice device, config::SkyAtmosphereRendererConfig config,
    WGPUPipelineLayout pipeline_layout, WGPUShaderModule shader_module, lut::SkyAtmosphereLutRenderer& lut_renderer)
{

    std::vector<WGPUConstantEntry> constants = {
        {
            .nextInChain = nullptr,
            .key = "AP_SLICE_COUNT",
            .value = double(lut_renderer.resources().aerial_perspective_lut().texture().depth_or_num_layers()),
        },
        {
            .nextInChain = nullptr,
            .key = "AP_DISTANCE_PER_SLICE",
            .value = double(lut_renderer.pipelines().aerial_perspective_lut_pipeline().aerial_perspective_distance_per_slice()),
        },
        {
            .nextInChain = nullptr,
            .key = "AP_INV_DISTANCE_PER_SLICE",
            .value = double(lut_renderer.pipelines().aerial_perspective_lut_pipeline().aerial_perspective_inv_distance_per_slice()),
        },
        {
            .nextInChain = nullptr,
            .key = "SKY_VIEW_LUT_RES_X",
            .value = double(lut_renderer.resources().sky_view_lut().texture().width()),
        },
        {
            .nextInChain = nullptr,
            .key = "SKY_VIEW_LUT_RES_Y",
            .value = double(lut_renderer.resources().sky_view_lut().texture().height()),
        },
        {
            .nextInChain = nullptr,
            .key = "IS_REVERSE_Z",
            .value = double(config.skyRenderer.depthBuffer.reverseZ),
        },
        {
            .nextInChain = nullptr,
            .key = "FROM_KM_SCALE",
            .value = double(config.fromKilometersScale),
        },
        {
            .nextInChain = nullptr,
            .key = "RENDER_SUN_DISK",
            .value = double(config.lights.renderSunDisk),
        },
        {
            .nextInChain = nullptr,
            .key = "RENDER_MOON_DISK",
            .value = double(config.lights.renderMoonDisk && config.lights.useMoon),
        },
        {
            .nextInChain = nullptr,
            .key = "LIMB_DARKENING_ON_SUN",
            .value = double(config.lights.applyLimbDarkeningOnSun),
        },
        {
            .nextInChain = nullptr,
            .key = "LIMB_DARKENING_ON_MOON",
            .value = double(config.lights.applyLimbDarkeningOnMoon),
        },
        {
            .nextInChain = nullptr,
            .key = "USE_MOON",
            .value = double(config.lights.useMoon),
        },
    };

    WGPUComputePipelineDescriptor descriptor {};
    descriptor.label = "Render sky with LUTs pipeline";
    descriptor.layout = pipeline_layout;
    descriptor.compute = {};
    descriptor.compute.entryPoint = "render_sky_atmosphere";
    descriptor.compute.module = shader_module;
    descriptor.compute.constantCount = constants.size();
    descriptor.compute.constants = constants.data();
    return std::make_unique<webgpu::raii::ComputePipeline>(device, descriptor);
}

std::unique_ptr<SkyWithLutsComputeRenderer> SkyWithLutsComputeRenderer::create(WGPUDevice device, config::SkyAtmosphereRendererConfig config)
{
    auto lut_renderer = lut::SkyAtmosphereLutRenderer::create(device, config);

    auto bind_group_layout = make_bind_group_layout(device, config);

    std::vector<WGPUBindGroupLayout> layouts { bind_group_layout->handle() };
    for (const auto& custom_bind_group_layout : config.customUniformsSource.bindGroupLayouts) {
        layouts.push_back(custom_bind_group_layout);
    }
    auto pipeline_layout = std::make_unique<webgpu::raii::PipelineLayout>(device, layouts, "Render sky with LUTs pipeline layout");

    auto shader_code = make_shader_code(config.skyRenderer.renderTarget.texture->descriptor().format, config.customUniformsSource.wgslCode);
    auto shader_module = ShaderModuleManager::create_shader_module(device, "Render sky with LUTs shader", shader_code);

    auto pipeline = make_compute_pipeline(device, config, pipeline_layout->handle(), shader_module->handle(), *lut_renderer);
    return std::make_unique<SkyWithLutsComputeRenderer>(
        std::move(lut_renderer), std::move(bind_group_layout), std::move(pipeline_layout), std::move(pipeline), config);
}

} // namespace webgpu_engine::atmosphere::sky
