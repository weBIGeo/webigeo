/*
 * Copyright (c) 2024 Lukas Herzberger
 * SPDX-License-Identifier: MIT
 */

// TODO copyright notice?

#include "resources.h"

namespace webgpu_engine::atmosphere::resources {

SkyAtmosphereResources::SkyAtmosphereResources(WGPUDevice device, const config::SkyAtmosphereRendererConfig& config)
    : m_label { config.label }
    , m_device { device }
    , m_lut_sampler { util::makeLutSampler(device) }
{
    // TODO move to member initializer list

    m_atmosphere_buffer = std::make_unique<Buffer<AtmosphereUniform>>(device, WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst);
    /*this.atmosphereBuffer = device.createBuffer({
        label: `atmosphere buffer [${this.label}]`,
        size: ATMOSPHERE_BUFFER_SIZE,
        usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
    });*/
    updateAtmosphere(config.atmosphere);

    if (config.useCustomUniformSources) {
        // this.uniformsBuffer = undefined;
    } else {
        m_uniforms_buffer = std::make_unique<Buffer<uniforms::Uniforms>>(device, WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst);
        /*this.uniformsBuffer = device.createBuffer({
            label: `config buffer [${this.label}]`,
            size: UNIFORMS_BUFFER_SIZE,
            usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
        });*/
    }

    WGPUTextureDescriptor transmittanceLutDescriptor {};
    transmittanceLutDescriptor.label = "transmittance LUT";
    transmittanceLutDescriptor.size = WGPUExtent3D { config.lookUpTables.transmittanceLut.size.x, config.lookUpTables.transmittanceLut.size.y, 1 };
    transmittanceLutDescriptor.format = config.lookUpTables.transmittanceLut.format;
    transmittanceLutDescriptor.dimension = WGPUTextureDimension_2D;
    transmittanceLutDescriptor.mipLevelCount = 1;
    transmittanceLutDescriptor.sampleCount = 1;
    transmittanceLutDescriptor.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_StorageBinding;
    transmittanceLutDescriptor.viewFormatCount = 1;
    transmittanceLutDescriptor.viewFormats = &config.lookUpTables.transmittanceLut.format;
    m_transmittance_lut = std::make_unique<util::LookUpTable>(std::make_unique<webgpu::raii::Texture>(device, transmittanceLutDescriptor));
    /*this.transmittanceLut = new LookUpTable(device.createTexture({
        label: `transmittance LUT [${this.label}]`,
        size: config.lookUpTables?.transmittanceLut?.size ?? DEFAULT_TRANSMITTANCE_LUT_SIZE,
        format: config.lookUpTables?.transmittanceLut?.format ?? TRANSMITTANCE_LUT_FORMAT,
        usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.STORAGE_BINDING,
    }));*/

    WGPUTextureDescriptor multiScatteringLutDescriptor {};
    multiScatteringLutDescriptor.label = "multi scattering LUT";
    multiScatteringLutDescriptor.size = WGPUExtent3D { config.lookUpTables.multiScatteringLut.size.x, config.lookUpTables.multiScatteringLut.size.y, 1 };
    multiScatteringLutDescriptor.format = config.lookUpTables.multiScatteringLut.format;
    multiScatteringLutDescriptor.dimension = WGPUTextureDimension_2D;
    multiScatteringLutDescriptor.mipLevelCount = 1;
    multiScatteringLutDescriptor.sampleCount = 1;
    multiScatteringLutDescriptor.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_StorageBinding;
    multiScatteringLutDescriptor.viewFormatCount = 1;
    multiScatteringLutDescriptor.viewFormats = &config.lookUpTables.multiScatteringLut.format;
    m_multi_scattering_lut = std::make_unique<util::LookUpTable>(std::make_unique<webgpu::raii::Texture>(device, multiScatteringLutDescriptor));
    /*this.multiScatteringLut = new LookUpTable(device.createTexture({
        label: `multi scattering LUT [${this.label}]`,
        size: config.lookUpTables?.multiScatteringLut?.size ?? [DEFAULT_MULTISCATTERING_LUT_SIZE, DEFAULT_MULTISCATTERING_LUT_SIZE],
        format: config.lookUpTables?.multiScatteringLut?.format ?? MULTI_SCATTERING_LUT_FORMAT,
        usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.STORAGE_BINDING,
    }));*/

    WGPUTextureDescriptor skyViewLutDescriptor {};
    skyViewLutDescriptor.label = "sky view LUT";
    skyViewLutDescriptor.size = WGPUExtent3D { config.lookUpTables.skyViewLut.size.x, config.lookUpTables.skyViewLut.size.y, 1 };
    skyViewLutDescriptor.format = config.lookUpTables.skyViewLut.format;
    skyViewLutDescriptor.dimension = WGPUTextureDimension_2D;
    skyViewLutDescriptor.mipLevelCount = 1;
    skyViewLutDescriptor.sampleCount = 1;
    skyViewLutDescriptor.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_StorageBinding;
    skyViewLutDescriptor.viewFormatCount = 1;
    skyViewLutDescriptor.viewFormats = &config.lookUpTables.skyViewLut.format;
    m_sky_view_lut = std::make_unique<util::LookUpTable>(std::make_unique<webgpu::raii::Texture>(device, skyViewLutDescriptor));
    /*this.skyViewLut = new LookUpTable(device.createTexture({
        label: `sky view LUT [${this.label}]`,
        size: config.lookUpTables?.skyViewLut?.size ?? DEFAULT_SKY_VIEW_LUT_SIZE,
        format: config.lookUpTables?.skyViewLut?.format ?? SKY_VIEW_LUT_FORMAT,
        usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.STORAGE_BINDING,
    }));*/

    WGPUTextureDescriptor aerialPerspectiveLutDescriptor {};
    aerialPerspectiveLutDescriptor.label = "aerial perspective LUT";
    aerialPerspectiveLutDescriptor.size = WGPUExtent3D { config.lookUpTables.aerialPerspectiveLut.size.x, config.lookUpTables.aerialPerspectiveLut.size.y,
        config.lookUpTables.aerialPerspectiveLut.size.z };
    aerialPerspectiveLutDescriptor.format = config.lookUpTables.aerialPerspectiveLut.format;
    aerialPerspectiveLutDescriptor.dimension = WGPUTextureDimension_3D;
    aerialPerspectiveLutDescriptor.mipLevelCount = 1;
    aerialPerspectiveLutDescriptor.sampleCount = 1;
    aerialPerspectiveLutDescriptor.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_StorageBinding;
    aerialPerspectiveLutDescriptor.viewFormatCount = 1;
    aerialPerspectiveLutDescriptor.viewFormats = &config.lookUpTables.aerialPerspectiveLut.format;
    m_aerial_perspective_lut = std::make_unique<util::LookUpTable>(std::make_unique<webgpu::raii::Texture>(device, aerialPerspectiveLutDescriptor));
    /*this.aerialPerspectiveLut = new LookUpTable(device.createTexture({
        label: `aerial perspective LUT [${this.label}]`,
        size: config.lookUpTables?.aerialPerspectiveLut?.size ?? DEFAULT_AERIAL_PERSPECTIVE_LUT_SIZE,
        format: config.lookUpTables?.aerialPerspectiveLut?.format ?? AERIAL_PERSPECTIVE_LUT_FORMAT,
        dimension: '3d',
        usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.STORAGE_BINDING,
    }));*/
}

void SkyAtmosphereResources::updateAtmosphere(const params::Atmosphere& atmosphere)
{
    m_atmosphere = atmosphere;

    m_atmosphere_buffer->data = atmosphereToUniformStruct(atmosphere);
    m_atmosphere_buffer->update_gpu_data(wgpuDeviceGetQueue(m_device));
}

void SkyAtmosphereResources::updateUniforms(uniforms::Uniforms uniforms)
{
    if (m_uniforms_buffer) {
        // TODO check layout
        m_uniforms_buffer->data = uniforms;
        m_uniforms_buffer->update_gpu_data(wgpuDeviceGetQueue(m_device));
    }
}

WGPUDevice SkyAtmosphereResources::device() const { return m_device; }

const params::Atmosphere& SkyAtmosphereResources::atmosphere() const { return m_atmosphere; }

Buffer<AtmosphereUniform>& SkyAtmosphereResources::atmosphere_buffer() { return *m_atmosphere_buffer; }
const Buffer<AtmosphereUniform>& SkyAtmosphereResources::atmosphere_buffer() const { return *m_atmosphere_buffer; }

bool SkyAtmosphereResources::has_uniforms_buffer() const { return bool(m_uniforms_buffer); }
Buffer<uniforms::Uniforms>& SkyAtmosphereResources::uniforms_buffer() { return *m_uniforms_buffer; }
const Buffer<uniforms::Uniforms>& SkyAtmosphereResources::uniforms_buffer() const { return *m_uniforms_buffer; }

webgpu::raii::Sampler& SkyAtmosphereResources::lut_sampler() { return *m_lut_sampler; }
const webgpu::raii::Sampler& SkyAtmosphereResources::lut_sampler() const { return *m_lut_sampler; }

util::LookUpTable& SkyAtmosphereResources::transmittance_lut() { return *m_transmittance_lut; }
const util::LookUpTable& SkyAtmosphereResources::transmittance_lut() const { return *m_transmittance_lut; }

util::LookUpTable& SkyAtmosphereResources::multi_scattering_lut() { return *m_multi_scattering_lut; }
const util::LookUpTable& SkyAtmosphereResources::multi_scattering_lut() const { return *m_multi_scattering_lut; }

util::LookUpTable& SkyAtmosphereResources::sky_view_lut() { return *m_sky_view_lut; }
const util::LookUpTable& SkyAtmosphereResources::sky_view_lut() const { return *m_sky_view_lut; }

util::LookUpTable& SkyAtmosphereResources::aerial_perspective_lut() { return *m_aerial_perspective_lut; }
const util::LookUpTable& SkyAtmosphereResources::aerial_perspective_lut() const { return *m_aerial_perspective_lut; }

AtmosphereUniform SkyAtmosphereResources::atmosphereToUniformStruct(const params::Atmosphere& atmosphere)
{
    return AtmosphereUniform {
        atmosphere.rayleigh.scattering,
        atmosphere.rayleigh.densityExpScale,
        atmosphere.mie.scattering,
        atmosphere.mie.densityExpScale,
        atmosphere.mie.extinction,
        atmosphere.mie.phaseParam,
        glm::max(atmosphere.mie.extinction - atmosphere.mie.scattering, glm::vec3(0.0f)),
        atmosphere.absorption.layer0.height,
        atmosphere.absorption.layer0.constantTerm,
        atmosphere.absorption.layer0.linearTerm,
        atmosphere.absorption.layer1.constantTerm,
        atmosphere.absorption.layer1.linearTerm,
        atmosphere.absorption.extinction,
        atmosphere.bottomRadius,
        atmosphere.groundAlbedo,
        atmosphere.bottomRadius + std::max(atmosphere.height, 0.0f),
        atmosphere.center,
        atmosphere.multipleScatteringFactor,
    };
}

} // namespace webgpu_engine::atmosphere::resources
