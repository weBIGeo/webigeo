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

// input
@group(0) @binding(0) var<storage> input_tile_ids: array<TileId>;
@group(0) @binding(1) var<storage> map_key_buffer: array<TileId>; // hash map key buffer
@group(0) @binding(2) var<storage> map_value_buffer: array<u32>; // hash map value buffer, contains texture array indices
@group(0) @binding(3) var input_height_tiles: texture_2d_array<u32>; // height tiles
@group(0) @binding(4) var input_sampler: sampler; // height tiles sampler

// output
@group(0) @binding(5) var output_tiles: texture_storage_2d_array<rgba8unorm, write>; // d8 tiles (output)
//TODO currently, format r8uint cannot be used for storage texture with write access (apparently only 32 bit formats can be used)

//TODO use storage buffer instead for now!
//  ASAP! saves 3 byte per texel (75%) immediately, could optimize further (just 3 bit per texel for current impl)

fn get_height_value(tile_id: TileId, uv: vec2f) -> u32 {
    let height_texture_size = textureDimensions(input_height_tiles);
    let tex_pos = vec2i(floor(uv * vec2f(height_texture_size - 1)));
    return sample_height_with_neighbors(height_texture_size.x, tile_id, tex_pos, &map_key_buffer, &map_value_buffer, input_height_tiles, input_sampler);
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
    let uv = vec2f(f32(col), f32(row)) / vec2f(output_texture_size - 1) + 1f / (2f * vec2f(output_texture_size - 1));

    const directions = array<vec2f, 8>(vec2f(1, 0), vec2f(1, 1), vec2f(0, 1), vec2f(-1, 1), vec2f(-1, 0), vec2f(-1, -1), vec2f(0, -1), vec2f(1, -1));

    let step_size = vec2f(1) / (vec2f(output_texture_size - 1));
    var min_height: u32 = 1 << 31; //TODO
    var min_index: u32; //TODO
    for (var i: u32 = 0; i < 8; i++) {
        let height_value = get_height_value(tile_id, uv + step_size * directions[i]);
        if (height_value < min_height) {
            min_height = height_value;
            min_index = i;
        }
    }
    let encoded_direction: u32 = 1u << min_index;
    //textureStore(output_tiles, vec2(col, row), id.x, vec4f(encoded_direction, 0, 0, 0));
    textureStore(output_tiles, vec2(col, row), id.x, vec4f(f32(min_index) / 8f, 0, 0, 1));
    //textureStore(output_tiles, vec2(col, row), id.x, vec4f(uv.x, uv.y, 0, 1));

}
