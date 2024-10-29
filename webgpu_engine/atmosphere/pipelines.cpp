/*
 * Copyright (c) 2024 Lukas Herzberger
 * SPDX-License-Identifier: MIT
 */

// TODO copyright notice?

#include "pipelines.h"

namespace webgpu_engine::atmosphere::pipelines {

TransmittanceLutPipeline::TransmittanceLutPipeline(WGPUDevice device, std::unique_ptr<webgpu::raii::BindGroupLayout> bind_group_layout,
    std::unique_ptr<webgpu::raii::PipelineLayout> pipeline_layout, std::unique_ptr<webgpu::raii::ComputePipeline> pipeline,
    WGPUTextureFormat transmittance_lut_format)
    : m_device { device }
    , m_bind_group_layout(std::move(bind_group_layout))
    , m_pipeline_layout(std::move(pipeline_layout))
    , m_pipeline(std::move(pipeline))
    , m_transmittance_lut_format { transmittance_lut_format }
{
}

std::string TransmittanceLutPipeline::make_shader_code(WGPUTextureFormat transmittance_lut_format)
{
    // TODO replace transmittance lut format string
    return ShaderModuleManager::load_and_preprocess_without_cache("atmosphere/render_transmittance_lut.wgsl");
}

std::unique_ptr<webgpu::raii::BindGroupLayout> TransmittanceLutPipeline::make_bind_group_layout(WGPUDevice device, WGPUTextureFormat transmittanceLutFormat)
{
    WGPUBindGroupLayoutEntry atmosphere_buffer_entry {};
    atmosphere_buffer_entry.binding = 0;
    atmosphere_buffer_entry.visibility = WGPUShaderStage_Compute;
    atmosphere_buffer_entry.buffer.type = WGPUBufferBindingType_Uniform;
    atmosphere_buffer_entry.buffer.hasDynamicOffset = false;
    atmosphere_buffer_entry.buffer.minBindingSize = 0;

    WGPUBindGroupLayoutEntry transmittance_lut_texture_entry {};
    transmittance_lut_texture_entry.binding = 1;
    transmittance_lut_texture_entry.visibility = WGPUShaderStage_Compute;
    transmittance_lut_texture_entry.storageTexture.viewDimension = WGPUTextureViewDimension_2D;
    transmittance_lut_texture_entry.storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
    transmittance_lut_texture_entry.storageTexture.format = transmittanceLutFormat;

    return std::make_unique<webgpu::raii::BindGroupLayout>(
        device, std::vector<WGPUBindGroupLayoutEntry> { atmosphere_buffer_entry, transmittance_lut_texture_entry }, "transmittance LUT bind group");
}

std::unique_ptr<webgpu::raii::ComputePipeline> TransmittanceLutPipeline::make_compute_pipeline(
    WGPUDevice device, WGPUPipelineLayout pipeline_layout, WGPUShaderModule shader_module, uint32_t sample_count)
{
    WGPUConstantEntry entry {};
    entry.key = "SAMPLE_COUNT";
    entry.value = std::max(double(sample_count), double(DEFAULT_TRANSMITTANCE_LUT_SAMPLE_COUNT));
    std::vector<WGPUConstantEntry> constants = { entry };

    WGPUComputePipelineDescriptor descriptor {};
    descriptor.label = "transmittance LUT";
    descriptor.layout = pipeline_layout;
    descriptor.compute = {};
    descriptor.compute.entryPoint = "render_transmittance_lut";
    descriptor.compute.module = shader_module;
    descriptor.compute.constantCount = constants.size();
    descriptor.compute.constants = constants.data();
    return std::make_unique<webgpu::raii::ComputePipeline>(device, descriptor);
}

std::unique_ptr<TransmittanceLutPipeline> TransmittanceLutPipeline::create(WGPUDevice device, WGPUTextureFormat transmittance_lut_format, uint32_t sample_count)
{
    auto bind_group_layout = make_bind_group_layout(device, transmittance_lut_format);
    auto pipeline_layout
        = std::make_unique<webgpu::raii::PipelineLayout>(device, std::vector<WGPUBindGroupLayout> { bind_group_layout->handle() }, "transmittance LUT");
    auto shader_module = ShaderModuleManager::create_shader_module(device, "transmittance LUT shader", make_shader_code(transmittance_lut_format));
    auto pipeline = make_compute_pipeline(device, pipeline_layout->handle(), shader_module->handle(), sample_count);
    return std::make_unique<TransmittanceLutPipeline>(
        device, std::move(bind_group_layout), std::move(pipeline_layout), std::move(pipeline), transmittance_lut_format);
}

std::unique_ptr<util::ComputePass> TransmittanceLutPipeline::make_compute_pass(const resources::SkyAtmosphereResources& resources)
{
    if (resources.device() != m_device) {
        qFatal() << "[TransmittanceLutPipeline::makeComputePass]: device mismatch";
    }

    // TODO check
    if (resources.atmosphere_buffer().raw_buffer().descriptor().size != sizeof(resources::AtmosphereUniform)) {
        qFatal() << "[TransmittanceLutPipeline::makeComputePass]: buffer too small for atmosphere parameters (${resources.atmosphereBuffer.size} < "
                    "${ATMOSPHERE_BUFFER_SIZE})";
    }

    if (resources.transmittance_lut().texture().descriptor().format != m_transmittance_lut_format) {
        qFatal() << "[TransmittanceLutPipeline::makeComputePass]: wrong texture format for transmittance LUT. expected "
                    "'${this.transmittanceLutFormat}', got ${resources.transmittanceLut.texture.format}";
    }

    std::vector<std::unique_ptr<webgpu::raii::BindGroup>> bind_groups;
    bind_groups.push_back(std::make_unique<webgpu::raii::BindGroup>(m_device, *m_bind_group_layout,
        std::vector<WGPUBindGroupEntry> {
            resources.atmosphere_buffer().raw_buffer().create_bind_group_entry(0),
            resources.transmittance_lut().view().create_bind_group_entry(1),
        },
        "transmittance LUT pass"));

    glm::uvec3 dispatch_group_dimensions = glm::uvec3(glm::ceil(glm::vec3 {
        float(resources.transmittance_lut().texture().descriptor().size.width) / 16.0f,
        float(resources.transmittance_lut().texture().descriptor().size.height) / 16.0f,
        1.0f,
    }));
    return std::make_unique<util::ComputePass>(m_pipeline->handle(), bind_groups, dispatch_group_dimensions);
}

MultiScatteringLutPipeline::MultiScatteringLutPipeline(WGPUDevice device, std::unique_ptr<webgpu::raii::BindGroupLayout> bind_group_layout,
    std::unique_ptr<webgpu::raii::PipelineLayout> pipeline_layout, std::unique_ptr<webgpu::raii::ComputePipeline> pipeline,
    WGPUTextureFormat multi_scattering_lut_format)
    : m_device { device }
    , m_bind_group_layout(std::move(bind_group_layout))
    , m_pipeline_layout(std::move(pipeline_layout))
    , m_pipeline(std::move(pipeline))
    , m_multi_scattering_lut_format { multi_scattering_lut_format }
{
}

std::string MultiScatteringLutPipeline::make_shader_code(WGPUTextureFormat multi_scattering_lut_format)
{
    QString code = QString::fromStdString(ShaderModuleManager::load_and_preprocess_without_cache("atmosphere/render_multi_scattering_lut.wgsl"));
    code.append(shaders::make_phase_shader_code());
    // TODO replace multi scattering lut format string
    return code.toStdString();
}

std::unique_ptr<webgpu::raii::BindGroupLayout> MultiScatteringLutPipeline::make_bind_group_layout(
    WGPUDevice device, WGPUTextureFormat multi_scattering_lut_format)
{
    WGPUBindGroupLayoutEntry atmosphere_buffer_entry {};
    atmosphere_buffer_entry.binding = 0;
    atmosphere_buffer_entry.visibility = WGPUShaderStage_Compute;
    atmosphere_buffer_entry.buffer.type = WGPUBufferBindingType_Uniform;
    atmosphere_buffer_entry.buffer.hasDynamicOffset = false;
    atmosphere_buffer_entry.buffer.minBindingSize = 0;

    WGPUBindGroupLayoutEntry sampler_entry {};
    sampler_entry.binding = 1;
    sampler_entry.visibility = WGPUShaderStage_Compute;
    sampler_entry.sampler.type = WGPUSamplerBindingType_Filtering;

    WGPUBindGroupLayoutEntry transmittance_lut_texture_entry {};
    transmittance_lut_texture_entry.binding = 2;
    transmittance_lut_texture_entry.visibility = WGPUShaderStage_Compute;
    transmittance_lut_texture_entry.texture.multisampled = false;
    transmittance_lut_texture_entry.texture.sampleType = WGPUTextureSampleType_Float;
    transmittance_lut_texture_entry.texture.viewDimension = WGPUTextureViewDimension_2D;

    WGPUBindGroupLayoutEntry multiscattering_lut_texture_entry {};
    multiscattering_lut_texture_entry.binding = 3;
    multiscattering_lut_texture_entry.visibility = WGPUShaderStage_Compute;
    multiscattering_lut_texture_entry.storageTexture.viewDimension = WGPUTextureViewDimension_2D;
    multiscattering_lut_texture_entry.storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
    multiscattering_lut_texture_entry.storageTexture.format = multi_scattering_lut_format;

    return std::make_unique<webgpu::raii::BindGroupLayout>(device,
        std::vector<WGPUBindGroupLayoutEntry> { atmosphere_buffer_entry, sampler_entry, transmittance_lut_texture_entry, multiscattering_lut_texture_entry },
        "multi scattering LUT bind group");
}

std::unique_ptr<webgpu::raii::ComputePipeline> MultiScatteringLutPipeline::make_compute_pipeline(
    WGPUDevice device, WGPUPipelineLayout pipeline_layout, WGPUShaderModule shader_module, uint32_t sample_count)
{
    WGPUConstantEntry entry {};
    entry.key = "SAMPLE_COUNT";
    entry.value = std::max(double(sample_count), double(MULTI_SCATTERING_LUT_MIN_SAMPLE_COUNT));
    std::vector<WGPUConstantEntry> constants = { entry };

    WGPUComputePipelineDescriptor descriptor {};
    descriptor.label = "multi scattering LUT pass";
    descriptor.layout = pipeline_layout;
    descriptor.compute = {};
    descriptor.compute.entryPoint = "render_multi_scattering_lut";
    descriptor.compute.module = shader_module;
    descriptor.compute.constantCount = constants.size();
    descriptor.compute.constants = constants.data();
    return std::make_unique<webgpu::raii::ComputePipeline>(device, descriptor);
}

std::unique_ptr<MultiScatteringLutPipeline> MultiScatteringLutPipeline::create(
    WGPUDevice device, WGPUTextureFormat multi_scattering_lut_format, uint32_t sample_count)
{
    auto bind_group_layout = make_bind_group_layout(device, multi_scattering_lut_format);
    auto pipeline_layout
        = std::make_unique<webgpu::raii::PipelineLayout>(device, std::vector<WGPUBindGroupLayout> { bind_group_layout->handle() }, "multi scattering LUT");
    auto shader_module = ShaderModuleManager::create_shader_module(device, "multi scattering LUT shader", make_shader_code(multi_scattering_lut_format));
    auto pipeline = make_compute_pipeline(device, pipeline_layout->handle(), shader_module->handle(), sample_count);
    return std::make_unique<MultiScatteringLutPipeline>(
        device, std::move(bind_group_layout), std::move(pipeline_layout), std::move(pipeline), multi_scattering_lut_format);
}

std::unique_ptr<util::ComputePass> MultiScatteringLutPipeline::make_compute_pass(const resources::SkyAtmosphereResources& resources)
{
    if (resources.device() != m_device) {
        qFatal() << "[MultiScatteringLutPipeline::makeComputePass]: device mismatch";
    }

    // TODO check
    if (resources.atmosphere_buffer().raw_buffer().descriptor().size != sizeof(resources::AtmosphereUniform)) {
        qFatal() << "[MultiScatteringLutPipeline::makeComputePass]: buffer too small for atmosphere parameters (${resources.atmosphereBuffer.size} "
                    "<${ATMOSPHERE_BUFFER_SIZE})";
    }

    if (resources.multi_scattering_lut().texture().descriptor().format != m_multi_scattering_lut_format) {
        qFatal() << "[MultiScatteringLutPipeline::makeComputePass]: wrong texture format for multiple scattering LUT. expected "
                    "'${this.multiScatteringLutFormat}', got ${resources.multiScatteringLut.texture.format}";
    }

    std::vector<std::unique_ptr<webgpu::raii::BindGroup>> bind_groups;
    bind_groups.push_back(std::make_unique<webgpu::raii::BindGroup>(m_device, *m_bind_group_layout,
        std::vector<WGPUBindGroupEntry> {
            resources.atmosphere_buffer().raw_buffer().create_bind_group_entry(0),
            resources.lut_sampler().create_bind_group_entry(1),
            resources.transmittance_lut().view().create_bind_group_entry(2),
            resources.multi_scattering_lut().view().create_bind_group_entry(3),
        },
        "multiple scattering LUT pass"));

    glm::uvec3 dispatch_group_dimensions = glm::uvec3 {
        resources.multi_scattering_lut().texture().width(),
        resources.multi_scattering_lut().texture().height(),
        1u,
    };
    return std::make_unique<util::ComputePass>(m_pipeline->handle(), bind_groups, dispatch_group_dimensions);
}

SkyViewLutPipeline::SkyViewLutPipeline(WGPUDevice device, std::unique_ptr<webgpu::raii::BindGroupLayout> bind_group_layout,
    std::unique_ptr<webgpu::raii::PipelineLayout> pipeline_layout, std::unique_ptr<webgpu::raii::ComputePipeline> pipeline,
    WGPUTextureFormat sky_view_lut_format, glm::uvec2 sky_view_lut_size, glm::uvec2 multi_scattering_lut_size)
    : m_device { device }
    , m_bind_group_layout(std::move(bind_group_layout))
    , m_pipeline_layout(std::move(pipeline_layout))
    , m_pipeline(std::move(pipeline))
    , m_sky_view_lut_format { sky_view_lut_format }
    , m_sky_view_lut_size { sky_view_lut_size }
    , m_multi_scattering_lut_size { multi_scattering_lut_size }
{
}

std::string SkyViewLutPipeline::make_shader_code(
    WGPUTextureFormat sky_view_lut_format, const std::string& shadow_code, const std::string& custom_uniforms_code, std::optional<float> const_droplet_diameter)
{
    QString base = QString::fromStdString(ShaderModuleManager::load_and_preprocess_without_cache("atmosphere/common/constants.wgsl"))
                       .append(ShaderModuleManager::load_and_preprocess_without_cache("atmosphere/common/intersection.wgsl"))
                       .append(ShaderModuleManager::load_and_preprocess_without_cache("atmosphere/common/medium.wgsl"))
                       .append(shaders::make_phase_shader_code(const_droplet_diameter))
                       .append(ShaderModuleManager::load_and_preprocess_without_cache("atmosphere/common/uv.wgsl"))
                       .append(ShaderModuleManager::load_and_preprocess_without_cache("atmosphere/common/uniforms.wgsl"));
    if (!custom_uniforms_code.empty()) {
        base.append(custom_uniforms_code).append(ShaderModuleManager::load_and_preprocess_without_cache("atmosphere/common/custom_uniforms.wgsl"));
    }
    base.append(ShaderModuleManager::load_and_preprocess_without_cache("atmosphere/common/coordinate_system.wgsl"))
        .append(ShaderModuleManager::load_and_preprocess_without_cache("atmosphere/common/multiple_scattering.wgsl"));

    QString shader = QString::fromStdString(ShaderModuleManager::load_and_preprocess_without_cache("atmosphere/render_sky_view_lut.wgsl"));
    // TODO replace multi scattering lut format string

    if (!custom_uniforms_code.empty()) {
        shader.replace("let config = config_buffer", "let config = get_uniforms()");
        shader.replace("@group(0) @binding(1) var<uniform> config_buffer: Uniforms;", "");
        for (int i = 2; i < 6; ++i) {
            shader.replace(QString("group(0) @binding(%1)").arg(i), QString("group(0) @binding(%1)").arg(i - 1));
        }
    }
    return QString::fromStdString(shaders::make_shadow_shader_code(shadow_code)).append(base).append(shader).toStdString();
}

std::unique_ptr<webgpu::raii::BindGroupLayout> SkyViewLutPipeline::make_bind_group_layout(
    WGPUDevice device, WGPUTextureFormat sky_view_lut_format, bool use_custom_uniforms_config)
{
    WGPUBindGroupLayoutEntry atmosphere_buffer_entry {};
    atmosphere_buffer_entry.binding = 0;
    atmosphere_buffer_entry.visibility = WGPUShaderStage_Compute;
    atmosphere_buffer_entry.buffer.type = WGPUBufferBindingType_Uniform;
    atmosphere_buffer_entry.buffer.hasDynamicOffset = false;
    atmosphere_buffer_entry.buffer.minBindingSize = 0;

    const uint32_t binding_offset = uint32_t(!use_custom_uniforms_config);

    WGPUBindGroupLayoutEntry sampler_entry {};
    sampler_entry.binding = 1 + binding_offset;
    sampler_entry.visibility = WGPUShaderStage_Compute;
    sampler_entry.sampler.type = WGPUSamplerBindingType_Filtering;

    WGPUBindGroupLayoutEntry transmittance_lut_texture_entry {};
    transmittance_lut_texture_entry.binding = 2 + binding_offset;
    transmittance_lut_texture_entry.visibility = WGPUShaderStage_Compute;
    transmittance_lut_texture_entry.texture.multisampled = false;
    transmittance_lut_texture_entry.texture.sampleType = WGPUTextureSampleType_Float;
    transmittance_lut_texture_entry.texture.viewDimension = WGPUTextureViewDimension_2D;

    WGPUBindGroupLayoutEntry multi_scattering_lut_texture_entry {};
    multi_scattering_lut_texture_entry.binding = 3 + binding_offset;
    multi_scattering_lut_texture_entry.visibility = WGPUShaderStage_Compute;
    multi_scattering_lut_texture_entry.texture.multisampled = false;
    multi_scattering_lut_texture_entry.texture.sampleType = WGPUTextureSampleType_Float;
    multi_scattering_lut_texture_entry.texture.viewDimension = WGPUTextureViewDimension_2D;

    WGPUBindGroupLayoutEntry sky_view_lut_texture_entry {};
    sky_view_lut_texture_entry.binding = 4 + binding_offset;
    sky_view_lut_texture_entry.visibility = WGPUShaderStage_Compute;
    sky_view_lut_texture_entry.storageTexture.viewDimension = WGPUTextureViewDimension_2D;
    sky_view_lut_texture_entry.storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
    sky_view_lut_texture_entry.storageTexture.format = sky_view_lut_format;

    std::vector<WGPUBindGroupLayoutEntry> entries = {};
    entries.push_back(atmosphere_buffer_entry);
    if (!use_custom_uniforms_config) {
        WGPUBindGroupLayoutEntry uniforms_buffer_entry {};
        uniforms_buffer_entry.binding = 1;
        uniforms_buffer_entry.visibility = WGPUShaderStage_Compute;
        uniforms_buffer_entry.buffer.type = WGPUBufferBindingType_Uniform;
        uniforms_buffer_entry.buffer.hasDynamicOffset = false;
        uniforms_buffer_entry.buffer.minBindingSize = 0;
        entries.emplace_back(uniforms_buffer_entry);
    }
    entries.push_back(sampler_entry);
    entries.push_back(transmittance_lut_texture_entry);
    entries.push_back(multi_scattering_lut_texture_entry);
    entries.push_back(sky_view_lut_texture_entry);

    return std::make_unique<webgpu::raii::BindGroupLayout>(device, entries, "sky view LUT layout");
}

std::unique_ptr<webgpu::raii::ComputePipeline> SkyViewLutPipeline::make_compute_pipeline(WGPUDevice device, WGPUPipelineLayout pipeline_layout,
    WGPUShaderModule shader_module, glm::uvec2 skyViewLutSize, glm::uvec2 multiscatteringLutSize, float distanceToMaxSampleCount,
    float fromKilometersScaleFactor, bool use_moon, config::MieHgDPhaseConfig miePhaseConfig)
{
    WGPUConstantEntry sky_view_lut_res_x_constant {};
    sky_view_lut_res_x_constant.key = "SKY_VIEW_LUT_RES_X";
    sky_view_lut_res_x_constant.value = double(skyViewLutSize.x);

    WGPUConstantEntry sky_view_lut_res_y_constant {};
    sky_view_lut_res_y_constant.key = "SKY_VIEW_LUT_RES_Y";
    sky_view_lut_res_y_constant.value = double(skyViewLutSize.y);

    WGPUConstantEntry inv_distance_to_max_sample_count_constant {};
    inv_distance_to_max_sample_count_constant.key = "INV_DISTANCE_TO_MAX_SAMPLE_COUNT";
    inv_distance_to_max_sample_count_constant.value = 1.0 / double(distanceToMaxSampleCount);

    WGPUConstantEntry multi_scattering_lut_res_x_constant {};
    multi_scattering_lut_res_x_constant.key = "MULTI_SCATTERING_LUT_RES_X";
    multi_scattering_lut_res_x_constant.value = double(multiscatteringLutSize.x);

    WGPUConstantEntry multi_scattering_lut_res_y_constant {};
    multi_scattering_lut_res_y_constant.key = "MULTI_SCATTERING_LUT_RES_Y";
    multi_scattering_lut_res_y_constant.value = double(multiscatteringLutSize.y);

    WGPUConstantEntry from_km_scale_constant {};
    from_km_scale_constant.key = "FROM_KM_SCALE";
    from_km_scale_constant.value = fromKilometersScaleFactor;

    WGPUConstantEntry use_moon_constant {};
    use_moon_constant.key = "USE_MOON";
    use_moon_constant.value = double(use_moon);

    std::vector<WGPUConstantEntry> constants = {
        sky_view_lut_res_x_constant,
        sky_view_lut_res_y_constant,
        inv_distance_to_max_sample_count_constant,
        multi_scattering_lut_res_x_constant,
        multi_scattering_lut_res_y_constant,
        from_km_scale_constant,
        use_moon_constant,
    };

    const auto mie_phase_overrides = makeMiePhaseOverrides(miePhaseConfig);
    for (uint32_t i = 0; i < mie_phase_overrides.size(); i++) {
        constants.push_back(WGPUConstantEntry {
            .nextInChain = nullptr,
            .key = mie_phase_overrides[i].first.c_str(),
            .value = mie_phase_overrides[i].second,
        });
    }

    WGPUComputePipelineDescriptor descriptor {};
    descriptor.label = "sky view LUT pass";
    descriptor.layout = pipeline_layout;
    descriptor.compute = {};
    descriptor.compute.entryPoint = "render_sky_view_lut";
    descriptor.compute.module = shader_module;
    descriptor.compute.constantCount = constants.size();
    descriptor.compute.constants = constants.data();
    return std::make_unique<webgpu::raii::ComputePipeline>(device, descriptor);
}

std::unique_ptr<SkyViewLutPipeline> SkyViewLutPipeline::create(WGPUDevice device, WGPUTextureFormat sky_view_lut_format, glm::uvec2 sky_view_lut_size,
    glm::uvec2 multi_scattering_lut_size, float distanceToMaxSampleCount, float fromKilometersScaleFactor, bool useMoon, config::ShadowConfig shadowConfig,
    config::CustomUniformsSourceConfig customUniformsConfig, config::MieHgDPhaseConfig miePhaseConfig)
{
    auto bind_group_layout = make_bind_group_layout(device, sky_view_lut_format);

    std::vector<WGPUBindGroupLayout> layouts { bind_group_layout->handle() };
    for (const auto& shadow_bind_group_layout : shadowConfig.bindGroupLayouts) {
        layouts.push_back(shadow_bind_group_layout);
    }
    for (const auto& custom_bind_group_layout : customUniformsConfig.bindGroupLayouts) {
        layouts.push_back(custom_bind_group_layout);
    }
    auto pipeline_layout = std::make_unique<webgpu::raii::PipelineLayout>(device, layouts, "sky view LUT");

    const std::string shader_code
        = make_shader_code(sky_view_lut_format, shadowConfig.wgslCode, customUniformsConfig.wgslCode, miePhaseConfig.constantDropletDiameter);
    auto shader_module = ShaderModuleManager::create_shader_module(device, "sky view LUT shader", shader_code);
    auto pipeline = make_compute_pipeline(device, pipeline_layout->handle(), shader_module->handle(), sky_view_lut_size, multi_scattering_lut_size,
        distanceToMaxSampleCount, fromKilometersScaleFactor, useMoon, miePhaseConfig);

    return std::make_unique<SkyViewLutPipeline>(device, std::move(bind_group_layout), std::move(pipeline_layout), std::move(pipeline), sky_view_lut_format,
        sky_view_lut_size, multi_scattering_lut_size);
}

std::unique_ptr<util::ComputePass> SkyViewLutPipeline::make_compute_pass(const resources::SkyAtmosphereResources& resources,
    const std::vector<WGPUBindGroup>& shadow_bind_groups, const std::vector<WGPUBindGroup>& custom_uniforms_bind_groups)
{
    if (resources.device() != m_device) {
        qFatal() << "[SkyViewLutPipeline::makeComputePass]: device mismatch";
    }

    // TODO check
    if (resources.atmosphere_buffer().raw_buffer().size_in_byte() < sizeof(resources::AtmosphereUniform)) {
        qFatal() << "[SkyViewLutPipeline::makeComputePass]: buffer too small for atmosphere parameters (${resources.atmosphereBuffer.size} < "
                    "${ATMOSPHERE_BUFFER_SIZE})";
    }
    if (resources.has_uniforms_buffer() && resources.uniforms_buffer().raw_buffer().size_in_byte() < sizeof(uniforms::Uniforms)) {
        qFatal() << "[SkyViewLutPipeline::makeComputePass]: buffer too small for config (${resources.atmosphereBuffer.size} < ${ATMOSPHERE_BUFFER_SIZE})";
    }
    if (resources.multi_scattering_lut().texture().width() != m_multi_scattering_lut_size.x
        || resources.multi_scattering_lut().texture().height() != m_multi_scattering_lut_size.y) {
        qFatal() << "[SkyViewLutPipeline::makeComputePass]: wrong texture size for multiple scattering LUT. expected [" << m_multi_scattering_lut_size.x << ", "
                 << m_multi_scattering_lut_size.y << "], got [" << resources.multi_scattering_lut().texture().width() << ", " << ""
                 << resources.multi_scattering_lut().texture().height() << "]";
    }
    if (resources.sky_view_lut().texture().descriptor().format != m_sky_view_lut_format) {
        qFatal() << "[SkyViewLutPipeline::makeComputePass]: wrong texture format for sky view LUT. expected '${this.skyViewLutFormat}', got "
                    "${resources.skyViewLut.texture.format";
    }
    if (resources.sky_view_lut().texture().width() != m_sky_view_lut_size.x || resources.sky_view_lut().texture().height() != m_sky_view_lut_size.y) {
        qFatal() << "[SkyViewLutPipeline::makeComputePass]: wrong texture size for sky view LUT. expected [" << m_sky_view_lut_size.x << ", "
                 << m_sky_view_lut_size.y << "], got [" << resources.sky_view_lut().texture().width() << ", " << ""
                 << resources.sky_view_lut().texture().height() << "]";
    }

    std::vector<WGPUBindGroupEntry> bind_group_entries {
        resources.atmosphere_buffer().raw_buffer().create_bind_group_entry(0),
    };

    uint32_t bind_group_offset = 0;
    if (custom_uniforms_bind_groups.empty()) {
        bind_group_entries.push_back(resources.uniforms_buffer().raw_buffer().create_bind_group_entry(1));
        bind_group_offset = 1;
    }

    bind_group_entries.push_back(resources.lut_sampler().create_bind_group_entry(1 + bind_group_offset));
    bind_group_entries.push_back(resources.transmittance_lut().view().create_bind_group_entry(2 + bind_group_offset));
    bind_group_entries.push_back(resources.multi_scattering_lut().view().create_bind_group_entry(3 + bind_group_offset));
    bind_group_entries.push_back(resources.sky_view_lut().view().create_bind_group_entry(4 + bind_group_offset));

    std::vector<std::unique_ptr<webgpu::raii::BindGroup>> bind_groups;
    bind_groups.push_back(std::make_unique<webgpu::raii::BindGroup>(m_device, *m_bind_group_layout, bind_group_entries, "sky view LUT pass"));

    glm::uvec3 dispatch_group_dimensions = glm::uvec3(glm::ceil(glm::vec3 {
        float(resources.sky_view_lut().texture().width()) / 16.0f,
        float(resources.sky_view_lut().texture().height()) / 16.0f,
        1.0f,
    }));
    // TODO for now, just ignore shadow bind group and custom bind groups
    return std::make_unique<util::ComputePass>(m_pipeline->handle(), bind_groups, dispatch_group_dimensions);
}

std::vector<std::pair<std::string, double>> makeMiePhaseOverrides(std::optional<config::MieHgDPhaseConfig> miePhaseConfig)
{
    if (!miePhaseConfig.has_value()) {
        return {};
    } else {
        std::vector<std::pair<std::string, double>> key_value_pairs {};
        key_value_pairs.emplace_back("MIE_USE_HG_DRAINE", double(true));

        if (!(miePhaseConfig.value().useConstantDropletDiameter)) {
            key_value_pairs.emplace_back("MIE_USE_HG_DRAINE_DYNAMIC", double(true));
        } else if (miePhaseConfig.value().constantDropletDiameter) {
            key_value_pairs.emplace_back("HG_DRAINE_DROPLET_DIAMETER", miePhaseConfig.value().constantDropletDiameter);
        }
        return key_value_pairs;
    }
}

AerialPerspectiveLutPipeline::AerialPerspectiveLutPipeline(WGPUDevice device, std::unique_ptr<webgpu::raii::BindGroupLayout> bind_group_layout,
    std::unique_ptr<webgpu::raii::PipelineLayout> pipeline_layout, std::unique_ptr<webgpu::raii::ComputePipeline> pipeline,
    WGPUTextureFormat aerial_perspective_lut_format, float aerial_perspective_slice_count, float aerial_perspective_distance_per_slice,
    glm::uvec2 multi_scattering_lut_size)
    : m_device { device }
    , m_bind_group_layout(std::move(bind_group_layout))
    , m_pipeline_layout(std::move(pipeline_layout))
    , m_pipeline(std::move(pipeline))
    , m_aerial_perspective_lut_format { aerial_perspective_lut_format }
    , m_aerial_perspective_slice_count { aerial_perspective_slice_count }
    , m_aerial_perspective_distance_per_slice { aerial_perspective_distance_per_slice }
    , m_multi_scattering_lut_size { multi_scattering_lut_size }
{
}

std::string AerialPerspectiveLutPipeline::make_shader_code(WGPUTextureFormat aerial_perspective_lut_format, const std::string& shadow_code,
    const std::string& custom_uniforms_code, std::optional<float> const_droplet_diameter)
{
    QString base = QString::fromStdString(ShaderModuleManager::load_and_preprocess_without_cache("atmosphere/common/constants.wgsl"))
                       .append(ShaderModuleManager::load_and_preprocess_without_cache("atmosphere/common/intersection.wgsl"))
                       .append(ShaderModuleManager::load_and_preprocess_without_cache("atmosphere/common/medium.wgsl"))
                       .append(shaders::make_phase_shader_code(const_droplet_diameter))
                       .append(ShaderModuleManager::load_and_preprocess_without_cache("atmosphere/common/uv.wgsl"))
                       .append(ShaderModuleManager::load_and_preprocess_without_cache("atmosphere/common/uniforms.wgsl"));
    if (!custom_uniforms_code.empty()) {
        base.append(custom_uniforms_code).append(ShaderModuleManager::load_and_preprocess_without_cache("atmosphere/common/custom_uniforms.wgsl"));
    }
    base.append(ShaderModuleManager::load_and_preprocess_without_cache("atmosphere/common/coordinate_system.wgsl"))
        .append(ShaderModuleManager::load_and_preprocess_without_cache("atmosphere/common/multiple_scattering.wgsl"))
        .append(ShaderModuleManager::load_and_preprocess_without_cache("atmosphere/common/aerial_perspective.wgsl"))
        .append(ShaderModuleManager::load_and_preprocess_without_cache("atmosphere/common/sample_sagment_t.wgsl"));

    QString shader = QString::fromStdString(ShaderModuleManager::load_and_preprocess_without_cache("atmosphere/render_aerial_perspective_lut.wgsl"));
    // TODO replace aerial perspective lut format string

    if (!custom_uniforms_code.empty()) {
        shader.replace("let config = config_buffer", "let config = get_uniforms()");
        shader.replace("@group(0) @binding(1) var<uniform> config_buffer: Uniforms;", "");
        for (int i = 2; i < 6; ++i) {
            shader.replace(QString("group(0) @binding(%1)").arg(i), QString("group(0) @binding(%1)").arg(i - 1));
        }
    }
    return QString::fromStdString(shaders::make_shadow_shader_code(shadow_code)).append(base).append(shader).toStdString();
}

std::unique_ptr<webgpu::raii::BindGroupLayout> AerialPerspectiveLutPipeline::make_bind_group_layout(
    WGPUDevice device, WGPUTextureFormat aerial_perspective_lut_format, bool use_custom_uniforms_config)
{
    WGPUBindGroupLayoutEntry atmosphere_buffer_entry {};
    atmosphere_buffer_entry.binding = 0;
    atmosphere_buffer_entry.visibility = WGPUShaderStage_Compute;
    atmosphere_buffer_entry.buffer.type = WGPUBufferBindingType_Uniform;
    atmosphere_buffer_entry.buffer.hasDynamicOffset = false;
    atmosphere_buffer_entry.buffer.minBindingSize = 0;

    const uint32_t binding_offset = uint32_t(!use_custom_uniforms_config);

    WGPUBindGroupLayoutEntry sampler_entry {};
    sampler_entry.binding = 1 + binding_offset;
    sampler_entry.visibility = WGPUShaderStage_Compute;
    sampler_entry.sampler.type = WGPUSamplerBindingType_Filtering;

    WGPUBindGroupLayoutEntry transmittance_lut_texture_entry {};
    transmittance_lut_texture_entry.binding = 2 + binding_offset;
    transmittance_lut_texture_entry.visibility = WGPUShaderStage_Compute;
    transmittance_lut_texture_entry.texture.multisampled = false;
    transmittance_lut_texture_entry.texture.sampleType = WGPUTextureSampleType_Float;
    transmittance_lut_texture_entry.texture.viewDimension = WGPUTextureViewDimension_2D;

    WGPUBindGroupLayoutEntry multi_scattering_lut_texture_entry {};
    multi_scattering_lut_texture_entry.binding = 3 + binding_offset;
    multi_scattering_lut_texture_entry.visibility = WGPUShaderStage_Compute;
    multi_scattering_lut_texture_entry.texture.multisampled = false;
    multi_scattering_lut_texture_entry.texture.sampleType = WGPUTextureSampleType_Float;
    multi_scattering_lut_texture_entry.texture.viewDimension = WGPUTextureViewDimension_2D;

    WGPUBindGroupLayoutEntry aerial_perspective_lut_texture_entry {};
    aerial_perspective_lut_texture_entry.binding = 4 + binding_offset;
    aerial_perspective_lut_texture_entry.visibility = WGPUShaderStage_Compute;
    aerial_perspective_lut_texture_entry.storageTexture.viewDimension = WGPUTextureViewDimension_3D;
    aerial_perspective_lut_texture_entry.storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
    aerial_perspective_lut_texture_entry.storageTexture.format = aerial_perspective_lut_format;

    std::vector<WGPUBindGroupLayoutEntry> entries = {};
    entries.push_back(atmosphere_buffer_entry);
    if (!use_custom_uniforms_config) {
        WGPUBindGroupLayoutEntry uniforms_buffer_entry {};
        uniforms_buffer_entry.binding = 1;
        uniforms_buffer_entry.visibility = WGPUShaderStage_Compute;
        uniforms_buffer_entry.buffer.type = WGPUBufferBindingType_Uniform;
        uniforms_buffer_entry.buffer.hasDynamicOffset = false;
        uniforms_buffer_entry.buffer.minBindingSize = 0;
        entries.emplace_back(uniforms_buffer_entry);
    }
    entries.push_back(sampler_entry);
    entries.push_back(transmittance_lut_texture_entry);
    entries.push_back(multi_scattering_lut_texture_entry);
    entries.push_back(aerial_perspective_lut_texture_entry);

    return std::make_unique<webgpu::raii::BindGroupLayout>(device, entries, "sky view LUT layout");
}

std::unique_ptr<webgpu::raii::ComputePipeline> AerialPerspectiveLutPipeline::make_compute_pipeline(WGPUDevice device, WGPUPipelineLayout pipeline_layout,
    WGPUShaderModule shader_module, float aerial_perspective_slice_count, float aerial_perspective_distance_per_slice, glm::uvec2 multi_scattering_lut_size,
    float from_kilometers_scale_factor, bool randomize_sample_offsets, bool use_moon, config::MieHgDPhaseConfig mie_phase_config)
{
    WGPUConstantEntry ap_slice_count_constant {};
    ap_slice_count_constant.key = "AP_SLICE_COUNT";
    ap_slice_count_constant.value = double(aerial_perspective_slice_count);

    WGPUConstantEntry ap_distance_per_slice_constant {};
    ap_distance_per_slice_constant.key = "AP_DISTANCE_PER_SLICE";
    ap_distance_per_slice_constant.value = double(aerial_perspective_distance_per_slice);

    WGPUConstantEntry multi_scattering_lut_res_x_constant {};
    multi_scattering_lut_res_x_constant.key = "MULTI_SCATTERING_LUT_RES_X";
    multi_scattering_lut_res_x_constant.value = double(multi_scattering_lut_size.x);

    WGPUConstantEntry multi_scattering_lut_res_y_constant {};
    multi_scattering_lut_res_y_constant.key = "MULTI_SCATTERING_LUT_RES_Y";
    multi_scattering_lut_res_y_constant.value = double(multi_scattering_lut_size.y);

    WGPUConstantEntry from_km_scale_constant {};
    from_km_scale_constant.key = "FROM_KM_SCALE";
    from_km_scale_constant.value = from_kilometers_scale_factor;

    WGPUConstantEntry randomize_sample_offsets_constant {};
    randomize_sample_offsets_constant.key = "RANDOMIZE_SAMPLE_OFFSET";
    randomize_sample_offsets_constant.value = double(randomize_sample_offsets);

    WGPUConstantEntry use_moon_constant {};
    use_moon_constant.key = "USE_MOON";
    use_moon_constant.value = double(use_moon);

    std::vector<WGPUConstantEntry> constants = {
        ap_slice_count_constant,
        ap_distance_per_slice_constant,
        multi_scattering_lut_res_x_constant,
        multi_scattering_lut_res_y_constant,
        from_km_scale_constant,
        randomize_sample_offsets_constant,
        use_moon_constant,
    };

    const auto mie_phase_overrides = makeMiePhaseOverrides(mie_phase_config);
    for (uint32_t i = 0; i < mie_phase_overrides.size(); i++) {
        constants.push_back(WGPUConstantEntry {
            .nextInChain = nullptr,
            .key = mie_phase_overrides[i].first.c_str(),
            .value = mie_phase_overrides[i].second,
        });
    }

    WGPUComputePipelineDescriptor descriptor {};
    descriptor.label = "aerial perspective LUT pass";
    descriptor.layout = pipeline_layout;
    descriptor.compute = {};
    descriptor.compute.entryPoint = "render_aerial_perspective_lut";
    descriptor.compute.module = shader_module;
    descriptor.compute.constantCount = constants.size();
    descriptor.compute.constants = constants.data();
    return std::make_unique<webgpu::raii::ComputePipeline>(device, descriptor);
}

std::unique_ptr<AerialPerspectiveLutPipeline> AerialPerspectiveLutPipeline::create(WGPUDevice device, WGPUTextureFormat aerial_perspective_lut_format,
    float aerial_perspective_slice_count, float aerial_perspective_distance_per_slice, glm::uvec2 multi_scattering_lut_size, float from_kilometers_scale_factor,
    bool randomize_sample_offsets, bool use_moon, config::ShadowConfig shadow_config, config::CustomUniformsSourceConfig custom_uniforms_config,
    config::MieHgDPhaseConfig mie_phase_config)
{
    // TODO pass custom uniform config
    std::unique_ptr<webgpu::raii::BindGroupLayout> bind_group_layout = make_bind_group_layout(device, aerial_perspective_lut_format);

    std::vector<WGPUBindGroupLayout> layouts { bind_group_layout->handle() };
    for (const auto& shadow_bind_group_layout : shadow_config.bindGroupLayouts) {
        layouts.push_back(shadow_bind_group_layout);
    }
    for (const auto& custom_bind_group_layout : custom_uniforms_config.bindGroupLayouts) {
        layouts.push_back(custom_bind_group_layout);
    }
    auto pipeline_layout = std::make_unique<webgpu::raii::PipelineLayout>(device, layouts, "aerial perspective LUT");

    const std::string shader_code
        = make_shader_code(aerial_perspective_lut_format, shadow_config.wgslCode, custom_uniforms_config.wgslCode, mie_phase_config.constantDropletDiameter);
    auto shader_module = ShaderModuleManager::create_shader_module(device, "aerial perspective LUT shader", shader_code);
    auto pipeline = make_compute_pipeline(device, pipeline_layout->handle(), shader_module->handle(), aerial_perspective_slice_count,
        aerial_perspective_distance_per_slice, multi_scattering_lut_size, from_kilometers_scale_factor, randomize_sample_offsets, use_moon, mie_phase_config);

    return std::make_unique<AerialPerspectiveLutPipeline>(device, std::move(bind_group_layout), std::move(pipeline_layout), std::move(pipeline),
        aerial_perspective_lut_format, aerial_perspective_slice_count, aerial_perspective_distance_per_slice, multi_scattering_lut_size);
}

std::unique_ptr<util::ComputePass> AerialPerspectiveLutPipeline::make_compute_pass(const resources::SkyAtmosphereResources& resources,
    const std::vector<WGPUBindGroup>& shadow_bind_groups, const std::vector<WGPUBindGroup>& custom_uniforms_bind_groups)
{
    if (resources.device() != m_device) {
        qFatal() << "[AerialPerspectiveLutPipeline::makeComputePass]: device mismatch";
    }
    // TODO check
    if (resources.atmosphere_buffer().raw_buffer().size_in_byte() < sizeof(resources::AtmosphereUniform)) {
        qFatal() << "[AerialPerspectiveLutPipeline::makeComputePass]: buffer too small for atmosphere parameters (${resources.atmosphereBuffer.size} < "
                    "${ATMOSPHERE_BUFFER_SIZE})";
    }
    if (resources.has_uniforms_buffer() && resources.uniforms_buffer().raw_buffer().size_in_byte() < sizeof(uniforms::Uniforms)) {
        qFatal()
            << "[AerialPerspectiveLutPipeline::makeComputePass]: buffer too small for config (${resources.atmosphereBuffer.size} < ${ATMOSPHERE_BUFFER_SIZE})";
    }
    if (resources.multi_scattering_lut().texture().width() != m_multi_scattering_lut_size.x
        || resources.multi_scattering_lut().texture().height() != m_multi_scattering_lut_size.y) {
        qFatal() << "[AerialPerspectiveLutPipeline::makeComputePass]: wrong texture size for multiple scattering LUT. expected ["
                 << m_multi_scattering_lut_size.x << ", " << m_multi_scattering_lut_size.y << "], got [" << resources.multi_scattering_lut().texture().width()
                 << ", " << "" << resources.multi_scattering_lut().texture().height() << "]";
    }
    if (resources.aerial_perspective_lut().texture().descriptor().format != m_aerial_perspective_lut_format) {
        qFatal() << "[AerialPerspectiveLutPipeline::makeComputePass]: wrong texture format for aerial perspective LUT. expected "
                    "'${this.aerialPerspectiveLutFormat}', got ${resources.aerialPerspectiveLut.texture.format}";
    }
    if (resources.aerial_perspective_lut().texture().depth_or_num_layers() != m_aerial_perspective_slice_count) {
        qFatal() << "[AerialPerspectiveLutPipeline::makeComputePass]: wrong texture depth for aerial perspective LUT. expected '"
                 << m_aerial_perspective_slice_count << "', got " << resources.aerial_perspective_lut().texture().depth_or_num_layers();
    }

    std::vector<WGPUBindGroupEntry> bind_group_entries {
        resources.atmosphere_buffer().raw_buffer().create_bind_group_entry(0),
    };

    uint32_t bind_group_offset = 0;
    if (custom_uniforms_bind_groups.empty()) {
        bind_group_entries.push_back(resources.uniforms_buffer().raw_buffer().create_bind_group_entry(1));
        bind_group_offset = 1;
    }

    bind_group_entries.push_back(resources.lut_sampler().create_bind_group_entry(1 + bind_group_offset));
    bind_group_entries.push_back(resources.transmittance_lut().view().create_bind_group_entry(2 + bind_group_offset));
    bind_group_entries.push_back(resources.multi_scattering_lut().view().create_bind_group_entry(3 + bind_group_offset));
    bind_group_entries.push_back(resources.aerial_perspective_lut().view().create_bind_group_entry(4 + bind_group_offset));

    std::vector<std::unique_ptr<webgpu::raii::BindGroup>> bind_groups;
    bind_groups.push_back(std::make_unique<webgpu::raii::BindGroup>(m_device, *m_bind_group_layout, bind_group_entries, "aerial perspective LUT pass"));

    glm::uvec3 dispatch_group_dimensions = glm::uvec3(glm::ceil(glm::vec3 {
        float(resources.aerial_perspective_lut().texture().width()) / 16.0f,
        float(resources.aerial_perspective_lut().texture().height()) / 16.0f,
        resources.aerial_perspective_lut().texture().depth_or_num_layers(),
    }));
    // TODO for now, just ignore shadow bind group and custom bind groups
    return std::make_unique<util::ComputePass>(m_pipeline->handle(), bind_groups, dispatch_group_dimensions);
}

float AerialPerspectiveLutPipeline::aerial_perspective_distance_per_slice() const { return m_aerial_perspective_distance_per_slice; }

float AerialPerspectiveLutPipeline::aerial_perspective_inv_distance_per_slice() const { return 1.0f / this->m_aerial_perspective_distance_per_slice; }

SkyAtmospherePipelines::SkyAtmospherePipelines(std::unique_ptr<TransmittanceLutPipeline> transmittance_lut_pipeline,
    std::unique_ptr<MultiScatteringLutPipeline> multi_scattering_lut_pipeline, std::unique_ptr<SkyViewLutPipeline> sky_view_lut_pipeline,
    std::unique_ptr<AerialPerspectiveLutPipeline> aerial_perspective_lut_pipeline)
    : m_transmittance_lut_pipeline(std::move(transmittance_lut_pipeline))
    , m_multi_scattering_lut_pipeline(std::move(multi_scattering_lut_pipeline))
    , m_sky_view_lut_pipeline(std::move(sky_view_lut_pipeline))
    , m_aerial_perspective_lut_pipeline(std::move(aerial_perspective_lut_pipeline))
{
}

std::unique_ptr<SkyAtmospherePipelines> SkyAtmospherePipelines::create(WGPUDevice device, config::SkyAtmosphereRendererConfig config)
{
    auto transmittance_lut_pipeline = atmosphere::pipelines::TransmittanceLutPipeline::create(
        device, config.lookUpTables.transmittanceLut.format, config.lookUpTables.transmittanceLut.sampleCount);
    auto multi_scattering_lut_pipeline = atmosphere::pipelines::MultiScatteringLutPipeline::create(
        device, config.lookUpTables.multiScatteringLut.format, config.lookUpTables.multiScatteringLut.sampleCount);
    auto sky_view_lut_pipeline = atmosphere::pipelines::SkyViewLutPipeline::create(device, config.lookUpTables.skyViewLut.format,
        config.lookUpTables.skyViewLut.size, config.lookUpTables.multiScatteringLut.size, config.skyRenderer.distanceToMaxSampleCount,
        config.fromKilometersScale, config.lights.useMoon, config.lookUpTables.skyViewLut.affectedByShadow ? config.shadow : atmosphere::config::ShadowConfig(),
        config.customUniformsSource, config.mieHgDrainePhase);
    auto aerial_perspective_lut_pipeline = atmosphere::pipelines::AerialPerspectiveLutPipeline::create(device, config.lookUpTables.aerialPerspectiveLut.format,
        config.lookUpTables.aerialPerspectiveLut.size.z, config.lookUpTables.aerialPerspectiveLut.distancePerSlice, config.lookUpTables.multiScatteringLut.size,
        config.fromKilometersScale, config.lookUpTables.aerialPerspectiveLut.randomizeRayOffsets, config.lights.useMoon,
        config.lookUpTables.aerialPerspectiveLut.affectedByShadow ? config.shadow : atmosphere::config::ShadowConfig(), config.customUniformsSource,
        config.mieHgDrainePhase);

    return std::make_unique<SkyAtmospherePipelines>(std::move(transmittance_lut_pipeline), std::move(multi_scattering_lut_pipeline),
        std::move(sky_view_lut_pipeline), std::move(aerial_perspective_lut_pipeline));
}

TransmittanceLutPipeline& SkyAtmospherePipelines::transmittance_lut_pipeline() { return *m_transmittance_lut_pipeline; }

const TransmittanceLutPipeline& SkyAtmospherePipelines::transmittance_lut_pipeline() const { return *m_transmittance_lut_pipeline; }

MultiScatteringLutPipeline& SkyAtmospherePipelines::multi_scattering_lut_pipeline() { return *m_multi_scattering_lut_pipeline; }

const MultiScatteringLutPipeline& SkyAtmospherePipelines::multi_scattering_lut_pipeline() const { return *m_multi_scattering_lut_pipeline; }

SkyViewLutPipeline& SkyAtmospherePipelines::sky_view_lut_pipeline() { return *m_sky_view_lut_pipeline; }

const SkyViewLutPipeline& SkyAtmospherePipelines::sky_view_lut_pipeline() const { return *m_sky_view_lut_pipeline; }

AerialPerspectiveLutPipeline& SkyAtmospherePipelines::aerial_perspective_lut_pipeline() { return *m_aerial_perspective_lut_pipeline; }

const AerialPerspectiveLutPipeline& SkyAtmospherePipelines::aerial_perspective_lut_pipeline() const { return *m_aerial_perspective_lut_pipeline; }

} // namespace webgpu_engine::atmosphere::pipelines
