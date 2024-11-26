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

#include "util/filtering.wgsl"
#include "util/tile_util.wgsl"
#include "util/tile_hashmap.wgsl"
#include "util/normals_util.wgsl"
#include "util/color_mapping.wgsl"

struct AvalancheTrajectoriesSettings {
    output_resolution: vec2u,
    sampling_interval: vec2u, // n means starting a trajectory at every n-th texel

    num_steps: u32, // maximum number of steps (along gradient)
    step_length: f32, // length of one simulation step in world space
    source_zoomlevel: u32,

    model_type: u32, //0 is simple, 1 is more complex
    model1_linear_drag_coeff: f32,
    model1_downward_acceleration_coeff: f32,
    model2_gravity: f32,
    model2_mass: f32,
    model2_friction_coeff: f32,
    model2_drag_coeff: f32,

    trigger_point_min_slope_angle: f32, // in rad
    trigger_point_max_slope_angle: f32, // in rad

    //model5_weights: array<f32, 8>, // wgsl compiler does not allow f32 arrays in uniforms because of padding requirements, altough it would work here
    model5_weights: array<vec4f, 2>,
    model5_center_height_offset: f32,

    runout_model_type: u32, //0 none, 1 perla

    runout_perla_my: f32, // sliding friction coeff
    runout_perla_md: f32, // M/D mass-to-drag ratio
    runout_perla_l: f32, // distance between grid cells (in m)
    runout_perla_g: f32, // acceleration due to gravity (in m/s^2)

    padding1: u32,
    padding2: u32,
}

// input
@group(0) @binding(0) var<storage> input_tile_ids: array<TileId>; // tiles ids to process
@group(0) @binding(1) var<storage> input_tile_bounds: array<vec4<f32>>; // pre-computed tile bounds per tile id (in world space, relative to reference point)
@group(0) @binding(2) var<uniform> settings: AvalancheTrajectoriesSettings;

@group(0) @binding(3) var<storage> map_key_buffer: array<TileId>; // hash map key buffer
@group(0) @binding(4) var<storage> map_value_buffer: array<u32>; // hash map value buffer, contains texture array indices
@group(0) @binding(5) var input_normal_tiles: texture_2d_array<f32>; // normal tiles
@group(0) @binding(6) var input_normal_tiles_sampler: sampler; // normal sampler
@group(0) @binding(7) var input_height_tiles: texture_2d_array<u32>; // height tiles
@group(0) @binding(8) var input_height_tiles_sampler: sampler; // height sampler

@group(0) @binding(9) var<storage> output_tiles_map_key_buffer: array<TileId>; // hash map key buffer for output tiles
@group(0) @binding(10) var<storage> output_tiles_map_value_buffer: array<u32>; // hash map value buffer, contains texture array indice for output tiles

// output
@group(0) @binding(11) var<storage, read_write> output_storage_buffer: array<atomic<u32>>; // trajectory tiles

// note: as of writing this, wgsl only supports atomic access for storage buffers and only for u32 and i32
//       therefore, we first write the risk value (along the trajectory as raster) into a buffer,
//       then write its contents into a texture in a subsequent step (avalanche_trajectories_buffer_to_texture.wgsl)


// ***** UTILITY FUNCTIONS *****

// checks if thread should do something (based on sampling interval)
// TODO: just start correct number of threads from CPU side
fn should_paint(col: u32, row: u32, tile_id: TileId) -> bool {
    return (col % settings.sampling_interval.x == 0) && (row % settings.sampling_interval.y == 0);
}

// returns index into the storage buffer for a 2d (texture) position
fn get_storage_buffer_index(texture_layer: u32, coords: vec2u, output_texture_size: vec2u) -> u32 {
    return texture_layer * output_texture_size.x * output_texture_size.y + coords.y * output_texture_size.x + coords.x;  
}

// returns slope angle in radians based on surface normal (0 is horizontal, pi/2 is vertical)
fn get_slope_angle(normal: vec3f) -> f32 {
    return acos(normal.z);
    //return (1 - normal.z) * (PI / 2); // previous implementation, incorrect
}

// map overlay (non-overlapping) uv coordinate to normal texture (overlapping) uv coordinate
fn get_normal_uv(overlay_uv: vec2f) -> vec2f {
    return overlay_uv * vec2f(textureDimensions(input_normal_tiles) - 1) / vec2f(textureDimensions(input_normal_tiles)) + 1f / (2f * vec2f(textureDimensions(input_normal_tiles)));
}

// map overlay (non-overlapping) uv coordinate to height texture (overlapping) uv coordinate
fn get_height_uv(overlay_uv: vec2f) -> vec2f {
    return overlay_uv * vec2f(textureDimensions(input_height_tiles) - 1) / vec2f(textureDimensions(input_height_tiles)) + 1f / (2f * vec2f(textureDimensions(input_height_tiles)));
}

// get normal for a specific overlay tile id and uv.
//  - if uv is not in [0,1], internally calculates correct tile id and uv
// if tile id exists, sets value of normal ptr to normal, returns true; otherwise returns false 
fn get_normal(tile_id: TileId, overlay_uv: vec2f, zoomlevel: u32, normal: ptr<function, vec3f>) -> bool {
    var source_tile_id: TileId = tile_id;
    var source_uv: vec2f = overlay_uv;
    calc_tile_id_and_uv_for_zoom_level(tile_id, overlay_uv, zoomlevel, &source_tile_id, &source_uv);
    let normal_texture_uv = get_normal_uv(source_uv);
    var texture_array_index: u32;
    let found = get_texture_array_index(source_tile_id, &texture_array_index, &map_key_buffer, &map_value_buffer);
    if (!found) {
        // moved to a tile where we don't have any input data, discard
        return false;
    }
    *normal = bilinear_sample_vec4f(input_normal_tiles, input_normal_tiles_sampler, normal_texture_uv, texture_array_index).xyz * 2 - 1;
    return true;
}

// get height for a specific overlay tile id and uv.
//  - if uv is not in [0,1], internally calculates correct tile id and uv
// if tile id exists, sets value of height ptr to height, returns true; otherwise returns false
fn get_height(tile_id: TileId, overlay_uv: vec2f, zoomlevel: u32, height: ptr<function, u32>) -> bool {
    var source_tile_id: TileId = tile_id;
    var source_uv: vec2f = overlay_uv;
    calc_tile_id_and_uv_for_zoom_level(tile_id, overlay_uv, zoomlevel, &source_tile_id, &source_uv);
    let height_texture_uv = get_height_uv(source_uv);
    var texture_array_index: u32;
    let found = get_texture_array_index(source_tile_id, &texture_array_index, &map_key_buffer, &map_value_buffer);
    if (!found) {
        // moved to a tile where we don't have any input data, discard
        return false;
    }
    *height = bilinear_sample_u32(input_height_tiles, input_height_tiles_sampler, height_texture_uv, texture_array_index);
    return true;
}

// checks if specific position is a trigger point for avalanches (using min/max slope angle in settings)
fn is_trigger_point(slope_angle: f32) -> bool {
    // check if slope angle is in allowed interval
    if (slope_angle < settings.trigger_point_min_slope_angle || slope_angle > settings.trigger_point_max_slope_angle) {
        return false;
    }

    return true;
}

// takes tile id and arbitrary uv coordinates (not restricted to [0,1])
// returns new tile id and uv, such that uv is in [0,1]
fn offset_uv(tile_id: TileId, uv: vec2f, output_tile_id: ptr<function, TileId>, output_uv: ptr<function, vec2f>) {
    let new_uv = fract(uv); //TODO this is actually never 1; also we might need some offset because of tile overlap (i think)
    let uv_space_tile_offset = vec2i(floor(uv));
    let world_space_tile_offset = vec2i(uv_space_tile_offset.x, -uv_space_tile_offset.y); // world space y is opposite to uv space y, therefore invert y
    let new_tile_coords = vec2i(i32(tile_id.x), i32(tile_id.y)) + world_space_tile_offset;
    let new_tile_id = TileId(u32(new_tile_coords.x), u32(new_tile_coords.y), tile_id.zoomlevel, 0);
    *output_uv = new_uv;
    *output_tile_id = new_tile_id;
}


// ***** MODELS *****

fn model_physics_simple(normal: vec3f, velocity: vec3f) -> vec3f {
    // simple model: slows down by a constant factor of the velocity, speeds up by constant factor of normalized gradient
    let gradient = get_gradient(normal);

    let velocity_change = -settings.model1_linear_drag_coeff * velocity + settings.model1_downward_acceleration_coeff * gradient;
    return velocity_change;
}

fn model_physics_less_simple(normal: vec3f, velocity: vec3f) -> vec3f {
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

fn model_gradient(normal: vec3f) -> vec2f {
    return get_gradient(normal).xy;
}

fn model_discretized_gradient(normal: vec3f) -> vec2f {
    // discreticed gradient, probably not the most efficient way of doing that
    let grad = get_gradient(normal).xy;
    if (grad.x == 0 && grad.y == 0) {
        return vec2f(0);
    }
    let angle = atan2(-grad.y, grad.x) + PI; // now [0, 2*pi]
    let angle_adjusted = (angle + PI / 8) % (2 * PI);
    let neighbor_index = min(i32((angle_adjusted) / (2 * PI) * 8), 7); //could be 8 if angle = +pi
    const possible_neighbors = array<vec2f, 8>(
                            vec2f(-1,0),
                            vec2f(-1,1),
                            vec2f(0,1),
                            vec2f(1,1),
                            vec2f(1,0),
                            vec2f(1,-1),
                            vec2f(0,-1),
                            vec2f(-1,-1));
    return possible_neighbors[neighbor_index];
}

fn model_d8_without_weights(tile_id: TileId, uv: vec2f) -> vec2f {
    const directions = array<vec2f, 8>(vec2f(1, 0), vec2f(1, 1), vec2f(0, 1), vec2f(-1, 1), vec2f(-1, 0), vec2f(-1, -1), vec2f(0, -1), vec2f(1, -1));
    let step_size_to_neighbor = vec2f(1) / vec2f(settings.output_resolution - 1);

    var min_height: u32 = 1 << 31;
    var min_index: u32;
    for (var i: u32 = 0; i < 8; i++) {
        var neighbor_tile_id: TileId;
        var neighbor_uv: vec2f;
        offset_uv(tile_id, uv + step_size_to_neighbor * directions[i], &neighbor_tile_id, &neighbor_uv);

        var neighbor_height: u32;
        if (!get_height(neighbor_tile_id, neighbor_uv, settings.source_zoomlevel, &neighbor_height)) {
            //TODO handle error somehow, maybe return incorrect dir or something?
            return vec2f(0);
        }

        if (neighbor_height < min_height) {
            min_height = neighbor_height;
            min_index = i;
        }
    }
    return directions[min_index];
}

// like d8, but multiplies heights with a weight before comparing them
// the weight is chosen based on the last direction taken (passed via index into directions array)
// if there was no previous movement, all directions are weighted equally (indicated by passing -1)
// if all weights are 1, the output should be the same as d8 without weights 
fn model_d8_with_weights(tile_id: TileId, uv: vec2f, last_dir_index: i32, selected_dir_index: ptr<function, i32>, weights: array<f32, 8>) -> vec2f {
    const directions = array<vec2f, 8>(vec2f(1, 0), vec2f(1, 1), vec2f(0, 1), vec2f(-1, 1), vec2f(-1, 0), vec2f(-1, -1), vec2f(0, -1), vec2f(1, -1));
    //const weights = array<f32, 8>(1, 0.707, 0, 0, 0, 0, 0, 0.707);

    let step_size_to_neighbor = vec2f(1) / vec2f(settings.output_resolution - 1);

    var this_height_u32: u32;
    if (!get_height(tile_id, uv, settings.source_zoomlevel, &this_height_u32)) {
        //TODO handle error somehow, maybe return incorrect dir or something?
        return vec2f(0);
    }
    let this_height = f32(this_height_u32) + settings.model5_center_height_offset;

    var max_weighted_descent: f32 = -100000; // positive if neighboring cell has lower height than this 
    var max_weighted_descent_index: i32;
    for (var i: i32 = 0; i < 8; i++) {
        var neighbor_tile_id: TileId;
        var neighbor_uv: vec2f;
        offset_uv(tile_id, uv + step_size_to_neighbor * directions[i], &neighbor_tile_id, &neighbor_uv);

        var neighbor_height: u32;
        if (!get_height(neighbor_tile_id, neighbor_uv, settings.source_zoomlevel, &neighbor_height)) {
            //TODO handle error somehow, maybe return incorrect dir or something?
            return vec2f(0);
        }
        
        let weight = select(weights[(i - last_dir_index) % 8], 1, last_dir_index == -1);
        let weighted_descent = weight * (this_height - f32(neighbor_height));

        if (weighted_descent > max_weighted_descent) {
            max_weighted_descent = weighted_descent;
            max_weighted_descent_index = i;
        }
    }
    *selected_dir_index = max_weighted_descent_index; 
    return directions[max_weighted_descent_index];
}


fn runout_perla(last_velocity: f32, last_theta: f32, normal: vec3f, out_theta: ptr<function, f32>) -> f32 {
    let my = settings.runout_perla_my; // sliding friction coeff
    let md = settings.runout_perla_md; // M/D mass-to-drag ratio
    let l = settings.runout_perla_l; // distance between grid cells
    let g = settings.runout_perla_g; // acceleration due to gravity
    let this_theta = get_slope_angle(normal); // local slope angle

    let this_alpha = g * (sin(this_theta) - my * cos(this_theta));
    let this_beta = -2f * l / (md);
    let diff_theta = max(0, last_theta - this_theta);
    let this_velocity = sqrt(this_alpha * md * (1 - exp(this_beta)) + pow(last_velocity, 2) * exp(this_beta) * cos(diff_theta));
    
    *out_theta = this_theta;
    return this_velocity;
}

// ***** OVERLAY IMPLEMENTATIONS *****

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

// draws traces within a single tile
fn traces_overlay(id: vec3<u32>) {
    let tile_id = input_tile_ids[id.x];
    let bounds = input_tile_bounds[id.x];
    let input_texture_size = textureDimensions(input_normal_tiles);
    let tile_width: f32 = (bounds.z - bounds.x);
    let tile_height: f32 = (bounds.w - bounds.y);

    let col = id.y; // in [0, texture_dimension(output_tiles).x - 1]
    let row = id.z; // in [0, texture_dimension(output_tiles).y - 1]

    // a texel's uv coordinate should be its center, therefore shift down and right by half a texel
    // the overlay uv coordinates and height/normal tile uv coordinates are NOT the same because height/normal textures are overlapping
    let overlay_uv = vec2f(f32(col), f32(row)) / vec2f(settings.output_resolution - 1) + 1f / (2f * vec2f(settings.output_resolution - 1));

    if (!should_paint(col, row, tile_id)) {
        return;
    }

    // get slope angle at start
    var start_normal: vec3f;
    get_normal(tile_id, overlay_uv, settings.source_zoomlevel, &start_normal);
    let start_slope_angle = get_slope_angle(start_normal);

    if (!is_trigger_point(start_slope_angle)) {
        return;
    }

    var max_slope_angle = 0.0;
    var velocity = vec3f(0, 0, 0);

    var perla_velocity = 0f;
    var perla_theta = 0f;

    var last_dir_index: i32 = -1;  // used for d8 with weights
    var world_space_offset = vec2f(0, 0); // offset from original world position
    for (var i: u32 = 0; i < settings.num_steps; i++) {
        // calculate tile id and overlay uv coordinates for current position
        let overlay_uv_space_offset = vec2f(world_space_offset.x, -world_space_offset.y) / vec2f(tile_width, tile_height);
        var current_tile_id: TileId;
        var current_tile_uv: vec2f;
        offset_uv(tile_id, overlay_uv + overlay_uv_space_offset, &current_tile_id, &current_tile_uv);

        // paint trace point
        let output_coords = vec2u(floor(current_tile_uv * vec2f(settings.output_resolution)));
        var output_texture_array_index: u32;
        let found_output_tile = get_texture_array_index(current_tile_id, &output_texture_array_index, &output_tiles_map_key_buffer, &output_tiles_map_value_buffer);
        if (found_output_tile) {
            let buffer_index = get_storage_buffer_index(output_texture_array_index, output_coords, settings.output_resolution);
            atomicMax(&output_storage_buffer[buffer_index], u32((start_slope_angle / (PI / 2)) * (1 << 31))); // map slope angle to [0,1]
            //atomicMax(&output_storage_buffer[buffer_index], u32(settings.model5_center_height_offset * (1 << 31)));
            //atomicMax(&output_storage_buffer[buffer_index], u32((length(velocity) / 25.0f) * (1 << 31)));
        }

        // read normal, get direction using model, check stopping criterion, update position
        var normal: vec3f;
        if (!get_normal(current_tile_id, current_tile_uv, settings.source_zoomlevel, &normal)) {
            break;
        }

        if (settings.model_type == 0) {
            velocity += model_physics_simple(normal, velocity);
            world_space_offset = world_space_offset + settings.step_length * velocity.xy;
        } else if (settings.model_type == 1) {
            velocity += settings.step_length * model_physics_less_simple(normal, velocity);
            world_space_offset = world_space_offset + settings.step_length * velocity.xy;
        } else if (settings.model_type == 2) {
            let direction = model_gradient(normal);
            world_space_offset = world_space_offset + settings.step_length * direction;
        } else if (settings.model_type == 3) {
            let direction = model_discretized_gradient(normal);
            let step = direction * vec2f(tile_width, tile_height) / vec2f(settings.output_resolution);
            world_space_offset = world_space_offset + settings.step_length * step;
        } else if (settings.model_type == 4) {
            let uv_direction = model_d8_without_weights(current_tile_id, current_tile_uv);
            let world_direction = vec2f(uv_direction.x, -uv_direction.y);
            let step_uv_offset = (1f / vec2f(settings.output_resolution));
            world_space_offset = world_space_offset + world_direction * step_uv_offset * vec2f(tile_width, tile_height);
        } else if (settings.model_type == 5) {
            let w1 = settings.model5_weights[0];
            let w2 = settings.model5_weights[1];
            let weights = array<f32, 8>(w1.x, w1.y, w1.z, w1.w, w2.x, w2.y, w2.z, w2.w);
            let uv_direction = model_d8_with_weights(current_tile_id, current_tile_uv, last_dir_index, &last_dir_index, weights);
            let world_direction = vec2f(uv_direction.x, -uv_direction.y);
            let step_uv_offset = (1f / vec2f(settings.output_resolution));
            world_space_offset = world_space_offset + world_direction * step_uv_offset * vec2f(tile_width, tile_height);
        }

        if (settings.runout_model_type == 1) {
            perla_velocity = runout_perla(perla_velocity, perla_theta, normal, &perla_theta);

            //let buffer_index = get_storage_buffer_index(output_texture_array_index, output_coords, settings.output_resolution);
            //atomicMax(&output_storage_buffer[buffer_index], u32(1000f * (perla_velocity / 10.0f)));

            if (perla_velocity < 0.01) { //TODO
                break;
            }
        }
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