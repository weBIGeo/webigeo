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
}

@group(0) @binding(0) var<uniform> params : accumulation_params;
@group(0) @binding(1) var current_color: texture_2d<f32>;  // Low-res RGBA16Float
@group(0) @binding(2) var current_depth: texture_2d<f32>; // Low-res depth
@group(0) @binding(3) var linear_sampler: sampler;
@group(0) @binding(4) var accumulation_color: texture_storage_2d<rgba16float, read_write>; // High-res color
@group(0) @binding(5) var accumulation_depth: texture_storage_2d<r32float, read_write>; // High-res depth

// Blend factor constant - tune between 0.90-0.95
// Higher = smoother but more ghosting, Lower = less ghosting but more noise
const HISTORY_BLEND_FACTOR = 0.92;

// YCoCg color space for better chroma preservation
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

// Improved clip to AABB (from Temporal Reprojection Anti-Aliasing in INSIDE)
fn clip_aabb(aabb_min: vec3f, aabb_max: vec3f, prev_sample: vec3f, current_sample: vec3f) -> vec3f {
    let p_clip = 0.5 * (aabb_max + aabb_min);
    let e_clip = 0.5 * (aabb_max - aabb_min);

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

// Catmull-Rom bicubic filter for better upsampling
fn bicubic_catmull_rom(tex: texture_2d<f32>, uv: vec2f, texel_size: vec2f) -> vec4f {
    let size = vec2f(textureDimensions(tex));
    let sample_pos = uv * size;
    let texel_center = floor(sample_pos - 0.5) + 0.5;
    let f = sample_pos - texel_center;

    // Catmull-Rom weights
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

// Sample with 5-tap pattern for better neighborhood
fn sample_5tap(tex: texture_2d<f32>, uv: vec2f, texel_size: vec2f) -> vec4f {
    let c = textureSampleLevel(tex, linear_sampler, uv, 0.0);
    let l = textureSampleLevel(tex, linear_sampler, uv + vec2f(-texel_size.x, 0.0), 0.0);
    let r = textureSampleLevel(tex, linear_sampler, uv + vec2f(texel_size.x, 0.0), 0.0);
    let t = textureSampleLevel(tex, linear_sampler, uv + vec2f(0.0, -texel_size.y), 0.0);
    let b = textureSampleLevel(tex, linear_sampler, uv + vec2f(0.0, texel_size.y), 0.0);

    // Cross pattern with center weight
    return (c * 0.5 + l * 0.125 + r * 0.125 + t * 0.125 + b * 0.125);
}

fn world_position_from_depth(uv: vec2f, depth: f32, inv_view_proj: mat4x4f) -> vec3f {
    let ndc = vec3f(uv * 2.0 - 1.0, depth);
    let world_pos = inv_view_proj * vec4f(ndc, 1.0);
    return world_pos.xyz / world_pos.w;
}

@compute @workgroup_size(8, 8, 1)
fn computeMain(@builtin(global_invocation_id) global_id: vec3u) {
    let output_dims = textureDimensions(accumulation_color);
    let pixel_coord = global_id.xy;

    if (pixel_coord.x >= output_dims.x || pixel_coord.y >= output_dims.y) {
        return;
    }

    let high_res_uv = (vec2f(pixel_coord) + 0.5) / vec2f(output_dims);

    // Stabilize: Remove jitter to get stable sample position
    let stable_uv = high_res_uv - params.jitter;

    // Clamp to valid texture range
    if (any(stable_uv < vec2f(0.0)) || any(stable_uv > vec2f(1.0))) {
        let fallback = bicubic_catmull_rom(current_color, high_res_uv, params.low_res_texel_size);
        textureStore(accumulation_color, pixel_coord, fallback);
        let fallback_depth_coord = vec2i(high_res_uv * vec2f(textureDimensions(current_depth)));
        let fallback_depth = textureLoad(current_depth, fallback_depth_coord, 0).r;
        textureStore(accumulation_depth, pixel_coord, vec4f(fallback_depth, 0.0, 0.0, 0.0));
        return;
    }

    // Sample current frame (upsampled with bicubic)
    let current_sample = bicubic_catmull_rom(current_color, stable_uv, params.low_res_texel_size);
    let current_depth_coord = vec2i(stable_uv * vec2f(textureDimensions(current_depth)));
    let current_depth = textureLoad(current_depth, current_depth_coord, 0).r;

    // Early out for sky or transparent pixels
    if (current_sample.a < 0.01 || current_depth >= 0.9999) {
        textureStore(accumulation_color, pixel_coord, current_sample);
        textureStore(accumulation_depth, pixel_coord, vec4f(current_depth, 0.0, 0.0, 0.0));
        return;
    }

    // Compute 3x3 neighborhood statistics in YCoCg space
    var m1 = vec3f(0.0);
    var m2 = vec3f(0.0);
    var min_color = vec3f(1e10);
    var max_color = vec3f(-1e10);

    for (var y = -1; y <= 1; y++) {
        for (var x = -1; x <= 1; x++) {
            let offset = vec2f(f32(x), f32(y)) * params.low_res_texel_size;
            let neighbor_uv = stable_uv + offset;
            let neighbor = textureSampleLevel(current_color, linear_sampler, neighbor_uv, 0.0).rgb;
            let neighbor_ycocg = rgb_to_ycocg(neighbor);

            m1 += neighbor_ycocg;
            m2 += neighbor_ycocg * neighbor_ycocg;
            min_color = min(min_color, neighbor_ycocg);
            max_color = max(max_color, neighbor_ycocg);
        }
    }

    let mu = m1 / 9.0;
    let sigma = sqrt(max(m2 / 9.0 - mu * mu, vec3f(0.0)));

    // Variance clipping with gamma = 1.0 for better convergence
    // Expand box slightly based on local variance
    let variance_gamma = 1.0 + length(sigma) * 0.5;
    let box_min = mu - variance_gamma * sigma;
    let box_max = mu + variance_gamma * sigma;

    // Also use min/max clamping for hard boundaries
    let aabb_min = max(box_min, min_color);
    let aabb_max = min(box_max, max_color);

    // Reproject to find history sample
    let inv_view_proj = params.curr_camera.inv_view_matrix * params.curr_camera.inv_proj_matrix;
    let world_pos = world_position_from_depth(stable_uv, current_depth, inv_view_proj);

    let prev_view_proj = params.prev_camera.proj_matrix * params.prev_camera.view_matrix;
    let prev_clip = prev_view_proj * vec4f(world_pos, 1.0);
    let prev_ndc = prev_clip.xyz / prev_clip.w;
    let prev_uv = prev_ndc.xy * 0.5 + 0.5;

    // Check if reprojection is valid
    let outside = any(prev_uv < vec2f(0.0)) || any(prev_uv > vec2f(1.0)) || prev_clip.w <= 0.0;

    if (outside) {
        textureStore(accumulation_color, pixel_coord, current_sample);
        textureStore(accumulation_depth, pixel_coord, vec4f(current_depth, 0.0, 0.0, 0.0));
        return;
    }

    // Read history from the same buffers we'll write to
    // This is safe because each thread reads its reprojected location (different from write location)
    let prev_pixel = vec2i(prev_uv * vec2f(output_dims));
    let history_sample = textureLoad(accumulation_color, prev_pixel);
    let history_depth = textureLoad(accumulation_depth, prev_pixel).r;

    // Depth consistency check
    let depth_diff = abs(current_depth - history_depth);
    let depth_threshold = 0.001;
    let depth_weight = 1.0 - smoothstep(0.0, depth_threshold, depth_diff);

    // Clip history to neighborhood AABB
    let history_ycocg = rgb_to_ycocg(history_sample.rgb);
    let clipped_history_ycocg = clip_aabb(aabb_min, aabb_max, history_ycocg, rgb_to_ycocg(current_sample.rgb));
    let clipped_history_rgb = ycocg_to_rgb(clipped_history_ycocg);

    // Calculate clipping factor for adaptive blend
    let clip_amount = length(history_ycocg - clipped_history_ycocg) / (length(sigma) + 0.001);

    // Adaptive blending based on multiple factors
    let base_blend = HISTORY_BLEND_FACTOR;

    // Reduce history weight when:
    // 1. History was clipped significantly
    // 2. Depth is inconsistent
    // 3. Alpha is low (transparent/fading clouds)
    let clip_factor = 1.0 - smoothstep(0.0, 1.0, clip_amount);
    let alpha_factor = smoothstep(0.1, 0.5, current_sample.a);

    let adaptive_blend = base_blend * clip_factor * depth_weight * alpha_factor;

    // Blend current and history
    let result_rgb = mix(current_sample.rgb, clipped_history_rgb, adaptive_blend);
    let result_alpha = mix(current_sample.a, history_sample.a, adaptive_blend * 0.8);

    // Ensure no negative values (can happen with HDR)
    let final_color = vec4f(max(result_rgb, vec3f(0.0)), result_alpha);

    textureStore(accumulation_color, pixel_coord, final_color);
    textureStore(accumulation_depth, pixel_coord, vec4f(current_depth, 0.0, 0.0, 0.0));
}