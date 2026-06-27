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

// Earth curvature correction: drops a camera-relative position by d^2 / (2R), such that distant terrain 
// bends below the horizon. Only XY-relative-to-camera matters; z is lowered. Apply this to the clip-space
// position ONLY.
fn apply_earth_curvature(pos_cws: vec3f, planet_radius_m: f32) -> vec3f {
    let d_sq = pos_cws.x * pos_cws.x + pos_cws.y * pos_cws.y;
    return vec3f(pos_cws.xy, pos_cws.z - d_sq / (2.0 * planet_radius_m));
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
