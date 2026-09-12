/*****************************************************************************
 * weBIGeo
 * Copyright (C) 2026 Gerald Kimmersdorfer
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
 * Ported from the sky/common LUT math originally by:
 * Copyright (c) 2024 Lukas Herzberger
 * Copyright (c) 2020 Epic Games, Inc.
 * SPDX-License-Identifier: MIT
 */

///use webgpu_engine::sky/common/constants

fn from_sub_uvs_to_unit(u: f32, resolution: f32) -> f32 {
	return (u - 0.5 / resolution) * (resolution / (resolution - 1.0));
}

fn from_unit_to_sub_uvs(u: f32, resolution: f32) -> f32 {
	return (u + 0.5 / resolution) * (resolution / (resolution + 1.0));
}

fn transmittance_lut_params_to_uv(bottom_radius: f32, top_radius: f32, view_height: f32, cos_view_zenith: f32) -> vec2<f32> {
	let height_sq = view_height * view_height;
	let bottom_radius_sq = bottom_radius * bottom_radius;
	let top_radius_sq = top_radius * top_radius;
	let h = sqrt(max(0.0, top_radius_sq - bottom_radius_sq));
	let rho = sqrt(max(0.0, height_sq - bottom_radius_sq));

	let discriminant = height_sq * (cos_view_zenith * cos_view_zenith - 1.0) + top_radius_sq;
	let distance_to_boundary = max(0.0, (-view_height * cos_view_zenith + sqrt(max(discriminant, 0.0))));

	let min_distance = top_radius - view_height;
	let max_distance = rho + h;
	let x_mu = (distance_to_boundary - min_distance) / (max_distance - min_distance);
	let x_r = rho / h;

	return vec2<f32>(x_mu, x_r);
}

override SKY_VIEW_LUT_RES_X: f32 = 192.0;
override SKY_VIEW_LUT_RES_Y: f32 = 108.0;

fn sky_view_lut_params_to_uv(bottom_radius: f32, intersects_ground: bool, cos_view_zenith: f32, cos_light_view: f32, view_height: f32) -> vec2<f32> {
	let v_horizon = sqrt(max(view_height * view_height - bottom_radius * bottom_radius, 0.0));
	let ground_to_horizon = acos(v_horizon / view_height);
	let zenith_horizon_angle = pi - ground_to_horizon;

	var uv = vec2<f32>();
	if !intersects_ground {
		let coord = 1.0 - sqrt(max(1.0 - acos(cos_view_zenith) / zenith_horizon_angle, 0.0));
		uv.y = coord * 0.5;
	} else {
		let coord = (acos(cos_view_zenith) - zenith_horizon_angle) / ground_to_horizon;
		uv.y = sqrt(max(coord, 0.0)) * 0.5 + 0.5;
	}
	uv.x = sqrt(-cos_light_view * 0.5 + 0.5);

	return vec2<f32>(from_unit_to_sub_uvs(uv.x, SKY_VIEW_LUT_RES_X), from_unit_to_sub_uvs(uv.y, SKY_VIEW_LUT_RES_Y));
}
