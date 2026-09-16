/*****************************************************************************
 * weBIGeo
 * Copyright (C) 2026 Gerald Kimmersdorfer
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

// Vertical drop of a point on a sphere of radius R that is tangent to the xy-plane, at squared
// horizontal distance d_sq from the tangent point using the spherical sagitta
// R - sqrt(R^2 - d^2), evaluated as the algebraically equal form d^2 / (R + sqrt(R^2 - d^2)) to avoid
// f32 cancellation of subtracting two near-equal large numbers near the camera
fn earth_curvature_drop(d_sq: f32, planet_radius_m: f32) -> f32 {
    let r = planet_radius_m;
    return d_sq / (r + sqrt(max(r * r - d_sq, 0.0)));
}

// Earth curvature correction: drops a camera-relative position onto a sphere of radius R that is
// tangent to the xy-plane under the camera.
fn apply_earth_curvature(pos_cws: vec3f, planet_radius_m: f32) -> vec3f {
    let d_sq = pos_cws.x * pos_cws.x + pos_cws.y * pos_cws.y;
    return vec3f(pos_cws.xy, pos_cws.z - earth_curvature_drop(d_sq, planet_radius_m));
}

// Reconstructs the camera-relative world-space position for a given pixel from
// a depth buffer value. Assumes the view-projection matrix operates in
// camera-relative world space.
fn camera_relative_pos_from_depth(tci: vec2u, dims: vec2u, depth: f32, inv_view_proj: mat4x4f) -> vec3f {
    let ndc_xy = (vec2f(tci) + 0.5) / vec2f(dims) * 2.0 - vec2f(1.0);
    let ndc = vec4f(ndc_xy.x, -ndc_xy.y, depth, 1.0);
    let world_h = inv_view_proj * ndc;
    return world_h.xyz / world_h.w;
}
