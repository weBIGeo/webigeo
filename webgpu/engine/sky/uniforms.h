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

#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace webgpu_engine::sky::uniforms {

struct Camera {
    /**
     * Inverse projection matrix for the current camera view
     */
    glm::mat4x4 inverseProjection;

    /**
     * Inverse view matrix for the current camera view
     */
    glm::mat4x4 inverseView;

    /**
     * World position of the current camera view
     */
    glm::vec3 position;
};

/**
 * Properties of a directional light influencing the atmosphere (e.g., sun or moon).
 */
struct AtmosphereLight {
    /**
     * Light's illuminance at the top of the atmosphere.
     *
     * Defaults to [1.0, 1.0, 1.0]
     */
    glm::vec3 illuminance = { 1.0f, 1.0f, 1.0f };

    /**
     * Light disk's angular diameter in radians.
     *
     * For the sun, defaults to ~0.0095120444 (0.545 degrees)
     * For the moon, defaults to ~0.0099134702 (0.568 degrees)
     */
    float diskAngularDiameter = 0.0095120444f;

    /**
     * Light's direction (direction to the light source).
     *
     * This is expected to be normalized.
     */
    glm::vec3 direction = { 0.0, 0.0, 1.0 };

    /**
     * Light disk's luminance scale.
     *
     * The light disk's luminance is computed from the given {@link illuminance} and the disk's {@link
     * diskAngularDiameter}. This scale is applied to the computed luminance value to give users more control over the sun disk's appearance.
     *
     * Defaults to 1.0.
     */
    float diskLuminanceScale = 1.0f;
};

struct Uniforms {
    /**
     * The current camera parameter.
     */
    Camera camera;

    /**
     * The current frame id.
     * This is only used if {@link FullResolutionRayMarchConfig.randomizeRayOffsets} or {@link AerialPerspectiveLutConfig.randomizeRayOffsets} is true.
     *
     * Defaults to 0.
     */
    float frameId = 0.0f;

    /**
     * Resolution of the output texture.
     */
    glm::vec2 screenResolution;

    /**
     * Minimum number of ray marching samples per pixel when rendering the sky view lookup table or rendering the sky using per-pixel ray marching.
     *
     * Defaults to 14.
     */
    float rayMarchMinSPP = 14.0f;

    /**
     * Maximum number of ray marching samples per pixel when rendering the sky view lookup table or rendering the sky using per-pixel ray marching.
     *
     * Defaults to 30.
     */
    float rayMarchMaxSPP = 30.0f;

    /**
     * A directional light that influences the atmosphere.
     *
     * Defaults to the default sun.
     */
    AtmosphereLight sun;

    /**
     * A directional lights that influences the atmosphere.
     *
     * Ignored if {@link SkyAtmosphereLutRenderer} is not configured to render the moon.
     */
    AtmosphereLight moon;
};

} // namespace webgpu_engine::sky::uniforms
