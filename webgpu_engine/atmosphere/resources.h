/*
 * Copyright (c) 2024 Lukas Herzberger
 * SPDX-License-Identifier: MIT
 */

// TODO copyright notice?

#pragma once

#include "config.h"
#include "params.h"
#include "uniforms.h"
#include "util.h"
#include "webgpu_engine/Buffer.h"
#include <memory>
#include <webgpu/webgpu.h>

namespace webgpu_engine::atmosphere::resources {

struct AtmosphereUniform {
    // Rayleigh scattering coefficients
    glm::vec3 rayleigh_scattering;
    // Rayleigh scattering exponential distribution scale in the atmosphere
    float rayleigh_density_exp_scale;

    // Mie scattering coefficients
    glm::vec3 mie_scattering;
    // Mie scattering exponential distribution scale in the atmosphere
    float mie_density_exp_scale;
    // Mie extinction coefficients
    glm::vec3 mie_extinction;
    // Mie phase parameter (Cornette-Shanks excentricity or Henyey-Greenstein-Draine droplet diameter)
    float mie_phase_param;
    // Mie absorption coefficients
    glm::vec3 mie_absorption;

    // Another medium type in the atmosphere
    float absorption_density_0_layer_height;
    float absorption_density_0_constant_term;
    float absorption_density_0_linear_term;
    float absorption_density_1_constant_term;
    float absorption_density_1_linear_term;
    // This other medium only absorb light, e.g. useful to represent ozone in the earth atmosphere
    glm::vec3 absorption_extinction;

    // Radius of the planet (center to ground)
    float bottom_radius;

    // The albedo of the ground.
    glm::vec3 ground_albedo;

    // Maximum considered atmosphere height (center to atmosphere top)
    float top_radius;

    // planet center in world space (z up)
    // used to transform the camera's position to the atmosphere's object space
    glm::vec3 planet_center;

    float multi_scattering_factor;
};

class SkyAtmosphereResources {
public:
    SkyAtmosphereResources(WGPUDevice device, const config::SkyAtmosphereRendererConfig& config);

    /**
     * Updates the {@link SkyAtmosphereResources.atmosphereBuffer} using a given {@link Atmosphere}.
     *
     * Overwrites this instance's internal {@link Atmosphere} parameters.
     *
     * @param atmosphere the {@link Atmosphere} to write to the {@link atmosphereBuffer}.
     * @see atmosphereToFloatArray Internally call {@link atmosphereToFloatArray} to convert the {@link Atmosphere} to a `Float32Array`.
     */
    void updateAtmosphere(const params::Atmosphere& atmosphere);

    /**
     * Updates the {@link SkyAtmosphereResources.m_uniformsBuffer} using a given {@link Uniforms}.
     * @param uniforms the {@link Uniforms} to write to the {@link atmosphereBuffer}.
     * @see uniformsToFloatArray Internally call {@link uniformsToFloatArray} to convert the {@link Uniforms} to a `Float32Array`.
     */
    void updateUniforms(uniforms::Uniforms uniforms);

    WGPUDevice device() const;
    const params::Atmosphere& atmosphere() const;

    Buffer<AtmosphereUniform>& atmosphere_buffer();
    const Buffer<AtmosphereUniform>& atmosphere_buffer() const;

    bool has_uniforms_buffer() const;
    Buffer<uniforms::Uniforms>& uniforms_buffer();
    const Buffer<uniforms::Uniforms>& uniforms_buffer() const;

    webgpu::raii::Sampler& lut_sampler();
    const webgpu::raii::Sampler& lut_sampler() const;

    util::LookUpTable& transmittance_lut();
    const util::LookUpTable& transmittance_lut() const;

    util::LookUpTable& multi_scattering_lut();
    const util::LookUpTable& multi_scattering_lut() const;

    util::LookUpTable& sky_view_lut();
    const util::LookUpTable& sky_view_lut() const;

    util::LookUpTable& aerial_perspective_lut();
    const util::LookUpTable& aerial_perspective_lut() const;

private:
    static AtmosphereUniform atmosphereToUniformStruct(const params::Atmosphere& atmosphere);

private:
    /**
     * A name that is propagated to the WebGPU resources.
     */
    std::string m_label;

    /**
     * The WebGPU m_device the resources are allocated from.
     */
    WGPUDevice m_device;

    /**
     * {@link Atmosphere} parameters.
     *
     * Set using {@link updateAtmosphere}.
     *
     * @see {@link updateAtmosphere}
     */
    params::Atmosphere m_atmosphere;

    /**
     * A uniform buffer of size {@link ATMOSPHERE_BUFFER_SIZE} storing the {@link Atmosphere}'s parameters.
     */
    std::unique_ptr<Buffer<AtmosphereUniform>> m_atmosphere_buffer;

    /**
     * A uniform buffer of size {@link UNIFORMS_BUFFER_SIZE} storing parameters set through {@link Uniforms}.
     *
     * If custom uniform buffers are used, this is undefined (see {@link CustomUniformsSourceConfig}).
     */
    std::unique_ptr<Buffer<uniforms::Uniforms>> m_uniforms_buffer;

    /**
     * A linear sampler used to sample the look up tables.
     */
    std::unique_ptr<webgpu::raii::Sampler> m_lut_sampler;

    /**
     * The transmittance look up table.
     * Stores the medium transmittance toward the sun.
     *      * Parameterized by the view / zenith angle in x and the altitude in y.
     */
    std::unique_ptr<util::LookUpTable> m_transmittance_lut;

    /**
     * The multiple scattering look up table.
     * Stores multiple scattering contribution.
     *      * Paramterized by the sun / zenith angle in x (range: [π, 0]) and the altitude in y (range: [0, top], where top is the height of the
     * atmosphere).
     */
    std::unique_ptr<util::LookUpTable> m_multi_scattering_lut;

    /**
     * The sky view look up table.
     * Stores the distant sky around the camera with respect to it's altitude within the atmosphere.
     *      * Parameterized by the longitude in x (range: [0, 2π]) and latitude in y (range: [-π/2, π/2]).
     */
    std::unique_ptr<util::LookUpTable> m_sky_view_lut;

    /**
     * The aerial perspective look up table.
     * Stores the aerial perspective in a volume fit to the view frustum.
     *      * Parameterized by x and y corresponding to the image plane and z being the view depth (range: [0, {@link AerialPerspectiveLutConfig.size}[2] *
     * {@link AerialPerspectiveLutConfig.distancePerSlice}]).
     */
    std::unique_ptr<util::LookUpTable> m_aerial_perspective_lut;
};

} // namespace webgpu_engine::atmosphere::resources
