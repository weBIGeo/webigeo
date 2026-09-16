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

#include "params.h"

namespace webgpu_engine::sky::params {

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

} // namespace webgpu_engine::sky::params
