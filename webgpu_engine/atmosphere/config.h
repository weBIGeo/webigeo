/*
 * Copyright (c) 2024 Lukas Herzberger
 * SPDX-License-Identifier: MIT
 */

// TODO copyright notice?

#pragma once

#include "params.h"
#include <cstdint>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <string>
#include <vector>
#include <webgpu/raii/Texture.h>
#include <webgpu/webgpu.h>

namespace webgpu_engine::atmosphere::config {

const glm::uvec2 DEFAULT_TRANSMITTANCE_LUT_SIZE = { 256u, 64u };
const uint32_t DEFAULT_MULTISCATTERING_LUT_SIZE = 32u;
const glm::uvec2 DEFAULT_SKY_VIEW_LUT_SIZE = { 192u, 108u };
const glm::uvec3 DEFAULT_AERIAL_PERSPECTIVE_LUT_SIZE = { 32u, 32u, 32u };

const WGPUTextureFormat TRANSMITTANCE_LUT_FORMAT = WGPUTextureFormat_RGBA16Float;
const WGPUTextureFormat MULTI_SCATTERING_LUT_FORMAT = TRANSMITTANCE_LUT_FORMAT;
const WGPUTextureFormat SKY_VIEW_LUT_FORMAT = TRANSMITTANCE_LUT_FORMAT;
const WGPUTextureFormat AERIAL_PERSPECTIVE_LUT_FORMAT = TRANSMITTANCE_LUT_FORMAT;

const uint32_t ATMOSPHERE_BUFFER_SIZE = 128u;
const uint32_t UNIFORMS_BUFFER_SIZE = 224u;

/**
 * External resources and settings required by a {@link SkyAtmosphereLutRenderer}.
 */
struct SkyRendererConfigBase {
    /**
     * If this is true, {@link SkyAtmosphereRasterRenderer.renderSky} / {@link SkyAtmosphereComputeRenderer.renderLutsAndSky} will default to full-screen
     * ray marching to render the atmosphere.
     *
     * Defaults to false.
     */
    bool defaultToPerPixelRayMarch = false;

    /**
     * Distance in kilometers at which the maximum number of sampler per ray is used when ray marching the sky (either when rendering the sky view lookup
     * table or when ray marching the sky per pixel).
     *
     * Defaults to 100 km.
     */
    float distanceToMaxSampleCount = 100.0f;
};

/**
 * The depth buffer to limit the ray marching distance when rendering the sky / atmosphere.
 */
struct DepthBufferConfig {
    /**
     * The depth buffer texture.
     */
    const webgpu::raii::Texture* texture;

    /**
     * A texture view to use for the depth buffer.
     * If {@link texture} has a depth-stencil format, this view must be a "depth-only" view (to support binding it as a `texture_2d<f32>`).
     *
     * If this is not present, a new view is created from the given {@link texture}.
     */
    const webgpu::raii::TextureView* view;

    /**
     * Specifiy if the depth buffer range is [0, 1] (reverse z) or [1, 0] (default).
     * Defaults to false.
     */
    bool reverseZ = false;
};

/**
 * The back buffer texture to use as back ground when rendering the sky / atmosphere using a GPUComputePipeline.
 */
struct ComputeBackBufferConfig {
    /**
     * The back buffer texture.
     */
    const webgpu::raii::Texture* texture;

    /**
     * A texture view to use for the back buffer.
     *
     * If this is not present, a new view is created from the given {@link texture}.
     */
    const webgpu::raii::TextureView* view;
};

/**
 * The render target to render into when using a GPUComputePipeline to render the sky / atmosphere.
 */
struct ComputeRenderTargetConfig {
    /**
     * Must support the `STORAGE_BINDING` usage.
     * Its format must support `"write-only"` access.
     * Its format should have at least 16 bit precision per channel.
     *
     * Must not be the same texture as the back or depth buffer.
     */
    const webgpu::raii::Texture* texture;

    /**
     * A texture view to use for the render target.
     * If this is not present, a new view is created from the given {@link texture}.
     */
    const webgpu::raii::TextureView* view;
};

struct FullResolutionRayMarchConfig {
    /**
     * If this is false, the sky view lookup table is used for pixels with an invalid depth value.
     *
     * While this is cheaper than a full-resolution ray march, volumetric shadows will not be rendered for distant sky pixels.
     *
     * Defaults to false.
     */
    bool rayMarchDistantSky = false;

    /**
     * Results in less sampling artefacts (e.g., smoother volumetric shadows) but introduces visible noise.
     * It is recommended to use temporal anti-aliasing to get rid of this noise.
     *
     * Defaults to true.
     */
    bool randomizeRayOffsets = true;

    /**
     * If this this true, colored transmittance will be used to blend the rendered sky and the texture data in the back buffer when using the full-screen
     * ray marching pass to render the sky.
     *
     * For a {@link SkyAtmosphereRasterRenderer}, this requires the "dual-source-blending" feature to be enabled. Otherwise, this flag has no
     * effect. Without the "dual-source-blending" feature enabled, colored transmissions can only be rendered using a {@link SkyAtmosphereComputeRenderer}
     * or by writing luminance and transmittance to extra targets and blending them in an extra pass (see {@link SkyRendererRasterConfig.transmissionFormat},
     * the blending step is then left to the user).
     *
     * Defaults to true.
     */
    bool useColoredTransmittance = true;
};

struct SkyRendererComputeConfig : public SkyRendererConfigBase {
    /**
     * The depth buffer to limit the ray marching distance when rendering the sky / atmosphere.
     */
    DepthBufferConfig depthBuffer;

    /**
     * The back buffer texture to use as back ground for rendering the sky / atmosphere.
     */
    ComputeBackBufferConfig backBuffer;

    /**
     * The render target to render into.
     * The result will be blended with the texture data in the {@link backBuffer}.
     */
    ComputeRenderTargetConfig renderTarget;

    /**
     * Settings for the full-resolution ray marching pass.
     */
    FullResolutionRayMarchConfig rayMarch;
};

struct AtmosphereLightsConfig {
    /**
     * Render a sun disk.
     *
     * Defaults to true.
     */
    bool renderSunDisk = true;

    /**
     * Use the second atmosphere light source specified in {@link Uniforms.moon}.
     *
     * Defaults to false.
     */
    bool useMoon = false;

    /**
     * Render a moon disk.
     *
     * Defaults to {@link useMoon}.
     */
    bool renderMoonDisk = useMoon;

    /**
     * If this is true, limb darkening is applied to the disk rendered for the first atmosphere light.
     *
     * Defaults to true.
     */
    bool applyLimbDarkeningOnSun = true;

    /**
     * If this is true, limb darkening is applied to the disk rendered for the second atmosphere light.
     *
     * Defaults to false.
     */
    bool applyLimbDarkeningOnMoon = false;
};

/**
 * Config for external resources required for the aerial perspective lookup table to take shadowing into account and for render volumetric shadows when
 * rendering the sky / atmosphere using full-screen ray marching.
 *
 * To integrate user-controlled shadow maps into the sky / atmosphere rendering passes, WGSL code needs to be injected into the shader code and the
 * layouts of the respective sky rendering pipelines need to be created using external bind group layouts.
 */
struct ShadowConfig {
    /**
     * A list of bind group layouts specifying all resources required to respect user-controlled shadow map(s) when rendering the aerial perspective lookup
     * table or when doing full-screen ray marching.
     *
     * This should not contain more than `maxBindGroups - 1` bind group layouts, where `maxBindGroups` is the maximum number of bind group layouts per
     * pipeline layout supported by the device.
     */
    std::vector<WGPUBindGroupLayout> bindGroupLayouts;

    /**
     * A list of bind groups generated using the {@link bindGroupLayouts}, containing all resources required by the user-controlled shadow mapping
     * implementation.
     */
    std::vector<WGPUBindGroup> bindGroups;

    /**
     * The shader code to inject into the aerial perspective & full-screen ray marching pipelines.
     *
     * This needs to provide at least a function with the following signature:
     *
     *      fn get_shadow(world_space_position: vec3<f32>, light_index: u32) -> f32
     *
     * The function should return a floating point value in the range [0, 1], where 1 implies that the world space position
     * given (`world_space_position`) is not in shadow. The `light_index` parameter refers to the index of the atmosphere light, where `0` refers to {@link
     * Uniforms.sun} and `1` refers to {@link Uniforms.moon}.
     *
     * It should also include the bind groups matching the given {@link bindGroupLayouts}.
     * The bind groups must not use bind group index 0.
     */
    std::string wgslCode;
};

struct CustomUniformsSourceConfig {
    /**
     * A list of bind group layouts specifying all user-controlled resources containing the individual parts of the uniform values required by a {@link
     * SkyAtmosphereLutRenderer}.
     *
     * This should not contain more than `maxBindGroups - 1` bind group layouts, where `maxBindGroups` is the maximum number of bind group layouts per
     * pipeline layout supported by the device.
     */
    std::vector<WGPUBindGroupLayout> bindGroupLayouts;

    /**
     * A list of bind groups generated using the {@link bindGroupLayouts}, containing all user-controlled resources containing the individual parts of
     * the uniform values required by a {@link SkyAtmosphereLutRenderer}.
     */
    std::vector<WGPUBindGroup> bindGroups;

    /**
     * The shader code to inject into internal pipelines.
     *
     * This needs to provide at least the following interface:
     *
     *      fn get_inverse_projection() -> mat4x4<f32>
     *
     *      fn get_inverse_view() -> mat4x4<f32>
     *
     *      fn get_camera_world_position() -> vec3<f32>
     *
     *      fn get_frame_id() -> f32
     *
     *      fn get_screen_resolution() -> vec2<f32>
     *
     *      fn get_ray_march_min_spp() -> f32
     *
     *      fn get_ray_march_max_spp() -> f32
     *
     *      fn get_sun_illuminance() -> vec3<f32>
     *
     *      fn get_sun_direction() -> vec3<f32>
     *
     *      fn get_sun_disk_diameter() -> f32
     *
     *      fn get_sun_disk_luminance_scale() -> f32
     *
     *      fn get_moon_illuminance() -> vec3<f32>
     *
     *      fn get_moon_direction() -> vec3<f32>
     *
     *      fn get_moon_disk_diameter() -> f32
     *
     *      fn get_moon_disk_luminance_scale() -> f32
     *
     * For more details on the individual parameters, refer to the documentation on {@link Uniforms}.
     *
     * The WGSL code should also include the bind groups matching the given {@link bindGroupLayouts}.
     * The bind groups must not use bind group index 0.
     *
     * If shadows are used (see {@link ShadowConfig}), the bind group layouts required to render shadows will be injected before
     * the custom unifom buffer bind group layouts. I.e., the bind group indices should start with `1 + shadowConfig.bindGroupLayouts.length`.
     */
    std::string wgslCode;
};

struct TransmittanceLutConfig {
    /**
     * The size of the transmittance lookup table.
     *
     * Defaults to [256, 64]
     */
    glm::uvec2 size = DEFAULT_TRANSMITTANCE_LUT_SIZE;

    /**
     * The format of the transmittance lookup table.
     *
     * Must support `GPUTextureUsage.STORAGE_BINDING` with `"write-only"` access.
     * Must support `GPUTextureSampleType` `"float"`.
     * Should be at least a three component format.
     *
     * Defaults to: `"rgba16float"`
     */
    WGPUTextureFormat format = TRANSMITTANCE_LUT_FORMAT;

    /**
     * The ray marching sample count to use when rendering the transmittance lookup table.
     *
     * Clamped to `max(40, sampleCount)`
     *
     * Defaults to 40
     */
    uint32_t sampleCount = 40u;
};

struct MultiScatteringLutConfig {
    /**
     * The size of the multiple scattering lookup table.
     *
     * Defaults to [32, 32]
     */
    glm::uvec2 size = { DEFAULT_MULTISCATTERING_LUT_SIZE, DEFAULT_MULTISCATTERING_LUT_SIZE };

    /**
     * The format of the multiple scattering lookup table.
     *
     * Must support `GPUTextureUsage.STORAGE_BINDING` with `"write-only"` access.
     * Must support `GPUTextureSampleType` `"float"`.
     * Should be at least a three component format.
     *
     * Defaults to: `"rgba16float"`
     */
    WGPUTextureFormat format = MULTI_SCATTERING_LUT_FORMAT;

    /**
     * The ray marching sample count to use when rendering the multiple scattering lookup table.
     *
     * Clamped to `max(10, sampleCount)`
     *
     * Defaults to 20
     */
    uint32_t sampleCount = 20u;
};

struct SkyViewLutConfig {
    /**
     * The size of the sky view lookup table.
     *
     * Defaults to [192, 108]
     */
    glm::uvec2 size = DEFAULT_SKY_VIEW_LUT_SIZE;

    /**
     * The format of the sky view lookup table.
     *
     * Must support `GPUTextureUsage.STORAGE_BINDING` with `"write-only"` access.
     * Must support `GPUTextureSampleType` `"float"`.
     * Should be at least a three component format.
     *
     * Defaults to: `"rgba16float"`
     */
    WGPUTextureFormat format = SKY_VIEW_LUT_FORMAT;

    /**
     * If this is true and {@link SkyAtmosphereRendererConfig.shadow} is defined, user-controlled shadow mapping will be evaluated for
     * every sample when rendering the sky view lookup table.
     *
     * Defaults to true.
     */
    bool affectedByShadow = true;
};

struct AerialPerspectiveLutConfig {
    /**
     * The size of the aerial perspective lookup table.
     *          * Defaults to [32, 32, 32]
     */
    glm::uvec3 size = DEFAULT_AERIAL_PERSPECTIVE_LUT_SIZE;

    /**
     * The format of the aerial perspective lookup table.
     *
     * Must support `GPUTextureUsage.STORAGE_BINDING` with `"write-only"` access.
     * Must support `GPUTextureSampleType` `"float"`.
     * Should be at least a three component format.
     *
     * Defaults to: `"rgba16float"`
     */
    WGPUTextureFormat format = AERIAL_PERSPECTIVE_LUT_FORMAT;

    /**
     * The distance each slice of the areal perspective lookup table covers in kilometers.
     *
     * Defaults to 4 km.
     */
    float distancePerSlice = 4.0f;

    /**
     * If this is true and {@link SkyAtmosphereRendererConfig.shadow} is defined, user-controlled shadow mapping will be evaluated for every sample
     * when rendering the aerial perspective lookup table.
     *
     * Defaults to true.
     */
    bool affectedByShadow = true;

    /**
     * Might results in smoother volumetric shadows but introduces visible noise.
     *
     * Defaults to false.
     */
    bool randomizeRayOffsets = false;
};

/**
 * Config for internally used lookup tables.
 */
struct SkyAtmosphereLutConfig {
    /**
     * Settings for the transmittance lookup table.
     */
    TransmittanceLutConfig transmittanceLut;

    /**
     * Settings for the multiple scattering lookup table.
     */
    MultiScatteringLutConfig multiScatteringLut;

    /**
     * Settings for the sky view lookup table.
     */
    SkyViewLutConfig skyViewLut;

    /**
     * Settings for the aerial perspective lookup table.
     */
    AerialPerspectiveLutConfig aerialPerspectiveLut;
};

/**
 * Parameters for the Henyey-Greenstein + Draine ({@link https://research.nvidia.com/labs/rtr/approximate-mie/}) approximation of the Mie phase function
 */
struct MieHgDPhaseConfig {
    /**
     * If this is true, a constant droplet diameter will be used throughout the atmosphere renderer's lifetime.
     * Otherwise, the diameter is set dynamically each frame using {@link Mie.phaseParam}.
     *
     * Defaults to true.
     */
    bool useConstantDropletDiameter = true;

    /**
     * The constant water droplet diameter for the Henyey-Greenstein + Draine phase function.
     *
     * If {@link useConstantDropletDiameter} is false, this parameter is ignored.
     *
     * Defaults to 3.4;
     */
    float constantDropletDiameter = 3.4f;
};

struct SkyAtmosphereRendererConfig {
    /**
     * A name used to lable internal resources and pipelines.
     *
     * Defaults to `"atmosphere"`
     */
    std::string label = "atmosphere";

    /**
     * If true, all lookup tables that only depend on constant atmosphere parameters are rendered at creation time.
     *
     * Defaults to true.
     */
    bool initializeConstantLuts = true;

    /**
     * The scale factor to scale points in the scale 1 = 1km to a different scale, e.g., for 1 = 1m set this to 1000.
     *
     * The distance scale used by {@link SkyAtmosphereLutRenderer}s is 1 = 1km. This is used to correctly render the sky / atmosphere in
     * scenes with a different scale.
     *
     * Defaults to 1.0.
     */
    float fromKilometersScale = 1.0f;

    /**
     * The atmosphere parameters for this {@link SkyAtmosphereLutRenderer}.
     * Defaults to: {@link makeEarthAtmosphere} with the scale parameter set to {@link SkyAtmosphereRendererConfig.fromKilometersScale}.
     * @see {@link makeEarthAtmosphere}
     */
    params::Atmosphere atmosphere = params::makeEarthAtmosphere();

    /**
     * Config for the sky rendering post process.
     */
    SkyRendererComputeConfig skyRenderer;

    /**
     * Config for atmosphere lights (sun, moon, sun disk).
     */
    AtmosphereLightsConfig lights;

    /**
     * Config for external resources required by a {@link SkyAtmosphereLutRenderer} to integrate user-controlled shadow maps.
     */
    ShadowConfig shadow;

    bool useCustomUniformSources = false;

    /**
     * Config for externally controlled buffers containing the parameters otherwise controlled by an internal buffer storing {@link Uniforms}.
     *
     * If this is set, no internal buffer for storing {@link Uniforms} will be created or updated.
     */
    CustomUniformsSourceConfig customUniformsSource;

    /**
     * Config for internally used lookup tables.
     */
    SkyAtmosphereLutConfig lookUpTables;

    /**
     * Config for the Henyey-Greenstein + Draine ({@link https://research.nvidia.com/labs/rtr/approximate-mie/}) approximation of the Mie phase
     * function.
     *
     * If this is set, the renderer uses the Henyey-Greenstein + Draine approximation instead of the Cornette-Shanks approximation.
     *
     * Defaults to undefined.
     */
    MieHgDPhaseConfig mieHgDrainePhase;
};

} // namespace webgpu_engine::atmosphere::config
