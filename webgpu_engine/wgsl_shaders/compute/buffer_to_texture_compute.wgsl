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

#include "util/color_mapping.wgsl"

// input
@group(0) @binding(0) var<uniform> settings: BufferToTextureSettings;
@group(0) @binding(1) var<storage> input_storage_buffer: array<u32>;

// output
@group(0) @binding(2) var output_texture: texture_storage_2d<rgba8unorm, write>;

struct BufferToTextureSettings {
    input_resolution: vec2u,
}

fn read_buffer_at(pos: vec2u) -> u32 {
    let index = pos.y * settings.input_resolution.x + pos.x;
    return input_storage_buffer[index];  
}

@compute @workgroup_size(16, 16, 1)
fn computeMain(@builtin(global_invocation_id) id: vec3<u32>) {
    // id.xy in [0, ceil(input_resolution / workgroup_size.xy) - 1]

    // exit if thread id is outside image dimensions (i.e. thread is not supposed to be doing any work)
    if (id.x >= settings.input_resolution.x || id.y >= settings.input_resolution.y) {
        return;
    }
    // id.xy in [0, texture_dimensions(output_tiles) - 1]

    let risk_value = f32(read_buffer_at(id.xy)) / (1 << 31);
    //let risk_value = f32(input_storage_buffer[buffer_index]) / 1000f;
    if (risk_value == 0.0) {
        return;
    }
    let color = color_mapping_bergfex(risk_value);
    textureStore(output_texture, id.xy, vec4f(color, 1.0));
}