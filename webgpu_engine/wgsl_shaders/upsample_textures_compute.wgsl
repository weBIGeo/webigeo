/*****************************************************************************
 * weBIGeo
 * Copyright (C) 2023 Gerald Kimmersdorfer
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

// input
@group(0) @binding(0) var<storage> indices: array<u32>;
@group(0) @binding(1) var input_textures: texture_2d_array<f32>;
@group(0) @binding(2) var input_sampler: sampler;

// output
@group(0) @binding(3) var output_textures: texture_storage_2d_array<rgba8unorm, write>;

fn bilinear_sample_vec4f(texture_array: texture_2d_array<f32>, texture_sampler: sampler, uv: vec2f, layer: u32) -> vec4f {
    let texture_dimensions: vec2u = textureDimensions(texture_array);

    // weights need to match the texels that are chosen by textureGather - this does NOT align perfectly, and introduces some artifacts
    // adding offset fixes this issue, see https://www.reedbeta.com/blog/texture-gathers-and-coordinate-precision/
    const offset = 1.0f / 512.0f;
    let weights: vec2f = fract(uv * vec2f(texture_dimensions) - 0.5 + offset);

    let x = dot(vec4f((1.0 - weights.x) * weights.y, weights.x * weights.y, weights.x * (1.0 - weights.y), (1.0 - weights.x) * (1.0 - weights.y)),
        vec4f(textureGather(0, texture_array, texture_sampler, uv, layer)));
    let y = dot(vec4f((1.0 - weights.x) * weights.y, weights.x * weights.y, weights.x * (1.0 - weights.y), (1.0 - weights.x) * (1.0 - weights.y)),
        vec4f(textureGather(1, texture_array, texture_sampler, uv, layer)));
    let z = dot(vec4f((1.0 - weights.x) * weights.y, weights.x * weights.y, weights.x * (1.0 - weights.y), (1.0 - weights.x) * (1.0 - weights.y)),
        vec4f(textureGather(2, texture_array, texture_sampler, uv, layer)));
    let w = dot(vec4f((1.0 - weights.x) * weights.y, weights.x * weights.y, weights.x * (1.0 - weights.y), (1.0 - weights.x) * (1.0 - weights.y)),
        vec4f(textureGather(3, texture_array, texture_sampler, uv, layer)));

    return vec4f(x, y, z, w);
}

@compute @workgroup_size(1, 16, 16)
fn computeMain(@builtin(global_invocation_id) id: vec3<u32>) {
    // id.x  in [0, num_textures]
    // id.yz in [0, ceil(texture_dimensions(output_tiles).xy / workgroup_size.yz) - 1]

    // exit if thread id is outside image dimensions (i.e. thread is not supposed to be doing any work)
    let output_texture_size: vec2u = textureDimensions(output_textures);
    if (id.y >= output_texture_size.x || id.z >= output_texture_size.y) {
        return;
    }
    // id.yz in [0, texture_dimensions(output_tiles) - 1]

    let texture_layer_index = indices[id.x];

    let col = id.y; // in [0, texture_dimension(output_tiles).x - 1]
    let row = id.z; // in [0, texture_dimension(output_tiles).y - 1]
    let input_texture_size: vec2u = textureDimensions(input_textures);
    let uv = vec2f(f32(col), f32(row)) / vec2f(output_texture_size - 1);
    let result = bilinear_sample_vec4f(input_textures, input_sampler, uv, texture_layer_index);
    
    textureStore(output_textures, vec2(col, row), texture_layer_index, result);
}
