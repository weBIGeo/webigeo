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
#include "tile_id.wgsl"

// input
@group(0) @binding(0) var<storage> input_tile_ids: array<TileId>;
@group(0) @binding(1) var<storage> input_tile_bounds: array<vec4<f32>>;
@group(0) @binding(2) var<storage> map_key_buffer: array<vec4<u32>>; // hash map key buffer
@group(0) @binding(3) var<storage> map_value_buffer: array<u32>; // hash map value buffer, contains texture array indices
@group(0) @binding(4) var input_tiles: texture_2d_array<u32>; // height tiles

// output
@group(0) @binding(5) var output_tiles: texture_storage_2d_array<rgba8unorm, write>; // normal tiles (output)

@compute @workgroup_size(1)
fn computeMain(@builtin(global_invocation_id) id: vec3<u32>) {
    const input_n_edge_vertices = 65;
    const n_quads_per_direction: u32 = 65 - 1;

    let tile_id = input_tile_ids[id.x];
    let bounds = input_tile_bounds[id.x];
    let quad_width: f32 = (bounds.z - bounds.x) / f32(n_quads_per_direction);
    let quad_height: f32 = (bounds.w - bounds.y) / f32(n_quads_per_direction);

    // find correct hash for tile id
    let tile_id_vec = vec3<u32>(tile_id.x, tile_id.y, tile_id.zoomlevel);
    var hash = hash_tile_id(tile_id_vec);

    while(any(map_key_buffer[hash].xyz != tile_id_vec) && all(map_key_buffer[hash].xyz != EMPTY_TILE_ID_VEC)) {
        hash++;
    }

    // retrieve array layer by hash
    let texture_array_index = i32(map_value_buffer[hash]);

    for (var i: u32 = 0; i < 256; i++) {
        for (var j: u32 = 0; j < 256; j++) {
            let uv = vec2f(f32(i) / f32(256), f32(j) / f32(256));
            let pos_y = uv.y * f32(quad_height) + bounds.y;
            let altitude_correction_factor = calc_altitude_correction_factor(pos_y);
            let normal = normal_by_finite_difference_method(uv, input_n_edge_vertices, quad_width, quad_height, altitude_correction_factor, texture_array_index, input_tiles);
            //let color = vec3f(f32(i) / 256, f32(j) / 256, f32(id.x) / 256);
            textureStore(output_tiles, vec2(i, j), id.x, vec4f(normal, 1));
        }
    }
}
