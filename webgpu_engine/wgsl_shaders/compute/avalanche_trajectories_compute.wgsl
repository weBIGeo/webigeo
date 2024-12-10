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
#include "util/normals_util.wgsl"

struct AvalancheTrajectoriesSettings {
    output_resolution: vec2u,
    region_size: vec2f, // world space width and height of the the region we operate on

    num_steps: u32, // maximum number of steps (along gradient)
    step_length: f32, // length of one simulation step in world space

    model_type: u32, //0 is simple, 1 is more complex
    model1_linear_drag_coeff: f32,
    model1_downward_acceleration_coeff: f32,
    model2_gravity: f32,
    model2_mass: f32,
    model2_friction_coeff: f32,
    model2_drag_coeff: f32,

    //model5_weights: array<f32, 8>, // wgsl compiler does not allow f32 arrays in uniforms because of padding requirements, altough it would work here
    model5_weights: array<vec4f, 2>,
    model5_center_height_offset: f32,

    runout_model_type: u32, //0 none, 1 perla

    runout_perla_my: f32, // sliding friction coeff
    runout_perla_md: f32, // M/D mass-to-drag ratio
    runout_perla_l: f32, // distance between grid cells (in m)
    runout_perla_g: f32, // acceleration due to gravity (in m/s^2)
}

// input
@group(0) @binding(0) var<uniform> settings: AvalancheTrajectoriesSettings;
@group(0) @binding(1) var input_normal_texture: texture_2d<f32>;
@group(0) @binding(2) var input_height_texture: texture_2d<f32>;
@group(0) @binding(3) var input_release_point_texture: texture_2d<f32>;
@group(0) @binding(4) var input_sampler: sampler;

// output
@group(0) @binding(5) var<storage, read_write> output_storage_buffer: array<atomic<u32>>; // trajectory texture

// note: as of writing this, wgsl only supports atomic access for storage buffers and only for u32 and i32
//       therefore, we first write the risk value (along the trajectory as raster) into a buffer,
//       then write its contents into a texture in a subsequent step (avalanche_trajectories_buffer_to_texture.wgsl)


// ***** UTILITY FUNCTIONS *****

// writes single pixel to storage buffer, value must be in [0,1]
fn write_pixel_at_pos(pos: vec2u, value: f32) {
    let buffer_index = pos.y * settings.output_resolution.x + pos.x;
    let value_u32 =  u32(value * (1 << 31)); // map value from [0,1] angle to [0, 2^32 - 1]
    atomicMax(&output_storage_buffer[buffer_index], value_u32);         
}

fn sample_normal_texture(uv: vec2f) -> vec3f {
    let texture_dimensions: vec2u = textureDimensions(input_normal_texture) - 1;
    let weights: vec2f = fract(uv * vec2f(texture_dimensions));

    let x = dot(vec4f((1.0 - weights.x) * weights.y, weights.x * weights.y, weights.x * (1.0 - weights.y), (1.0 - weights.x) * (1.0 - weights.y)),
        vec4f(textureGather(0, input_normal_texture, input_sampler, uv)));
    let y = dot(vec4f((1.0 - weights.x) * weights.y, weights.x * weights.y, weights.x * (1.0 - weights.y), (1.0 - weights.x) * (1.0 - weights.y)),
        vec4f(textureGather(1, input_normal_texture, input_sampler, uv)));
    let z = dot(vec4f((1.0 - weights.x) * weights.y, weights.x * weights.y, weights.x * (1.0 - weights.y), (1.0 - weights.x) * (1.0 - weights.y)),
        vec4f(textureGather(2, input_normal_texture, input_sampler, uv)));

    return vec3f(x, y, z) * 2 - 1;

    // as of writing this, textureSample can only be used in the fragment stage, thus compute shaders cannot use it (see doc)
    //return textureSample(input_normal_texture, input_sampler, uv).xyz * 2 - 1;
}

fn sample_height_texture(uv: vec2f) -> f32 {
    let texture_dimensions = textureDimensions(input_height_texture);
    let weights: vec2f = fract(uv * vec2f(texture_dimensions));
    let texel_values: vec4f = textureGather(0, input_height_texture, input_sampler, uv);
    return dot(vec4f((1.0 - weights.x) * weights.y, weights.x * weights.y, weights.x * (1.0 - weights.y), (1.0 - weights.x) * (1.0 - weights.y)), texel_values);
}

fn sample_release_point_texture(uv: vec2f) -> bool {
    let texture_dimensions = textureDimensions(input_release_point_texture);
    let pos = vec2u(uv * vec2f(texture_dimensions - 1));
    let mask = textureLoad(input_release_point_texture, pos, 0).rgba;
    return mask.a > 0;
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
    /*const directions = array<vec2f, 8>(vec2f(1, 0), vec2f(1, 1), vec2f(0, 1), vec2f(-1, 1), vec2f(-1, 0), vec2f(-1, -1), vec2f(0, -1), vec2f(1, -1));
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
    return directions[min_index];*/
    return vec2f(0, 0);
}

// like d8, but multiplies heights with a weight before comparing them
// the weight is chosen based on the last direction taken (passed via index into directions array)
// if there was no previous movement, all directions are weighted equally (indicated by passing -1)
// if all weights are 1, the output should be the same as d8 without weights 
fn model_d8_with_weights(tile_id: TileId, uv: vec2f, last_dir_index: i32, selected_dir_index: ptr<function, i32>, weights: array<f32, 8>) -> vec2f {
    /*const directions = array<vec2f, 8>(vec2f(1, 0), vec2f(1, 1), vec2f(0, 1), vec2f(-1, 1), vec2f(-1, 0), vec2f(-1, -1), vec2f(0, -1), vec2f(1, -1));
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
    return directions[max_weighted_descent_index];*/
    return vec2f(0, 0);
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


fn trajectory_overlay(id: vec3<u32>) {
    let input_texture_size = textureDimensions(input_normal_texture);
    
    // a texel's uv coordinate should be its center, therefore shift down and right by half a texel
    let uv = vec2f(f32(id.x), f32(id.y)) / vec2f(settings.output_resolution - 1) + 1f / (2f * vec2f(settings.output_resolution));

    if (!sample_release_point_texture(uv)) {
        return;
    }

    // get slope angle at start
    let start_normal = sample_normal_texture(uv);
    let start_slope_angle = get_slope_angle(start_normal);
    let trajectory_value = start_slope_angle / (PI / 2);

    var velocity = vec3f(0, 0, 0);

    var perla_velocity = 0f;
    var perla_theta = 0f;

    var last_dir_index: i32 = -1;  // used for d8 with weights
    var world_space_offset = vec2f(0, 0); // offset from original world position
    for (var i: u32 = 0; i < settings.num_steps; i++) {
        // compute uv coordinates for current position
        let current_uv = uv + vec2f(world_space_offset.x, -world_space_offset.y) / settings.region_size;

        // quit if moved out of bounds
        if (current_uv.x < 0 || current_uv.x > 1 || current_uv.y < 0 || current_uv.y > 1) {
            break;
        }

        // draw trajectory point
        // TODO draw line between last point and this point
        let output_coords = vec2u(floor(current_uv * vec2f(settings.output_resolution - 1)));
        write_pixel_at_pos(output_coords, trajectory_value);

        // sample normal and get new world space offset based on chosen model
        let normal = sample_normal_texture(current_uv);
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
            world_space_offset = world_space_offset + settings.step_length * direction;
        } else if (settings.model_type == 4) {
            /*let uv_direction = model_d8_without_weights(current_tile_id, current_tile_uv);
            let world_direction = vec2f(uv_direction.x, -uv_direction.y);
            let step_uv_offset = (1f / vec2f(settings.output_resolution));
            world_space_offset = world_space_offset + world_direction * step_uv_offset * settings.region_size;*/
        } else if (settings.model_type == 5) {
            /*let w1 = settings.model5_weights[0];
            let w2 = settings.model5_weights[1];
            let weights = array<f32, 8>(w1.x, w1.y, w1.z, w1.w, w2.x, w2.y, w2.z, w2.w);
            let uv_direction = model_d8_with_weights(current_tile_id, current_tile_uv, last_dir_index, &last_dir_index, weights);
            let world_direction = vec2f(uv_direction.x, -uv_direction.y);
            let step_uv_offset = (1f / vec2f(settings.output_resolution));
            world_space_offset = world_space_offset + world_direction * step_uv_offset * settings.region_size;*/
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

@compute @workgroup_size(16, 16, 1)
fn computeMain(@builtin(global_invocation_id) id: vec3<u32>) {
    // id.xy in [0, ceil(texture_dimensions(output_tiles).xy / workgroup_size.xy) - 1]

    // exit if thread id is outside image dimensions (i.e. thread is not supposed to be doing any work)
    if (id.x >= settings.output_resolution.x || id.y >= settings.output_resolution.y) {
        return;
    }
    // id.xy in [0, texture_dimensions(output_tiles) - 1]

    trajectory_overlay(id);
}