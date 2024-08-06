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
#include "tile_hashmap.wgsl"
#include "noise.wgsl"

struct SnowSettings {
    angle: vec4f,
    alt: vec4f,
}

// input
@group(0) @binding(0) var<storage> input_tile_ids: array<TileId>;
@group(0) @binding(1) var<storage> input_tile_bounds: array<vec4<f32>>;
@group(0) @binding(2) var<uniform> snow_settings: SnowSettings;

@group(0) @binding(3) var<storage> map_key_buffer: array<TileId>; // hash map key buffer
@group(0) @binding(4) var<storage> map_value_buffer: array<u32>; // hash map value buffer, contains texture array indices
@group(0) @binding(5) var input_tiles: texture_2d_array<u32>; // height tiles

@group(0) @binding(6) var input_tiles_sampler: sampler;

// output
@group(0) @binding(7) var output_tiles: texture_storage_2d_array<rgba8unorm, write>; // snow tiles (output)


//TODO find nice place to put
fn calculate_falloff(dist: f32, lower: f32, upper: f32) -> f32 { return clamp(1.0 - (dist - lower) / (upper - lower), 0.0, 1.0); }

fn world_to_lat_long_alt(pos_ws: vec3f) -> vec3f {
    let mercN = pos_ws.y * PI / ORIGIN_SHIFT;
    let latRad = 2.0 * (atan(exp(mercN)) - (PI / 4.0));
    let latitude = latRad * 180.0 / PI;
    let longitude = (pos_ws.x + ORIGIN_SHIFT) / (ORIGIN_SHIFT / 180.0) - 180.0;
    let altitude = pos_ws.z * cos(latitude * PI / 180.0);
    return vec3f(latitude, longitude, altitude);
}

fn calculate_band_falloff(val: f32, min: f32, max: f32, smoothf: f32) -> f32 {
    if (val < min) { return calculate_falloff(val, min + smoothf, min); }
    else if (val > max) { return calculate_falloff(val, max, max + smoothf); }
    else { return 1.0; }
}

fn overlay_snow(normal: vec3f, pos_ws: vec3f) -> vec4f {
    // Calculate steepness in deg where 90.0 = vertical (90°) and 0.0 = flat (0°)
    let steepness_deg = (1.0 - dot(normal, vec3(0.0, 0.0, 1.0))) * 90.0;

    let steepness_based_alpha = calculate_band_falloff(
                steepness_deg,
                snow_settings.angle.y,
                snow_settings.angle.z,
                snow_settings.angle.w);

    let lat_long_alt = world_to_lat_long_alt(pos_ws);
    let pos_noise_hf = noise(pos_ws / 70.0);
    let pos_noise_lf = noise(pos_ws / 500.0);
    let snow_border = snow_settings.alt.x
            + (snow_settings.alt.y * (2.0 * pos_noise_lf - 0.5))
            + (snow_settings.alt.y * (0.5 * (pos_noise_hf - 0.5)));
    let altitude_based_alpha = calculate_falloff(
                lat_long_alt.z,
                snow_border,
                snow_border - snow_settings.alt.z * pos_noise_lf) ;

    let snow_color = vec3f(1.0);
    return vec4f(snow_color, altitude_based_alpha * steepness_based_alpha);
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
        altitude_correction_factor, tile_id, &map_key_buffer, &map_value_buffer, input_tiles, input_tiles_sampler);
    
    let pos_x = uv.x * f32(quad_width) + bounds.x;
    let pos_z = altitude_correction_factor * f32(sample_height_with_index(tile_id, vec2u(uv * vec2f(f32(input_n_edge_vertices - 1))), &map_key_buffer, &map_value_buffer, input_tiles));
    let overlay = overlay_snow(normal, vec3f(pos_x, pos_y, pos_z));

    textureStore(output_tiles, vec2(col, row), id.x, overlay); // incorrect
}
