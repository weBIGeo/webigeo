/*****************************************************************************
 * weBIGeo
 * Copyright (C) 2022 Adam Celarek
 * Copyright (C) 2024 Patrick Komon
 * Copyright (C) 2026 Wendelin Muth
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

const atmosphere_height = 100.0;

const wavelengths = vec3f(700.0, 560.0, 440.0);

fn scattering_coefficients() -> vec3f {
    return pow(400.0 / wavelengths, vec3(4.0, 4.0, 4.0)) * 0.04;
}

fn density_at_height(height_msl: f32) -> f32 {
    return exp(-height_msl * 0.13);
}

fn an_optical_depth(origin_height: f32, h_delta: f32, distance: f32) -> f32 {
    let end_height = origin_height + h_delta * distance;
    const w = 0.13;
    let density_at_orign = density_at_height(origin_height);
    let density_at_end = density_at_height(end_height);
    if abs(h_delta) < 0.001 {
        return distance * 0.5 * (density_at_orign + density_at_end);
    }
    return (1.0 / (w * h_delta)) * (density_at_orign - density_at_end);
}

fn atmospheric_inscatter_at_point(pos_km: vec3f, sun_dir: vec3f) -> vec3f {
    let height = pos_km.z;
    let sun_optical_depth = an_optical_depth(height, 1.0, atmosphere_height - height);
    let sun_transmittance = exp(-sun_optical_depth * scattering_coefficients());
    let air_density = density_at_height(height);
    return air_density * sun_transmittance;
}
