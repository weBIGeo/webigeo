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

#include "filtering.wgsl"
#include "tile_util.wgsl"
#include "tile_hashmap.wgsl"
#include "normals_util.wgsl"
#include "color_mapping.wgsl"

struct AvalancheTrajectoriesSettings {
    output_resolution: vec2u,
    padding1: f32,
    padding2: f32,

    target_point: vec4f,
    reference_point: vec4f,
    num_steps: u32, // maximum number of steps (along gradient)
    step_length: f32, // length of one simulation step in world space
    radius: f32,
    source_zoomlevel: u32,

    model_type: u32, //0 is simple, 1 is more complex
    model1_linear_drag_coeff: f32,
    model1_downward_acceleration_coeff: f32,
    model2_gravity: f32,
    model2_mass: f32,
    model2_friction_coeff: f32,
    model2_drag_coeff: f32,
    padding3: f32,
}

// input
@group(0) @binding(0) var<storage> input_tile_ids: array<TileId>; // tiles ids to process
@group(0) @binding(1) var<storage> input_tile_bounds: array<vec4<f32>>; // pre-computed tile bounds per tile id (in world space, relative to reference point)
@group(0) @binding(2) var<uniform> settings: AvalancheTrajectoriesSettings;

@group(0) @binding(3) var<storage> map_key_buffer: array<TileId>; // hash map key buffer
@group(0) @binding(4) var<storage> map_value_buffer: array<u32>; // hash map value buffer, contains texture array indices
@group(0) @binding(5) var input_normal_tiles: texture_2d_array<f32>; // normal tiles
@group(0) @binding(6) var input_normal_tiles_sampler: sampler; // normal sampler
@group(0) @binding(7) var input_height_tiles: texture_2d_array<f32>; // height tiles
@group(0) @binding(8) var input_height_tiles_sampler: sampler; // height sampler

@group(0) @binding(9) var<storage> output_tiles_map_key_buffer: array<TileId>; // hash map key buffer for output tiles
@group(0) @binding(10) var<storage> output_tiles_map_value_buffer: array<u32>; // hash map value buffer, contains texture array indice for output tiles

// output
@group(0) @binding(11) var<storage, read_write> output_storage_buffer: array<atomic<u32>>; // trajectory tiles

const SAMPLING_DENSITY = 16; // traces only: grid frequency in xy direction in (output texture) texels

fn should_paint(col: u32, row: u32, tile_id: TileId) -> bool {
    return (col % SAMPLING_DENSITY == 0) && (row % SAMPLING_DENSITY == 0);
    //return (col % 16 == 0) && (row % 16 == 0) && (tile_id.x == 140386 + 1) && (tile_id.y == 169805 + 1);
    //return (col == 0) && (row == 0) && (tile_id.x == 140386 + 1) && (tile_id.y == 169805 + 1);
    //return (col == 64) && (row == 64) && (tile_id.x == 140386 + 1) && (tile_id.y == 169805 + 1);
}

fn gradient_overlay(id: vec3<u32>) {
    let tile_id = input_tile_ids[id.x];
    let bounds = input_tile_bounds[id.x];
    let input_texture_size = textureDimensions(input_normal_tiles);
    let tile_width: f32 = (bounds.z - bounds.x);
    let tile_height: f32 = (bounds.w - bounds.y);

    let col = id.y; // in [0, texture_dimension(output_tiles).x - 1]
    let row = id.z; // in [0, texture_dimension(output_tiles).y - 1]
    let uv = vec2f(f32(col), f32(row)) / vec2f(settings.output_resolution - 1);

    var source_tile_id: TileId = tile_id;
    var source_uv: vec2f = uv;
    calc_tile_id_and_uv_for_zoom_level(tile_id, uv, settings.source_zoomlevel, &source_tile_id, &source_uv);

    // read normal
    var texture_array_index: u32;
    let found = get_texture_array_index(source_tile_id, &texture_array_index, &map_key_buffer, &map_value_buffer);
    if (!found) {
        return;
    }
    let normal = bilinear_sample_vec4f(input_normal_tiles, input_normal_tiles_sampler, source_uv, texture_array_index).xyz * 2 - 1;
    let gradient = get_gradient(normal);
    //textureStore(output_tiles, vec2u(col, row), id.x, vec4f(gradient, 1));
}

fn model1(normal: vec3f, velocity: vec3f) -> vec3f {
    // simple model: slows down by a constant factor of the velocity, speeds up by constant factor of normalized gradient
    let gradient = get_gradient(normal);

    let velocity_change = -settings.model1_linear_drag_coeff * velocity + settings.model1_downward_acceleration_coeff * gradient;
    return velocity_change;
}

fn model2(normal: vec3f, velocity: vec3f) -> vec3f {
    // trying to come up with a more realistic model with tunable parameters
    //   Fw = m * g * (0,0,-1)                  ... weight force
    //   Fn = (-Fw . normal) * normal           ... normal force (Fw + Fn is the part of the weight force acting along gradient)
    //   Ff = friction_coeff * |N| * (-v / |v|) ... friction force, acting against direction of current velocity
    //   Fd = drag_coeff * |v|^2 * (-v / |v|)   ... drag force, acting against direction of current velocity
    // TODO need to convert between world space and meters
    let gravity = vec3f(0, 0, -settings.model2_gravity);
    let mass = settings.model2_mass;
    let friction_coefficient = settings.model2_friction_coeff;
    let drag_coefficient = settings.model2_drag_coeff;
    
    let velocity_magnitude = length(velocity);
    let against_motion_dir = -normalize(velocity);

    let f_weight = mass * gravity;
    let f_normal = dot(-f_weight, normal) * normal;
    let f_friction = friction_coefficient * length(f_normal) * select(vec3f(0), against_motion_dir, velocity_magnitude > 0);
    let f_drag = drag_coefficient * pow(velocity_magnitude, 2) * select(vec3f(0), against_motion_dir, velocity_magnitude > 0);
    let f_net = f_weight + f_normal + f_friction + f_drag;
    
    let a = f_net / mass;
    return settings.step_length * a;
}

fn get_storage_buffer_index(texture_layer: u32, coords: vec2u, output_texture_size: vec2u) -> u32 {
    return texture_layer * output_texture_size.x * output_texture_size.y + coords.y * output_texture_size.x + coords.x;  
}

// draws traces within a single tile
fn traces_overlay(id: vec3<u32>) {
    let tile_id = input_tile_ids[id.x];
    let bounds = input_tile_bounds[id.x];
    let input_texture_size = textureDimensions(input_normal_tiles);
    let tile_width: f32 = (bounds.z - bounds.x);
    let tile_height: f32 = (bounds.w - bounds.y);

    let col = id.y; // in [0, texture_dimension(output_tiles).x - 1]
    let row = id.z; // in [0, texture_dimension(output_tiles).y - 1]
    let uv = vec2f(f32(col), f32(row)) / vec2f(settings.output_resolution - 1);

    if (!should_paint(col, row, tile_id)) {
        return;
    }

    var max_steepness = 0.0;
    var velocity = vec3f(0, 0, 0);

    var world_space_offset = vec2f(0, 0); // offset from original world position
    for (var i: u32 = 0; i < settings.num_steps; i++) {
        // calculate tile id and uv coordinates
        let uv_space_offset = vec2f(world_space_offset.x, -world_space_offset.y) / vec2f(tile_width, tile_height);
        let new_uv = fract(uv + uv_space_offset); //TODO this is actually never 1; also we might need some offset because of tile overlap (i think)
        let uv_space_tile_offset = vec2i(floor(uv + uv_space_offset));
        let world_space_tile_offset = vec2i(uv_space_tile_offset.x, -uv_space_tile_offset.y); // world space y is opposite to uv space y, therefore invert y
        let new_tile_coords = vec2i(i32(tile_id.x), i32(tile_id.y)) + world_space_tile_offset;
        let new_tile_id = TileId(u32(new_tile_coords.x), u32(new_tile_coords.y), tile_id.zoomlevel, 0);

        // read normal
        var source_tile_id: TileId = new_tile_id;
        var source_uv: vec2f = new_uv;
        calc_tile_id_and_uv_for_zoom_level(new_tile_id, new_uv, settings.source_zoomlevel, &source_tile_id, &source_uv);
        var texture_array_index: u32;
        let found = get_texture_array_index(source_tile_id, &texture_array_index, &map_key_buffer, &map_value_buffer);
        if (!found) {
            // moved to a tile where we don't have any input data, discard
            break;
        }
        let normal = bilinear_sample_vec4f(input_normal_tiles, input_normal_tiles_sampler, source_uv, texture_array_index).xyz * 2 - 1;


        let new_steepness = 1.0 - normal.z;
        max_steepness = max(max_steepness, new_steepness);

        var velocity_change: vec3f;
        if (settings.model_type == 0) {
            velocity_change = model1(normal, velocity);
        } else if (settings.model_type == 1) {
            velocity_change = settings.step_length * model2(normal, velocity);
        } else if (settings.model_type == 2) {
            velocity_change = -velocity + get_gradient(normal);
        }

        // update velocity
        velocity = velocity + velocity_change;

        // paint trace point
        let output_coords = vec2u(new_uv * vec2f(settings.output_resolution));
        var output_texture_array_index: u32;
        let found_output_tile = get_texture_array_index(new_tile_id, &output_texture_array_index, &output_tiles_map_key_buffer, &output_tiles_map_value_buffer);
        if (found_output_tile) {
            // color by distinct starting point
            //let color = vec3(f32(col) / f32(settings.output_resolution.x), f32(row) / f32(settings.output_resolution.y), 0.0);

            // color by max steepness
            //let color = color_mapping_bergfex(max_steepness);

            // color by velocity
            let color = vec3f(length(velocity) / 50.0, 0, 0);

            // color by num steps
            //let color = vec3(1.0 - f32(i) / f32(settings.num_steps), 0.0, 0.0);

            let buffer_index = get_storage_buffer_index(output_texture_array_index, output_coords, settings.output_resolution);
            atomicMax(&output_storage_buffer[buffer_index], u32(max_steepness * (2 << 16)));
            //atomicMax(&output_storage_buffer[buffer_index], u32((length(velocity) / 25.0f) * (2 << 16)));


            //textureStore(output_tiles, output_coords, output_texture_array_index, vec4f(color, 1.0));
        }

        // update position
        world_space_offset = world_space_offset + settings.step_length * velocity.xy;
    }

    // overpaint start point
    //textureStore(output_tiles, vec2u(col, row), id.x, vec4f(0.0, 0.0, 1.0, 1.0));
}

@compute @workgroup_size(1, 16, 16)
fn computeMain(@builtin(global_invocation_id) id: vec3<u32>) {
    // id.x  in [0, num_tiles]
    // id.yz in [0, ceil(texture_dimensions(output_tiles).xy / workgroup_size.yz) - 1]

    // exit if thread id is outside image dimensions (i.e. thread is not supposed to be doing any work)
    if (id.y >= settings.output_resolution.x || id.z >= settings.output_resolution.y) {
        return;
    }
    // id.yz in [0, texture_dimensions(output_tiles) - 1]

    //gradient_overlay(id);
    traces_overlay(id);
}