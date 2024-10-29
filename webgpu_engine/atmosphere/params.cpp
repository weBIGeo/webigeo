/*
 * Copyright (c) 2024 Lukas Herzberger
 * SPDX-License-Identifier: MIT
 */

// TODO copyright notice?

#include "params.h"

namespace webgpu_engine::atmosphere::params {

Atmosphere makeEarthAtmosphere(bool yUp, bool useHenyeyGreenstein)
{
    const float rayleighScaleHeight = 8.0f;
    const float mieScaleHeight = 1.2f;
    const float bottomRadius = 6360.0f;

    Rayleigh rayleigh {};
    rayleigh.densityExpScale = -1.0f / rayleighScaleHeight;
    rayleigh.scattering = { 0.005802f, 0.013558f, 0.033100f };

    Mie mie {};
    mie.densityExpScale = -1.0f / mieScaleHeight;
    mie.scattering = { 0.003996f, 0.003996f, 0.003996f };
    mie.extinction = { 0.004440f, 0.004440f, 0.004440f };
    mie.phaseParam = useHenyeyGreenstein ? 0.8f : 3.4f;

    AbsorptionLayer0 absorptionLayer0 {};
    absorptionLayer0.height = 25.0f;
    absorptionLayer0.constantTerm = -2.0f / 3.0f;
    absorptionLayer0.linearTerm = 1.0f / 15.0f;

    AbsorptionLayer1 absorptionLayer1 {};
    absorptionLayer1.constantTerm = 8.0f / 3.0f;
    absorptionLayer1.linearTerm = -1.0f / 15.0f;

    Absorption absorption {};
    absorption.layer0 = absorptionLayer0;
    absorption.layer1 = absorptionLayer1;
    absorption.extinction = { 0.000650f, 0.001881f, 0.000085f };

    Atmosphere atmosphere {};
    atmosphere.center = { 0.0f, yUp ? -bottomRadius : 0.0f, yUp ? 0.0f : -bottomRadius };
    atmosphere.bottomRadius = bottomRadius;
    atmosphere.height = 100.0f;
    atmosphere.rayleigh = rayleigh;
    atmosphere.mie = mie;
    atmosphere.absorption = absorption;
    atmosphere.groundAlbedo = { 0.4f, 0.4f, 0.4f };
    atmosphere.multipleScatteringFactor = 1.0f;

    return atmosphere;
}

} // namespace webgpu_engine::atmosphere::params
