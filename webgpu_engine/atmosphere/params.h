/*
 * Copyright (c) 2024 Lukas Herzberger
 * SPDX-License-Identifier: MIT
 */

// TODO copyright notice?

#pragma once

#include <glm/vec3.hpp>

namespace webgpu_engine::atmosphere::params {

/**
 * Rayleigh scattering parameters.
 */
struct Rayleigh {
    /**
     * Rayleigh scattering exponential distribution scale in the atmosphere in `km^-1`.
     */
    float densityExpScale;

    /**
     * Rayleigh scattering coefficients in `km^-1`.
     */
    glm::vec3 scattering;
};

/**
 * Mie scattering parameters.
 *
 * The Mie phase function is approximated using the Cornette-Shanks phase function.
 */
struct Mie {
    /**
     * Mie scattering exponential distribution scale in the atmosphere in `km^-1`.
     */
    float densityExpScale;

    /**
     * Mie scattering coefficients in `km^-1`.
     */
    glm::vec3 scattering;

    /**
     * Mie extinction coefficients in `km^-1`.
     */
    glm::vec3 extinction;

    /**
     * Mie phase function parameter.
     *
     * For Cornette-Shanks, this is the excentricity, i.e., the asymmetry paraemter of the phase function in range ]-1, 1[.
     *
     * For Henyey-Greenstein + Draine, this is the droplet diameter in µm. This should be in range ]2, 20[
     * (according to the paper, the lower bound for plausible fog particle sizes is 5 µm). For Henyey-Greenstein + Draine using a constant droplet diameter,
     * this parameter has no effect.
     */
    float phaseParam;
};

struct AbsorptionLayer0 {
    /**
     * The height of the first layer of the absorption component in kilometers.
     */
    float height;

    /**
     * The constant term of the absorption component's first layer.
     *              * This is unitless.
     */
    float constantTerm;

    /**
     * The linear term of the absorption component's first layer in `km^-1`.
     */
    float linearTerm;
};

struct AbsorptionLayer1 {
    /**
     * The constant term of the absorption component's second layer.
     *
     * This is unitless.
     */
    float constantTerm;

    /**
     * The linear term of the absorption component's second layer in `km^-1`.
     */
    float linearTerm;
};

/**
 * A medium type in the atmosphere that only absorbs light with two layers.
 * In Earth's atmosphere this is used to model ozone.
 *
 * Computed as:
 *
 *      extinction * (linearTerm * h + constantTerm),
 *
 * where `h` is the altitude and `linearTerm` and `constantTerm` are the first or second layer's linear and constant terms.
 * If `h` is lower than {@link AbsorptionLayer0.height}, {@link Absorption.layer0} is used, otherwise {@link Absorption.layer1} is used.
 */
struct Absorption {
    /**
     * The lower layer of the absorption component.
     */
    AbsorptionLayer0 layer0;

    /**
     * The upper layer of the absorption component.
     */
    AbsorptionLayer1 layer1;

    /**
     * The extinction coefficients of the absorption component in `km^-1`.
     */
    glm::vec3 extinction;
};

/**
 * Atmosphere parameters.
 *
 * The atmosphere is modelled as a sphere around a spherical planet.
 */
struct Atmosphere {
    /**
     * Center of the atmosphere.
     */
    glm::vec3 center;

    /**
     * Radius of the planet (center to ground) in kilometers.
     */
    float bottomRadius;

    /**
     * Height of atmosphere (distance from {@link bottomRadius} to atmosphere top) in kilometers.
     *
     * Clamped to `max(height, 0)`
     */
    float height;

    /**
     * Rayleigh scattering component.
     */
    Rayleigh rayleigh;

    /**
     * Mie scattering component.
     */
    Mie mie;

    /**
     * Absorption / Ozone component.
     */
    Absorption absorption;

    /**
     * The average albedo of the ground used to model light bounced off the planet's surface.
     */
    glm::vec3 groundAlbedo;

    /**
     * A weight for multiple scattering in the atmosphere.
     */
    float multipleScatteringFactor;
};

/**
 * Create a default atmosphere that corresponds to earth's atmosphere.
 *
 * @param center The center of the atmosphere. Defaults to `upDirection * -{@link Atmosphere.bottomRadius}` (`upDirection` depends on `yUp`).
 * @param yUp If true, the up direction for the default center will be `[0, 1, 0]`, otherwise `[0, 0, 1]` will be used.
 * @param useHenyeyGreenstein If this is true, {@link Mie.phaseParam} will be set to a value suitable for the Cornette-Shanks approximation (`0.8`),
 * otherwise it is set to `3.4` for use with the Henyey-Greenstein + Draine approximation.
 *
 * @returns Atmosphere parameters corresponding to earth's atmosphere.
 */
Atmosphere makeEarthAtmosphere(bool yUp = true, bool useHenyeyGreenstein = true);

} // namespace webgpu_engine::atmosphere::params
