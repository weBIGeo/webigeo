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

#include "util/tile_util.wgsl"
#include "util/tile_hashmap.wgsl"

// input
@group(0) @binding(0) var<storage> input_tile_ids: array<TileId>;
@group(0) @binding(1) var<uniform> settings: ReleasePointSettings;
@group(0) @binding(2) var<storage> map_key_buffer: array<TileId>; // hash map key buffer
@group(0) @binding(3) var<storage> map_value_buffer: array<u32>; // hash map value buffer, contains texture array indices
@group(0) @binding(4) var input_normal_tiles: texture_2d_array<f32>; // normal tiles

// output
//currently, format r8uint cannot be used for storage texture with write access (apparently only 32 bit formats can be used)
//TODO use storage buffer instead for now!
//  ASAP! saves 3 byte per texel (75%) immediately, could optimize further (just 3 bit per texel for current impl)
@group(0) @binding(6) var output_tiles: texture_storage_2d_array<rgba8unorm, write>; // release point tiles (output)

struct ReleasePointSettings {
    min_slope_angle: f32, // in rad
    max_slope_angle: f32, // in rad
    padding1: f32,
    padding2: f32,
}

@compute @workgroup_size(1, 16, 16)
fn computeMain(@builtin(global_invocation_id) id: vec3<u32>) {
    // id.x  in [0, num_tiles]
    // id.yz in [0, ceil(texture_dimensions(output_tiles).xy / workgroup_size.yz) - 1]

    // exit if thread id is outside image dimensions (i.e. thread is not supposed to be doing any work)
    let output_texture_size = textureDimensions(output_tiles);
    if (id.y >= output_texture_size.x || id.z >= output_texture_size.y) {
        return;
    }
    // id.yz in [0, texture_dimensions(output_tiles) - 1]

    let tile_id = input_tile_ids[id.x];
    let input_texture_size = textureDimensions(input_height_tiles);

    let col = id.y; // in [0, texture_dimension(output_tiles).x - 1]
    let row = id.z; // in [0, texture_dimension(output_tiles).y - 1]
    let tex_pos = vec2i(i32(col), i32(row));

    var texture_array_index: u32;
    let found = get_texture_array_index(tile_id, &texture_array_index, &map_key_buffer, &map_value_buffer);
    if (!found) {
        //textureStore(output_tiles, tex_pos, id.x, vec4f(0, 0, 1, 1)); // for debugging
        return;
    }

    let height = textureLoad(input_height_tiles, tex_pos, texture_array_index, 0).r;
    let normal = textureLoad(input_normal_tiles, tex_pos, texture_array_index, 0).xyz * 2 - 1;
    let slope_angle = acos(normal.z); // slope angle in rad (0 flat, pi/2 vertical)

    if (slope_angle < settings.min_slope_angle || slope_angle > settings.max_slope_angle) {
        textureStore(output_tiles, tex_pos, id.x, vec4f(0, 0, 0, 0));
    } else {
        textureStore(output_tiles, tex_pos, id.x, vec4f(1, 0, 0, 1));
    }
}
