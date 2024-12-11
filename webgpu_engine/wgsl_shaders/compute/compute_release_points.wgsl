/*****************************************************************************
 * weBIGeo
 * Copyright (C) 2024 Adam Celarek
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

#include "util/normals_util.wgsl"

// input
@group(0) @binding(0) var<uniform> settings: ReleasePointSettings;
@group(0) @binding(1) var normals_texture: texture_2d<f32>;

// output
//currently, format r8uint cannot be used for storage texture with write access (apparently only 32 bit formats can be used)
//TODO use storage buffer instead for now!
//  ASAP! saves 3 byte per texel (75%) immediately, could optimize further (just 3 bit per texel for current impl)
@group(0) @binding(2) var release_points_texture: texture_storage_2d<rgba8unorm, write>; // ASSERT: same dimensions as heights_texture

struct ReleasePointSettings {
    min_slope_angle: f32, // in rad
    max_slope_angle: f32, // in rad
    sampling_interval: vec2u,
}

fn should_paint(pos: vec2u) -> bool {
    return (pos.x % settings.sampling_interval.x == 0) && (pos.y % settings.sampling_interval.y == 0);
}

@compute @workgroup_size(16, 16, 1)
fn computeMain(@builtin(global_invocation_id) id: vec3<u32>) {
    // id.xy in [0, ceil(texture_dimensions(normals_texture).xy / workgroup_size.xy) - 1]

    // exit if thread id is outside image dimensions (i.e. thread is not supposed to be doing any work)
    let texture_size = textureDimensions(release_points_texture);
    if (id.x >= texture_size.x || id.y >= texture_size.y) {
        return;
    }
    // id.xy in [0, texture_dimensions(output_tiles) - 1]

    let tex_pos = id.xy;
    let normal = textureLoad(normals_texture, tex_pos, 0).xyz * 2 - 1;
    let slope_angle = get_slope_angle(normal); // slope angle in rad (0 flat, pi/2 vertical)

    if (slope_angle < settings.min_slope_angle || slope_angle > settings.max_slope_angle || !should_paint(tex_pos)) {
        textureStore(release_points_texture, tex_pos, vec4f(0, 0, 0, 0));
    } else {
        textureStore(release_points_texture, tex_pos, vec4f(1, 0, 0, 1));
    }
}
