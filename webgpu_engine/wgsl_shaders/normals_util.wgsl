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

fn normal_by_finite_difference_method(
    uv: vec2<f32>,
    quad_width: f32,
    quad_height: f32,
    altitude_correction_factor: f32,
    texture_layer: i32,
    texture_array: texture_2d_array<u32>) -> vec3<f32>
{
    let height_texture_size = textureDimensions(texture_array);
    // from here: https://stackoverflow.com/questions/6656358/calculating-normals-in-a-triangle-mesh/21660173#21660173
    let height = quad_width + quad_height;
    
    // 0 is texel center of first texel, 1 is texel center of last texel
    let uv_tex = vec2i(floor(uv * vec2f(height_texture_size - 1) + 0.5));
    
    let upper_bounds = vec2<i32>(height_texture_size - 1);
    let lower_bounds = vec2<i32>(0, 0);
    let hL_uv = clamp(uv_tex - vec2<i32>(1, 0), lower_bounds, upper_bounds);
    let hL_sample = textureLoad(texture_array, hL_uv, texture_layer, 0);
    let hL = f32(hL_sample.r) * altitude_correction_factor;

    let hR_uv = clamp(uv_tex + vec2<i32>(1, 0), lower_bounds, upper_bounds);
    let hR_sample = textureLoad(texture_array, hR_uv, texture_layer, 0);
    let hR = f32(hR_sample.r) * altitude_correction_factor;

    let hD_uv = clamp(uv_tex + vec2<i32>(0, 1), lower_bounds, upper_bounds);
    let hD_sample = textureLoad(texture_array, hD_uv, texture_layer, 0);
    let hD = f32(hD_sample.r) * altitude_correction_factor;

    let hU_uv = clamp(uv_tex - vec2<i32>(0, 1), lower_bounds, upper_bounds);
    let hU_sample = textureLoad(texture_array, hU_uv, texture_layer, 0);
    let hU = f32(hU_sample.r) * altitude_correction_factor;

    return normalize(vec3<f32>(hL - hR, hD - hU, height));
}

fn normal_by_finite_difference_method_with_neighbors(
    uv: vec2<f32>,
    quad_width: f32,
    quad_height: f32,
    altitude_correction_factor: f32,
    tile_id: TileId,
    tiles_map_key_buffer: ptr<storage, array<TileId>>,
    tiles_map_value_buffer: ptr<storage, array<u32>>,
    height_tiles_texture: texture_2d_array<u32>,
    height_tiles_sampler: sampler
) -> vec3<f32> {
    // from here: https://stackoverflow.com/questions/6656358/calculating-normals-in-a-triangle-mesh/21660173#21660173
    let height_texture_size: vec2u = textureDimensions(height_tiles_texture);
    
    let height = quad_width + quad_height;

    // 0 is texel center of first texel, 1 is texel center of last texel
    let uv_tex = vec2i(floor(uv * vec2f(height_texture_size - 1) + 0.5));

    let hL_uv = uv_tex - vec2<i32>(1, 0);
    let hL_sample = sample_height_with_neighbors(height_texture_size.x, tile_id, hL_uv, tiles_map_key_buffer, tiles_map_value_buffer, height_tiles_texture, height_tiles_sampler);
    let hL = f32(hL_sample) * altitude_correction_factor;

    let hR_uv = uv_tex + vec2<i32>(1, 0);
    let hR_sample = sample_height_with_neighbors(height_texture_size.x, tile_id, hR_uv, tiles_map_key_buffer, tiles_map_value_buffer, height_tiles_texture, height_tiles_sampler);
    let hR = f32(hR_sample) * altitude_correction_factor;

    let hD_uv = uv_tex + vec2<i32>(0, 1);
    let hD_sample = sample_height_with_neighbors(height_texture_size.x, tile_id, hD_uv, tiles_map_key_buffer, tiles_map_value_buffer, height_tiles_texture, height_tiles_sampler);
    let hD = f32(hD_sample) * altitude_correction_factor;

    let hU_uv = uv_tex - vec2<i32>(0, 1);
    let hU_sample = sample_height_with_neighbors(height_texture_size.x, tile_id, hU_uv, tiles_map_key_buffer, tiles_map_value_buffer, height_tiles_texture, height_tiles_sampler);
    let hU = f32(hU_sample) * altitude_correction_factor;

    return normalize(vec3<f32>(hL - hR, hD - hU, height));
}

fn get_gradient(normal: vec3f) -> vec3f {
    let up = vec3f(0, 0, 1);
    let right = cross(up, normal);
    let gradient = cross(right, normal);
    return gradient;
}