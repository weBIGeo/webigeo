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

#include "color_mapping.wgsl"
#include "tile_util.wgsl"
#include "tile_hashmap.wgsl"

// input
@group(0) @binding(0) var<storage> input_tile_ids: array<TileId>; // tiles ids to process
@group(0) @binding(1) var<storage> map_key_buffer: array<TileId>; // hash map key buffer
@group(0) @binding(2) var<storage> map_value_buffer: array<u32>; // hash map value buffer, contains texture array indices

@group(0) @binding(3) var<storage> input_storage_buffer: array<u32>; // trajectory tiles

// output
@group(0) @binding(4) var output_tiles: texture_storage_2d_array<rgba8unorm, write>; // trajectory tiles (output)


fn get_storage_buffer_index(texture_layer: u32, coords: vec2u, output_texture_size: vec2u) -> u32 {
    return texture_layer * output_texture_size.x * output_texture_size.y + coords.y * output_texture_size.x + coords.x;  
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
    let col = id.y; // in [0, texture_dimension(output_tiles).x - 1]
    let row = id.z; // in [0, texture_dimension(output_tiles).y - 1]
    
    //TODO get array index from 
    var output_texture_array_index: u32;
    let found_output_tile = get_texture_array_index(tile_id, &output_texture_array_index, &map_key_buffer, &map_value_buffer);
    if (!found_output_tile) {
        return;
    }

    let buffer_index = get_storage_buffer_index(output_texture_array_index, vec2u(col, row), output_texture_size);
    let risk_value = f32(input_storage_buffer[buffer_index]) / (2 << 16);
    if (risk_value == 0.0) {
        return;
    }
    let color = color_mapping_bergfex(risk_value);
    textureStore(output_tiles, vec2u(col, row), id.x, vec4f(color, 1.0));
}