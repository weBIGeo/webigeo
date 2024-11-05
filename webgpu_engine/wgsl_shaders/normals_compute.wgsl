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

#include "tile_util.wgsl"
#include "tile_hashmap.wgsl"
#include "normals_util.wgsl"

// input
@group(0) @binding(0) var<storage> input_tile_ids: array<TileId>;
@group(0) @binding(1) var<storage> input_tile_bounds: array<vec4<f32>>;
@group(0) @binding(2) var<storage> map_key_buffer: array<TileId>; // hash map key buffer
@group(0) @binding(3) var<storage> map_value_buffer: array<u32>; // hash map value buffer, contains texture array indices
@group(0) @binding(4) var input_tiles: texture_2d_array<u32>; // height tiles
@group(0) @binding(5) var input_tiles_sampler: sampler; // height tiles sampler

// output
@group(0) @binding(6) var output_tiles: texture_storage_2d_array<rgba8unorm, write>; // normal tiles (output)

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
    let bounds = input_tile_bounds[id.x];
    let input_texture_size = textureDimensions(input_tiles);
    let quad_width: f32 = (bounds.z - bounds.x) / f32(input_texture_size.x - 1);
    let quad_height: f32 = (bounds.w - bounds.y) / f32(input_texture_size.y - 1);

    let col = id.y; // in [0, texture_dimension(output_tiles).x - 1]
    let row = id.z; // in [0, texture_dimension(output_tiles).y - 1]
    let uv = vec2f(f32(col), f32(row)) / vec2f(output_texture_size - 1) + 1f / (2f * vec2f(output_texture_size - 1));
    let pos_y = uv.y * f32(quad_height) + bounds.y;
    let altitude_correction_factor = calc_altitude_correction_factor(pos_y);
    let normal = normal_by_finite_difference_method_with_neighbors(uv, quad_width, quad_height,
        altitude_correction_factor, tile_id, &map_key_buffer, &map_value_buffer, input_tiles, input_tiles_sampler);
    textureStore(output_tiles, vec2(col, row), id.x, vec4f(0.5 * normal + 0.5, 1));
}
