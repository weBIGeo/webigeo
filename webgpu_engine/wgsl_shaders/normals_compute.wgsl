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
@group(0) @binding(1) var<storage> input_tile_bounds: array<vec4<f32>>;
@group(0) @binding(2) var<storage> map_key_buffer: array<TileId>; // hash map key buffer
@group(0) @binding(3) var<storage> map_value_buffer: array<u32>; // hash map value buffer, contains texture array indices
@group(0) @binding(4) var input_tiles: texture_2d_array<u32>; // height tiles

// output
@group(0) @binding(5) var output_tiles: texture_storage_2d_array<rgba8unorm, write>; // normal tiles (output)

fn get_texture_array_index(tile_id: TileId, texture_array_index: ptr<function, u32>, map_key_buffer: ptr<storage, array<TileId>>, map_value_buffer: ptr<storage, array<u32>>)  -> bool {
    // find correct hash for tile id
    var hash = hash_tile_id(tile_id);
    while(!tile_ids_equal(map_key_buffer[hash], tile_id) && !tile_id_empty(map_key_buffer[hash])) {
        hash++;
    }

    let was_found = !tile_id_empty(map_key_buffer[hash]);
    if (was_found) {
        // retrieve array layer by hash
        *texture_array_index = map_value_buffer[hash];
    } else {
        *texture_array_index = 0;
    }
    return was_found;
}

fn get_neighboring_tile_id_and_pos(num_edge_vertices: u32, tile_id: TileId, pos: vec2<i32>, out_tile_id: ptr<function, TileId>, out_pos: ptr<function, vec2<u32>>) {
    var new_pos = pos;
    var new_tile_id = tile_id;
    
    // tiles overlap! in each direction, the last value of one tile equals the first of the next
    // therefore offset by 1 in respective direction
    if (new_pos.x == -1) {
        new_pos.x = i32(num_edge_vertices - 2);
        new_tile_id.x -= 1;
    } else if (new_pos.x == i32(num_edge_vertices)) {
        new_pos.x = 1;
        new_tile_id.x += 1;
    }
    if (new_pos.y == -1) {
        new_pos.y = i32(num_edge_vertices - 2);
        new_tile_id.y += 1;
    } else if (new_pos.y == i32(num_edge_vertices)) {
        new_pos.y = 1;
        new_tile_id.y -= 1;
    }

    *out_tile_id = new_tile_id;
    *out_pos = vec2u(new_pos);
}


fn sample_height(num_edge_vertices: u32,
    tile_id: TileId,
    pos: vec2i,
    map_key_buffer: ptr<storage, array<TileId>>,
    map_value_buffer: ptr<storage, array<u32>>,
    height_textures: texture_2d_array<u32>
) -> u32 {
    var target_tile_id: TileId;
    var target_pos: vec2u;
    get_neighboring_tile_id_and_pos(num_edge_vertices, tile_id, pos, &target_tile_id, &target_pos);
    
    var texture_array_index: u32;
    let found = get_texture_array_index(target_tile_id, &texture_array_index, map_key_buffer, map_value_buffer);
    return select(0, textureLoad(height_textures, target_pos, texture_array_index, 0).r, found);
}

fn normal_by_finite_difference_method_with_neighbors(
    uv: vec2<f32>,
    edge_vertices_count: u32,
    quad_width: f32,
    quad_height: f32,
    altitude_correction_factor: f32,
    tile_id: TileId,
    tiles_map_key_buffer: ptr<storage, array<TileId>>,
    tiles_map_value_buffer: ptr<storage, array<u32>>,
    height_tiles_texture: texture_2d_array<u32>
) -> vec3<f32> {
    // from here: https://stackoverflow.com/questions/6656358/calculating-normals-in-a-triangle-mesh/21660173#21660173
    let height = quad_width + quad_height;
    let uv_tex = vec2<i32>(i32(uv.x * f32(edge_vertices_count - 1)), i32(uv.y * f32(edge_vertices_count - 1))); // in [0, texture_dimension(input_tiles) - 1]
    
    let hL_uv = uv_tex - vec2<i32>(1, 0);
    let hL_sample = sample_height(edge_vertices_count, tile_id, hL_uv, &map_key_buffer, &map_value_buffer, height_tiles_texture);
    let hL = f32(hL_sample) * altitude_correction_factor;

    let hR_uv = uv_tex + vec2<i32>(1, 0);
    let hR_sample = sample_height(edge_vertices_count, tile_id, hR_uv, &map_key_buffer, &map_value_buffer, height_tiles_texture);
    let hR = f32(hR_sample) * altitude_correction_factor;

    let hD_uv = uv_tex + vec2<i32>(0, 1);
    let hD_sample = sample_height(edge_vertices_count, tile_id, hD_uv, &map_key_buffer, &map_value_buffer, height_tiles_texture);
    let hD = f32(hD_sample) * altitude_correction_factor;

    let hU_uv = uv_tex - vec2<i32>(0, 1);
    let hU_sample = sample_height(edge_vertices_count, tile_id, hU_uv, &map_key_buffer, &map_value_buffer, height_tiles_texture);
    let hU = f32(hU_sample) * altitude_correction_factor;

    return normalize(vec3<f32>(hL - hR, hD - hU, height));
}

@compute @workgroup_size(1, 16, 16)
fn computeMain(@builtin(global_invocation_id) id: vec3<u32>) {
    // id.x  in [0, num_tiles]
    // id.yz in [0, ceil(texture_dimensions(output_tiles).xy / workgroup_size.yz) - 1]

    // exit if thread id is outside image dimensions (i.e. thread is not supposed to be doing any work)
    let output_n_edge_vertices = textureDimensions(output_tiles);
    if (id.y >= output_n_edge_vertices.x || id.z >= output_n_edge_vertices.y) {
        return;
    }
    let output_num_quads_per_direction = output_n_edge_vertices - 1;

    // id.yz in [0, texture_dimensions(output_tiles) - 1]

    let input_n_edge_vertices = textureDimensions(input_tiles).x; //TODO allow non-square
    let input_n_quads_per_direction: u32 = input_n_edge_vertices - 1;

    let tile_id = input_tile_ids[id.x];
    let bounds = input_tile_bounds[id.x];
    let quad_width: f32 = (bounds.z - bounds.x) / f32(input_n_quads_per_direction);
    let quad_height: f32 = (bounds.w - bounds.y) / f32(input_n_quads_per_direction);

    let col = id.y; // in [0, texture_dimension(output_tiles).x - 1]
    let row = id.z; // in [0, texture_dimension(output_tiles).y - 1]
    let uv = vec2f(f32(col) / f32(output_num_quads_per_direction.x), f32(row) / f32(output_num_quads_per_direction.y)); // in [0, 1]
    let pos_y = uv.y * f32(quad_height) + bounds.y;
    let altitude_correction_factor = calc_altitude_correction_factor(pos_y);
    let normal = normal_by_finite_difference_method_with_neighbors(uv, input_n_edge_vertices, quad_width, quad_height,
        altitude_correction_factor, tile_id, &map_key_buffer, &map_value_buffer, input_tiles);
    textureStore(output_tiles, vec2(col, row), id.x, vec4f(0.5 * normal + 0.5, 1));
}
