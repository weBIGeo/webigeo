struct camera_config {
    view_matrix: mat4x4f,
    proj_matrix: mat4x4f,
    inv_view_matrix: mat4x4f,
    inv_proj_matrix: mat4x4f,
    position: vec4f,
}

struct accumulation_params {
    curr_camera: camera_config,
    prev_camera: camera_config,
    jitter: vec2f,
    prev_jitter: vec2f,
    low_res_texel_size: vec2f,
    high_res_texel_size: vec2f,
    resolution_scale: vec2f,
    _padding0: vec2f,
}

@group(0) @binding(0) var<uniform> params : accumulation_params;
@group(0) @binding(1) var current_color: texture_2d<f32>;
@group(0) @binding(2) var current_depth: texture_2d<f32>;
@group(0) @binding(3) var linear_sampler: sampler;
@group(0) @binding(4) var accumulation_color_r: texture_storage_2d<rgba16float, read>;
@group(0) @binding(5) var accumulation_depth_r: texture_storage_2d<r32float, read>;
@group(0) @binding(6) var accumulation_color_w: texture_storage_2d<rgba16float, write>;
@group(0) @binding(7) var accumulation_depth_w: texture_storage_2d<r32float, write>;

@group(1) @binding(0) var depth_texture: texture_2d<f32>;

// Tuned for volumetric clouds
const HISTORY_BLEND_FACTOR = 0.95;
const DEPTH_THRESHOLD = 0.01;  // Relaxed for volumetrics
const MIN_BLEND_FACTOR = 0.5;   // Never fully reject history

// YCoCg color space
fn rgb_to_ycocg(rgb: vec3f) -> vec3f {
    let Y  = dot(rgb, vec3f(0.25, 0.5, 0.25));
    let Co = dot(rgb, vec3f(0.5, 0.0, -0.5));
    let Cg = dot(rgb, vec3f(-0.25, 0.5, -0.25));
    return vec3f(Y, Co, Cg);
}

fn ycocg_to_rgb(ycocg: vec3f) -> vec3f {
    let Y  = ycocg.x;
    let Co = ycocg.y;
    let Cg = ycocg.z;
    return vec3f(Y + Co - Cg, Y + Cg, Y - Co - Cg);
}

// Variance-based clipping (the main defense against ghosting)
fn clip_aabb(aabb_min: vec3f, aabb_max: vec3f, prev_sample: vec3f, current_sample: vec3f) -> vec3f {
    let p_clip = 0.5 * (aabb_max + aabb_min);
    let e_clip = 0.5 * (aabb_max - aabb_min) + vec3f(0.001);

    let v_clip = prev_sample - p_clip;
    let v_unit = v_clip / e_clip;
    let a_unit = abs(v_unit);
    let ma_unit = max(a_unit.x, max(a_unit.y, a_unit.z));

    if (ma_unit > 1.0) {
        return p_clip + v_clip / ma_unit;
    } else {
        return prev_sample;
    }
}

// Catmull-Rom bicubic filter
fn bicubic_catmull_rom(tex: texture_2d<f32>, uv: vec2f, texel_size: vec2f) -> vec4f {
    let size = vec2f(textureDimensions(tex));
    let sample_pos = uv * size;
    let texel_center = floor(sample_pos - 0.5) + 0.5;
    let f = sample_pos - texel_center;

    let w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    let w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    let w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    let w3 = f * f * (-0.5 + 0.5 * f);

    let w12 = w1 + w2;
    let tc0 = (texel_center - 1.0) * texel_size;
    let tc12 = (texel_center + w2 / w12) * texel_size;
    let tc3 = (texel_center + 2.0) * texel_size;

    let col0 = textureSampleLevel(tex, linear_sampler, vec2f(tc12.x, tc0.y), 0.0) * w12.x
             + textureSampleLevel(tex, linear_sampler, vec2f(tc0.x, tc0.y), 0.0) * w0.x
             + textureSampleLevel(tex, linear_sampler, vec2f(tc3.x, tc0.y), 0.0) * w3.x;

    let col1 = textureSampleLevel(tex, linear_sampler, vec2f(tc12.x, tc12.y), 0.0) * w12.x
             + textureSampleLevel(tex, linear_sampler, vec2f(tc0.x, tc12.y), 0.0) * w0.x
             + textureSampleLevel(tex, linear_sampler, vec2f(tc3.x, tc12.y), 0.0) * w3.x;

    let col2 = textureSampleLevel(tex, linear_sampler, vec2f(tc12.x, tc3.y), 0.0) * w12.x
             + textureSampleLevel(tex, linear_sampler, vec2f(tc0.x, tc3.y), 0.0) * w0.x
             + textureSampleLevel(tex, linear_sampler, vec2f(tc3.x, tc3.y), 0.0) * w3.x;

    return col0 * w0.y + col1 * w12.y + col2 * w3.y;
}

fn world_position_from_depth(uv: vec2f, depth: f32, inv_view_proj: mat4x4f) -> vec3f {
    let ndc = vec3f(uv * 2.0 - 1.0, depth);
    let world_pos = inv_view_proj * vec4f(ndc, 1.0);
    return world_pos.xyz / world_pos.w;
}

// Calculate luminance for better temporal stability
fn luminance(rgb: vec3f) -> f32 {
    return dot(rgb, vec3f(0.299, 0.587, 0.114));
}


@compute @workgroup_size(8, 8, 1)
fn computeMain(@builtin(global_invocation_id) global_id: vec3u) {
    let output_dims = textureDimensions(accumulation_color_r);
    let pixel_coord = global_id.xy;

    if (pixel_coord.x >= output_dims.x || pixel_coord.y >= output_dims.y) {
        return;
    }

    let high_res_uv = (vec2f(pixel_coord) + 0.5) / vec2f(output_dims);
    let jitter_high_res = params.jitter * params.resolution_scale;
    let sample_uv = high_res_uv + jitter_high_res;

    // Bounds check
    if (any(sample_uv < vec2f(0.0)) || any(sample_uv > vec2f(1.0))) {
        let fallback = bicubic_catmull_rom(current_color, high_res_uv, params.low_res_texel_size);
        textureStore(accumulation_color_w, pixel_coord, fallback);
        let fallback_depth_coord = vec2i(high_res_uv * vec2f(textureDimensions(current_depth)));
        let fallback_depth = textureLoad(current_depth, fallback_depth_coord, 0).r;
        textureStore(accumulation_depth_w, pixel_coord, vec4f(fallback_depth, 0.0, 0.0, 0.0));
        return;
    }

    // Sample current frame
    let current_sample = bicubic_catmull_rom(current_color, sample_uv, params.low_res_texel_size);
    let current_depth_coord = vec2i(sample_uv * vec2f(textureDimensions(current_depth)));
    let current_depth = textureLoad(current_depth, current_depth_coord, 0).r;

    // Compute 3x3 neighborhood statistics in YCoCg space
    // This is the MAIN protection against ghosting - it constrains history to the local neighborhood
    var m1 = vec3f(0.0);
    var m2 = vec3f(0.0);
    var alpha_min = 1.0;
    var alpha_max = 0.0;
    var count = 0.0;

    for (var y = -1; y <= 1; y++) {
        for (var x = -1; x <= 1; x++) {
            let offset = vec2f(f32(x), f32(y)) * params.low_res_texel_size;
            let neighbor_uv = sample_uv + offset;

            if (all(neighbor_uv >= vec2f(0.0)) && all(neighbor_uv <= vec2f(1.0))) {
                let neighbor = textureSampleLevel(current_color, linear_sampler, neighbor_uv, 0.0);
                let neighbor_ycocg = rgb_to_ycocg(neighbor.rgb);

                m1 += neighbor_ycocg;
                m2 += neighbor_ycocg * neighbor_ycocg;

                alpha_min = min(alpha_min, neighbor.a);
                alpha_max = max(alpha_max, neighbor.a);

                count += 1.0;
            }
        }
    }

    let mu = m1 / count;
    let sigma = sqrt(max(m2 / count - mu * mu, vec3f(0.0)));

    // Very conservative variance clipping for noisy volumetrics
    // The variance naturally captures the noise, so we expand the box significantly
    let variance_gamma = 1.5;  // Allow more variation
    let box_min = mu - variance_gamma * sigma;
    let box_max = mu + variance_gamma * sigma;

    // Reproject to find history sample
    let inv_view_proj = params.curr_camera.inv_view_matrix * params.curr_camera.inv_proj_matrix;
    let world_pos = world_position_from_depth(sample_uv, current_depth, inv_view_proj);

    let prev_view_proj = params.prev_camera.proj_matrix * params.prev_camera.view_matrix;
    let prev_clip = prev_view_proj * vec4f(world_pos, 1.0);
    let prev_ndc = prev_clip.xyz / prev_clip.w;
    let prev_uv = prev_ndc.xy * 0.5 + 0.5;

    // Check if reprojection is valid (CRITICAL disocclusion check)
    let outside = any(prev_uv < vec2f(0.0)) || any(prev_uv > vec2f(1.0)) || prev_clip.w <= 0.0;

    if (outside) {
        textureStore(accumulation_color_w, pixel_coord, current_sample);
        textureStore(accumulation_depth_w, pixel_coord, vec4f(current_depth, 0.0, 0.0, 0.0));
        return;
    }

    // Read history
    let prev_pixel = vec2i(prev_uv * vec2f(output_dims));
    let history_sample = textureLoad(accumulation_color_r, prev_pixel);
    let history_depth = textureLoad(accumulation_depth_r, prev_pixel).r;

    // Depth consistency check (CRITICAL disocclusion check)
    let depth_diff = abs(current_depth - history_depth);
    var depth_weight = 1.0 - smoothstep(0.0, DEPTH_THRESHOLD, depth_diff);

    // Terrain depth masking
    let base_depth_coord = vec2i(pixel_coord / 2) * 2;
    let pixel_depth = textureLoad(depth_texture, pixel_coord, 0).x;
    let d00 = textureLoad(depth_texture, base_depth_coord + vec2i(0, 0), 0).x;
    let d10 = textureLoad(depth_texture, base_depth_coord + vec2i(1, 0), 0).x;
    let d01 = textureLoad(depth_texture, base_depth_coord + vec2i(0, 1), 0).x;
    let d11 = textureLoad(depth_texture, base_depth_coord + vec2i(1, 1), 0).x;
    let farthest_terrain_depth = min(min(d00, d10), min(d01, d11));
    let terrain_depth_diff = max(pixel_depth - farthest_terrain_depth, 0.0);

    depth_weight *= 1.0 - saturate(terrain_depth_diff / 0.000001);

    // Camera motion detection
    let camera_motion = length(params.curr_camera.position.xyz - params.prev_camera.position.xyz);
    let motion_weight = 1.0 - smoothstep(0.0, 1.0, camera_motion);

    // Clip history to neighborhood AABB (MAIN ghosting protection)
    let history_ycocg = rgb_to_ycocg(history_sample.rgb);
    let current_ycocg = rgb_to_ycocg(current_sample.rgb);
    let clipped_history_ycocg = clip_aabb(box_min, box_max, history_ycocg, current_ycocg);
    let clipped_history_rgb = ycocg_to_rgb(clipped_history_ycocg);
    let clipped_history_alpha = clamp(history_sample.a, alpha_min, alpha_max);

    // Calculate clipping amount - indicates possible disocclusion/scene change
    let clip_amount = length(history_ycocg - clipped_history_ycocg);
    let sigma_length = length(sigma) + 0.001;
    // Only reduce blend if clipping was VERY significant (outside 2 sigma)
    let clip_factor = 1.0 - smoothstep(sigma_length * 1.0, sigma_length * 2.0, clip_amount);

    // Combine ONLY the reliable factors for volumetrics:
    // 1. Depth consistency (real disocclusion)
    // 2. Camera motion (potential disocclusion)
    // 3. Severe AABB clipping (scene change)
    //
    // We do NOT use luminance or alpha differences because volumetric noise
    // causes these to vary wildly frame-to-frame even with no actual change

    var adaptive_blend = HISTORY_BLEND_FACTOR;
    adaptive_blend *= depth_weight;    // Reject if depth changed
    adaptive_blend *= motion_weight;   // Reduce during camera motion
    adaptive_blend *= clip_factor;     // Reduce only for severe clipping
//    adaptive_blend = max(adaptive_blend, MIN_BLEND_FACTOR);

    // Special case: completely empty history
    if (history_sample.a < 0.001 && current_sample.a > 0.05) {
        adaptive_blend = 0.0;
    }

    // Blend current and history
    let result_rgb = mix(current_sample.rgb, clipped_history_rgb, adaptive_blend);
    let result_alpha = mix(current_sample.a, clipped_history_alpha, adaptive_blend);

    // Ensure no negative values
    let final_color = vec4f(max(result_rgb, vec3f(0.0)), max(result_alpha, 0.0));

    textureStore(accumulation_color_w, pixel_coord, final_color);
    textureStore(accumulation_depth_w, pixel_coord, vec4f(current_depth, 0.0, 0.0, 0.0));
}