#include "util/tile_util.wgsl"

struct tile_info {
    index: u32,
    zoom: u32,
}

struct camera_config {
    view_matrix: mat4x4f,
    proj_matrix: mat4x4f,
    inv_view_matrix: mat4x4f,
    inv_proj_matrix: mat4x4f,
    position: vec4f,
}

struct shader_params {
    camera: camera_config,
    bounds_min: vec4f,
    bounds_max: vec4f,
    frame_index: u32,
    _padding0: array<u32, 3>,
    start_distance: f32,
    start_step_size: f32,
    end_distance: f32,
    end_step_size: f32,
    extinction_multiplier: f32,
    detail_strength: f32,
    _padding1: array<f32, 2>,
}

@group(0) @binding(0) var<uniform> params : shader_params;
@group(0) @binding(1) var atlas_texture: texture_3d<f32>;
@group(0) @binding(2) var atlas_sampler: sampler;
@group(0) @binding(3) var<storage, read> tile_infos: array<tile_info>;
@group(0) @binding(4) var output_color: texture_storage_2d<rgba16float, write>;
@group(0) @binding(5) var output_depth: texture_storage_2d<r32float, write>;

@group(1) @binding(0) var depth_texture: texture_2d<f32>;

// tile size at zoom level 11
override tile_size_x = 39135.7584820102;
override tile_size_y = 39135.7584820102;
override inv_tile_size_x = 1.0 / 39135.7584820102;
override inv_tile_size_y = 1.0 / 39135.7584820102;

override tile_count_x = 46/2;
override tile_count_y = 26/2;

override zoom_max = 10;

override tile_coords_offset_x = 538;
override tile_coords_offset_y = 660;

// Volume bounds
const VOLUME_HEIGHT_MIN = 0.0;
const VOLUME_HEIGHT_MAX = 14000.0;

// Texture dimensions
const TILE_WIDTH = 256.0;
const TILE_HEIGHT = 256.0;
const TILE_DEPTH = 64.0;
const HEIGHT_PER_TEXEL = 14000.0 / TILE_DEPTH;
const INV_HEIGHT_PER_TEXEL = 1.0 / HEIGHT_PER_TEXEL;

// Ray marching parameters
const MAX_STEPS = 128;
const DENSITY_THRESHOLD = 0.00001;

// Lighting parameters
const SUN_DIRECTION = vec3f(0.5, 0.3, 0.7);
const SUN_COLOR = vec3f(1.0, 0.95, 0.9) * 20.0;
const SUN_INTENSITY = 2.5;
const AMBIENT_COLOR = vec3f(0.5, 0.6, 0.7);
const AMBIENT_INTENSITY = 0.8;

// Scattering parameters
const SCATTERING_COEFF = 0.7;
const BASE_EXTINCTION_COEFF = 0.01;
const SCATTERING_ALBEDO = 0.99;

const LIGHT_STEP_SIZE = 150.0;
const MAX_LIGHT_STEPS = 8;

// IGN (Interleaved Gradient Noise) function for per-pixel jitter
fn ign(pixel: vec2f, frame: u32) -> f32 {
    let f = vec3f(pixel, f32(frame));
    return fract(52.9829189 * fract(dot(f, vec3f(0.06711056, 0.00583715, 0.01336789))));
}

fn unproject(normalised_device_coordinates: vec3f) -> vec3f {
    let unprojected = params.camera.inv_proj_matrix * vec4(normalised_device_coordinates, 1.0);
    let normalised_unprojected = unprojected / unprojected.w;
    return (params.camera.inv_view_matrix * normalised_unprojected).xyz;
}

// Box-ray intersection
fn intersect_aabb(ray_origin: vec3f, ray_dir: vec3f, box_min: vec3f, box_max: vec3f) -> vec2f {
    let inv_dir = 1.0 / ray_dir;
    let t0 = (box_min - ray_origin) * inv_dir;
    let t1 = (box_max - ray_origin) * inv_dir;
    let tmin = min(t0, t1);
    let tmax = max(t0, t1);
    let t_near = max(max(tmin.x, tmin.y), tmin.z);
    let t_far = min(min(tmax.x, tmax.y), tmax.z);
    return vec2f(t_near, t_far);
}

fn get_tile_info_at_pos(pos_world: vec3f) -> tile_info {
    let pos_bs = pos_world - params.bounds_min.xyz;
    let pos_ts = pos_bs.xy * vec2f(inv_tile_size_x, inv_tile_size_y);
    let tile_id = vec2i(floor(pos_ts));

    if(tile_id.x < 0 || tile_id.x >= tile_count_x || tile_id.y < 0 || tile_id.y >= tile_count_y) {
        var default_tile: tile_info;
        default_tile.index = 0u;
        default_tile.zoom = u32(zoom_max);
        return default_tile;
    }

    let tile_index = tile_id.x + tile_id.y * tile_count_x;
    return tile_infos[tile_index];
}

fn sample_volume(pos_world: vec3f, lod: f32) -> f32 {
    // Check height bounds
    if (pos_world.z < VOLUME_HEIGHT_MIN || pos_world.z > VOLUME_HEIGHT_MAX) {
        return 0.0;
    }

    let tile = get_tile_info_at_pos(pos_world);
    let dz = max(u32(zoom_max) - tile.zoom, 0u);
    let tile_size_multiplier = f32(1u << dz);

    // Calculate position in tile space
    let pos_bs = pos_world - params.bounds_min.xyz;
    let pos_ts = pos_bs.xy * vec2f(inv_tile_size_x, inv_tile_size_y);
    let tile_coords_offset = vec2f(f32(tile_coords_offset_x), f32(tile_coords_offset_y));
    let tile_uv = fract((pos_ts + tile_coords_offset) / tile_size_multiplier);

    // Calculate atlas position
    let atlas_x = tile.index & 3u;
    let atlas_y = (tile.index >> 2u) & 3u;
    let atlas_z = (tile.index >> 4u) & 3u;

    let height_adjusted = pos_world.z / cos(y_to_lat(pos_world.y));
    // Height coordinate (normalized to texture space)
    let height_normalized = height_adjusted * INV_HEIGHT_PER_TEXEL / TILE_DEPTH;

    // Conservative mip-level-aware edge margin
    let mip_scale = exp2(lod);
    let edge_margin_xy = (0.5 * mip_scale) / TILE_WIDTH;
    let edge_margin_z = (0.5 * mip_scale) / TILE_DEPTH;

    let tile_uv_safe = clamp(tile_uv, vec2f(edge_margin_xy), vec2f(1.0 - edge_margin_xy));
    let height_safe = clamp(height_normalized, edge_margin_z, 1.0 - edge_margin_z);

    let tile_uvz = vec3f(
        tile_uv_safe.x + f32(atlas_x),
        tile_uv_safe.y + f32(atlas_y),
        height_safe + f32(atlas_z)
    );
    let atlas_uvz = tile_uvz * 0.25;

    return textureSampleLevel(atlas_texture, atlas_sampler, atlas_uvz, lod).r;
}

fn calculate_lod(step_size: f32, tile_zoom: u32, distance: f32, view_dir: vec3f) -> f32 {
    let dz = max(u32(zoom_max) - tile_zoom, 0u);
    let tile_size_multiplier = f32(1u << dz);

    // Texel sizes in world space
    let texel_size_xy = (tile_size_x * tile_size_multiplier) / TILE_WIDTH;  // Horizontal texel size
    let texel_size_z = HEIGHT_PER_TEXEL;  // Vertical texel size (much coarser!)

    // View direction components (how much we're looking in each axis)
    let view_xy = length(view_dir.xy);  // Horizontal component
    let view_z = abs(view_dir.z);        // Vertical component

    // Projected texel size in screen space (accounting for distance and view angle)
    // For a texel to project to 1 pixel, we need: texel_size / distance ≈ pixel_angular_size
    // We want to find the worst case (largest projected texel)
    let projected_texel_xy = texel_size_xy * view_xy / max(distance, 1.0);
    let projected_texel_z = texel_size_z * view_z / max(distance, 1.0);

    // Use the larger projected texel size (worst case)
    let max_projected_texel = max(projected_texel_xy, projected_texel_z);

    // Step size in screen space
    let projected_step = step_size / max(distance, 1.0);

    // LOD based on how many texels we're skipping per step
    // If step covers multiple texels, we can use a coarser mip
    let texel_ratio = projected_step / max(max_projected_texel, 1e-6);

    // Conservative: only use coarser mips when we're clearly over-sampling
    // FIXME: LODs too coarse
//    let lod = log2(max(texel_ratio * 0.5, 1.0));
    let lod = log2(max(texel_ratio * 0.5 * 0.1, 1.0));

    return clamp(lod, 0.0, 5.0);
}

// Saturate helper
fn saturate(x: f32) -> f32 {
    return clamp(x, 0.0, 1.0);
}

// Henyey-Greenstein phase function for anisotropic scattering
fn henyey_greenstein(cos_angle: f32, g: f32) -> f32 {
    let g2 = g * g;
    let denom = 1.0 + g2 - 2.0 * g * cos_angle;
    return (1.0 - g2) / (4.0 * 3.14159265 * pow(denom, 1.5));
}

// Improved phase function with better forward and back scattering
fn phase_function(cos_angle: f32) -> f32 {
    let forward = henyey_greenstein(cos_angle, SCATTERING_COEFF);
    let backward = henyey_greenstein(cos_angle, -SCATTERING_COEFF * 0.5);
    let isotropic = 0.25 / 3.14159265;
    return forward * 0.5 + backward * 0.2 + isotropic * 0.3;
}

// Calculate how much light reaches a point from the sun (light transmittance)
fn sample_light_energy(pos: vec3f, sun_dir: vec3f, distance: f32, extinction_coeff: f32) -> f32 {
    var optical_depth = 0.0;

    // March towards sun to accumulate density
    for (var i = 1; i <= MAX_LIGHT_STEPS; i++) {
        let step_pos = pos + sun_dir * f32(i) * LIGHT_STEP_SIZE;

        // Quick bounds check
        if (step_pos.z < VOLUME_HEIGHT_MIN || step_pos.z > VOLUME_HEIGHT_MAX) {
            break;
        }

        let tile = get_tile_info_at_pos(step_pos);
        let light_distance = distance + f32(i) * LIGHT_STEP_SIZE;
        let lod = calculate_lod(LIGHT_STEP_SIZE, tile.zoom, light_distance, sun_dir);

        let base_density = sample_volume(step_pos, lod);

        // TODO: Apply detail noise here
        let density = base_density;

        optical_depth += density * LIGHT_STEP_SIZE * extinction_coeff;

        // Early exit for deep shadows
        if (optical_depth > 10.0) {
            return 0.0;
        }
    }

    // Beer's law
    let transmittance = exp(-optical_depth);

    // Energy conservation: even in shadow, some light gets through via multi-scattering
    let multi_scatter = 0.3 * exp(-optical_depth * 0.25);

    return transmittance + multi_scatter;
}

// Dynamic step size based on distance
fn get_step_size(distance: f32) -> f32 {
    let start_dist = params.start_distance;
    let end_dist = params.end_distance;
    let start_step = params.start_step_size;
    let end_step = params.end_step_size;

    let t = clamp((distance - start_dist) / (end_dist - start_dist), 0.0, 1.0);
    // Use smoothstep for smoother transitions between step sizes
    let smooth_t = t * t * (3.0 - 2.0 * t);
    return mix(start_step, end_step, smooth_t);
}

// Convert world position to NDC depth for storage
fn world_to_depth(world_pos: vec3f) -> f32 {
    let view_pos = params.camera.view_matrix * vec4f(world_pos, 1.0);
    let clip_pos = params.camera.proj_matrix * view_pos;
    let ndc = clip_pos.xyz / clip_pos.w;
    return ndc.z;
}

fn get_depth_offset(frame: u32) -> vec2i {
    let i = frame % 4u;
    if (i == 0u) { return vec2i(0, 0); }
    if (i == 1u) { return vec2i(1, 1); }
    if (i == 2u) { return vec2i(0, 1); }
    return vec2i(1, 0);
}

@compute @workgroup_size(8, 8, 1)
fn computeMain(@builtin(global_invocation_id) global_id: vec3u) {
    let output_dims = textureDimensions(output_color);
    let pixel_coord = global_id.xy;

    if (pixel_coord.x >= output_dims.x || pixel_coord.y >= output_dims.y) {
        return;
    }

    // Calculate normalized coordinates
    // Note: The camera projection matrix is already jittered on the CPU side
    // We add per-pixel IGN jitter here to break up banding artifacts
    let pixel_center = vec2f(pixel_coord) + 0.5;

    // IGN provides per-pixel sub-sample jitter WITHIN the frame
    // This is ADDITIONAL to the camera jitter and helps reduce banding
    let ign_dither = ign(vec2f(pixel_coord), params.frame_index) - 0.5;

    // Apply small per-pixel jitter (much smaller than camera jitter)
//    let jittered_coord = pixel_center + ign_dither * 0.5;
//    let texcoords = jittered_coord / vec2f(output_dims);
    let texcoords = pixel_center / vec2f(output_dims);

    let origin = params.camera.position.xyz;
    let depth_dims = vec2i(textureDimensions(depth_texture, 0).xy);
    let base_depth_coord = vec2i(texcoords * vec2f(depth_dims));
    let max_depth_coord = vec2i(depth_dims) - vec2i(1);
    let d00 = textureLoad(depth_texture, clamp(base_depth_coord + vec2i(0, 0), vec2i(0), max_depth_coord), 0).x;
    let d10 = textureLoad(depth_texture, clamp(base_depth_coord + vec2i(1, 0), vec2i(0), max_depth_coord), 0).x;
    let d01 = textureLoad(depth_texture, clamp(base_depth_coord + vec2i(0, 1), vec2i(0), max_depth_coord), 0).x;
    let d11 = textureLoad(depth_texture, clamp(base_depth_coord + vec2i(1, 1), vec2i(0), max_depth_coord), 0).x;

    // Use farthest depth
    let farthest_depth = min(min(d00, d10), min(d01, d11));
    let frag_depth = max(farthest_depth, 1e-6f);
    let frag_pos = unproject(vec3f(texcoords * vec2f(2.0, -2.0) + vec2f(-1.0, 1.0), frag_depth));
    let ray_direction = normalize(frag_pos);

    // Define volume bounds
    let volume_min = vec3f(params.bounds_min.x, params.bounds_min.y, VOLUME_HEIGHT_MIN);
    let volume_max = vec3f(params.bounds_max.x, params.bounds_max.y, VOLUME_HEIGHT_MAX);

    // Intersect ray with volume
    let intersection = intersect_aabb(origin, ray_direction, volume_min, volume_max);
    let t_near = max(intersection.x, 0.0);
    let t_far = min(intersection.y, length(frag_pos));

    if (t_near >= t_far) {
        textureStore(output_color, pixel_coord, vec4f(0.0, 0.0, 0.0, 0.0));
        textureStore(output_depth, pixel_coord, vec4f(frag_depth, 0.0, 0.0, 0.0));
        return;
    }

    // Ray marching
    let sun_dir = normalize(SUN_DIRECTION);
    var accumulated_light = vec3f(0.0);
    var transmittance = 1.0;

    // Track apparent depth (first significant cloud intersection)
    var apparent_depth = frag_depth;
    var depth_written = false;

    // Use IGN dither for ray start offset to reduce banding
    var t = t_near; // + ign_dither * get_step_size(t_near) * 0.5;
    var steps = 0;

    // Phase function (view-dependent scattering)
    let cos_angle = dot(ray_direction, sun_dir);
    let phase = phase_function(cos_angle);

    // Adaptive LOD bias based on view angle
    let view_angle_factor = abs(ray_direction.z);

    // Apply extinction multiplier
    let extinction_coeff = BASE_EXTINCTION_COEFF * params.extinction_multiplier;

    var last_step_size = 0.0;

    while (transmittance > 0.01 && steps < MAX_STEPS) {
        let sample_t = min(t - last_step_size * ign_dither, t_far);
        let pos = origin + ray_direction * sample_t;
        let distance_to_sample = sample_t;

        // Get tile info for adaptive stepping and LOD
        let tile = get_tile_info_at_pos(pos);
        let dz = max(u32(zoom_max) - tile.zoom, 0u);

        // Dynamic step size based on distance
        let base_step_size = get_step_size(t);

        // Adaptive step size based on tile resolution and view angle
        let resolution_multiplier = f32(max(1u << (dz / 2u), 1u));
        let angle_multiplier = mix(1.0, 1.5, 1.0 - view_angle_factor);
        var step_size = base_step_size * resolution_multiplier * angle_multiplier;
        step_size = min(step_size, t_far - sample_t); // FIXME: This isn't correct / consistent

        if (step_size < 0.01) {
            break;
        }

        let lod = calculate_lod(step_size, tile.zoom, distance_to_sample, ray_direction);

        // Sample density
        let base_density = sample_volume(pos, lod);

        // TODO: Apply detail noise here
        let density = base_density;

        if (density > DENSITY_THRESHOLD) {
            // Write apparent depth at first significant cloud hit
            if (!depth_written && density > 0.05) {
                apparent_depth = world_to_depth(pos);
                depth_written = true;
            }

            // Light energy reaching this point from the sun
            let light_energy = sample_light_energy(pos, sun_dir, distance_to_sample, extinction_coeff);

            // Calculate in-scattering coefficient
            let scatter_coefficient = density * extinction_coeff * SCATTERING_ALBEDO;

            // Direct lighting: Sun light scattered towards camera
            let direct_light = SUN_COLOR * SUN_INTENSITY * light_energy * phase;

            // Improved ambient model
            let height_factor = (pos.z - VOLUME_HEIGHT_MIN) / (VOLUME_HEIGHT_MAX - VOLUME_HEIGHT_MIN);
            let density_factor = 1.0 - saturate(density * 2.0);
            let ambient_boost = mix(0.5, 1.5, density_factor);
            let ambient_factor = mix(0.4, 1.0, height_factor) * ambient_boost;
            let ambient_light = AMBIENT_COLOR * AMBIENT_INTENSITY * ambient_factor;

            // Total incoming light
            let incoming_light = direct_light + ambient_light;

            // Accumulate scattered light
            let scattered_light = incoming_light * scatter_coefficient * step_size;
            accumulated_light += scattered_light * transmittance;

            // Reduce transmittance by extinction
            let extinction = scatter_coefficient * step_size;
            transmittance *= exp(-extinction);
        } else {
            if (t >= t_far) {
                break;
            }
            // Take larger steps through empty space, but not too large to miss thin clouds
            t += step_size * 1.2;
            last_step_size = step_size * 1.2;
            steps += 1;
            continue;
        }

        if (t >= t_far) {
            break;
        }

        t += step_size;
        last_step_size = step_size;
        steps += 1;
    }

    let final_alpha = 1.0 - transmittance;

    // Output HDR result (no tonemapping)
    // Clamp to reasonable range to prevent fireflies in TAAU
    let clamped_light = min(accumulated_light, vec3f(100.0));
    textureStore(output_color, pixel_coord, vec4f(clamped_light, final_alpha));

    // Output apparent depth (linear depth in NDC Z)
    textureStore(output_depth, pixel_coord, vec4f(apparent_depth, 0.0, 0.0, 0.0));
}
