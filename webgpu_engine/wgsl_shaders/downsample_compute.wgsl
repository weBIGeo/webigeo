/*****************************************************************************
 * weBIGeo
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
@group(0) @binding(1) var<storage> map_key_buffer: array<TileId>; // hash map key buffer
@group(0) @binding(2) var<storage> map_value_buffer: array<u32>; // hash map value buffer, contains texture array indices
@group(0) @binding(3) var input_textures: texture_2d_array<f32>; // textures per input tile

// output
@group(0) @binding(4) var output_textures: texture_storage_2d_array<rgba8unorm, write>; // textures per downsampled tile

// NOTE: read_write binding for storage texture of type rgba8unorm currently IS NOT supported by webGPU.
// There is ongoing discussion, but for now, we write to a different texture and copy afterwards as a workaround.

fn sample_higher_zoomlevel_tile(tile_id: TileId, coords: vec2<u32>, size: u32) -> vec4<f32> {
    // get tile id of tile to sample from for current position
    var higher_zoomlevel_tile_id: TileId;
    higher_zoomlevel_tile_id.x = 2u * tile_id.x + select(0u, 1u, coords.x >= size);
    higher_zoomlevel_tile_id.y = 2u * tile_id.y + select(0u, 1u, coords.y < size);
    higher_zoomlevel_tile_id.zoomlevel = tile_id.zoomlevel + 1;

    // find correct hash for tile id
    var hash = hash_tile_id(higher_zoomlevel_tile_id);
    while(!tile_ids_equal(map_key_buffer[hash], higher_zoomlevel_tile_id) && !tile_id_empty(map_key_buffer[hash])) {
        hash++;
    }
    let was_found = !tile_id_empty(map_key_buffer[hash]);

    var sampled_value: vec4<f32>;
    if (!was_found) {
        sampled_value = vec4f(0);
    } else {
        let texture_index = map_value_buffer[hash];
        sampled_value = textureLoad(input_textures, coords % vec2u(size), texture_index, 0);
    }

    return sampled_value;
}

@compute @workgroup_size(1, 16, 16)
fn computeMain(@builtin(global_invocation_id) id: vec3<u32>) {
    let tile_id = input_tile_ids[id.x];
    let size = 256u; // TODO don't hardcode
    let final_coord = vec2<u32>(id.y, id.z);
    
    let coords_sample_00 = (2 * final_coord + vec2u(0, 0));
    let coords_sample_01 = (2 * final_coord + vec2u(0, 1));
    let coords_sample_10 = (2 * final_coord + vec2u(1, 0));
    let coords_sample_11 = (2 * final_coord + vec2u(1, 1));
    
    let sample_00 = sample_higher_zoomlevel_tile(tile_id, coords_sample_00, size);
    let sample_01 = sample_higher_zoomlevel_tile(tile_id, coords_sample_01, size);
    let sample_10 = sample_higher_zoomlevel_tile(tile_id, coords_sample_10, size);
    let sample_11 = sample_higher_zoomlevel_tile(tile_id, coords_sample_11, size);

    let final_value = (sample_00 + sample_01 + sample_10 + sample_11) * 0.25f;

    textureStore(output_textures, final_coord, id.x, final_value);
}
