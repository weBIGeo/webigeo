/*****************************************************************************
 * weBIGeo
 * Copyright (C) 2022 Adam Celarek
 * Copyright (C) 2024 Patrick Komon
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

#include "util/camera_config.wgsl"
#include "screen_pass_vert.wgsl"

@group(0) @binding(0) var<uniform> camera : camera_config;

// const highp float infinity = 1.0 / 0.0;   // gives a warning on webassembly (and other angle based products)
const infinity = 3.40282e+38; // https://godbolt.org/z/9o9PdbGqW

fn unproject(normalised_device_coordinates: vec2f) -> vec3f {
    let unprojected = camera.inv_proj_matrix * vec4(normalised_device_coordinates, 1.0, 1.0);
    let normalised_unprojected = unprojected / unprojected.w;
    return normalize((camera.inv_view_matrix * normalised_unprojected).xyz);
}

@fragment
fn fragmentMain(vertex_out : VertexOut) -> @location(0) vec4f {

    let origin = camera.position.xyz;
    let ray_direction = unproject(vertex_out.texcoords * vec2f(2.0, -2.0) + vec2f(-1.0, 1.0));

    let cloud_height = 10000.0;
    let t = (cloud_height - origin.z) / ray_direction.z;
    if (t < 0.0) {
        discard;
    }

    let hit_pos = origin + ray_direction * t;

    let color = vec3f(fract(hit_pos.x / 100000.0), fract(hit_pos.y / 100000.0), 0.0);

    return vec4f(color, 0.9);
}
