#include "util/tile_util.wgsl"
#include "util/shared_config.wgsl"
#include "util/atmosphere.wgsl"

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
    scattering_coeff: f32,
    extinction_coeff: f32,
    albedo: f32,

    step_size_min: f32,
    step_size_distance_factor: f32,
    step_size_horizon_factor: f32,
    fade_factor: f32,

    sun_light_scale: f32,
    ambient_light_scale: f32,
    jitter: vec2f,
}

@group(0) @binding(0) var<uniform> params : shader_params;
@group(0) @binding(1) var atlas_texture: texture_3d<f32>;
@group(0) @binding(2) var atlas_sampler: sampler;
@group(0) @binding(3) var<storage, read> tile_infos: array<tile_info>;
@group(0) @binding(4) var output_color: texture_storage_2d<rgba16float, write>;
@group(0) @binding(5) var output_depth: texture_storage_2d<r32float, write>;

@group(1) @binding(0) var depth_texture: texture_2d<f32>;

@group(2) @binding(0) var<uniform> sconf : shared_config;

// tile size at zoom level 10
override tile_size_xy = 39135.7584820102;
override inv_tile_size_xy = 1.0 / 39135.7584820102;

override tile_count_x = 46/2;
override tile_count_y = 26/2;

override zoom_max = 10;

override tile_coords_offset_x = 538;
override tile_coords_offset_y = 660;

override atlas_bits_xy = 2u;
override atlas_bits_z = 4u;

#define ATLAS_MASK_XY u32((1<<atlas_bits_xy)-1)
#define ATLAS_MASK_Z u32((1<<atlas_bits_z)-1)
#define ATLAS_INV_SCALE vec3(1.0 / f32(1<<atlas_bits_xy), 1.0 / f32(1<<atlas_bits_xy), 1.0 / f32(1<<atlas_bits_z))

// Texture dimensions
const TILE_RESOLUTION_XY = 256.0;
const INV_TILE_RESOLUTION_XY = 1.0 / TILE_RESOLUTION_XY;
const TILE_RESOLUTION_Z = 128.0;
const INV_TILE_RESOLUTION_Z = 1.0 / TILE_RESOLUTION_Z;
// Note: Using a constant 22000 here is correct
const HEIGHT_PER_TEXEL = 22500.0 / TILE_RESOLUTION_Z;
const INV_HEIGHT_PER_TEXEL = 1.0 / HEIGHT_PER_TEXEL;
const TEXTURE_VALUE_SCALE = 64.0; // Has to match generation script

// Ray marching parameters
const MAX_STEPS = 128;
const DENSITY_THRESHOLD = 0.00001;

const MAX_LIGHT_STEPS = 8;

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

fn get_tile_id_at_pos(pos_world: vec3f) -> vec2i {
    // Equivalent to subtracting bounds_min, dividing and then flooring,
    // but avoids floating point mismatch to tile_uv calculation.
    let pos_ts = pos_world.xy * inv_tile_size_xy;
    return vec2i(floor(pos_ts)) - vec2i(tile_coords_offset_x, tile_coords_offset_y);
}

fn get_tile_info(tile_id: vec2i) -> tile_info {
    if(tile_id.x < 0 || tile_id.x >= tile_count_x || tile_id.y < 0 || tile_id.y >= tile_count_y) {
        var default_tile: tile_info;
        default_tile.index = 0u;
        default_tile.zoom = u32(0u);
        return default_tile;
    }

    let tile_index = tile_id.x + tile_id.y * tile_count_x;
    return tile_infos[tile_index];
}

fn sample_volume(pos_world: vec3f, lod: f32, tile_id: vec2i, tile: tile_info) -> f32 {
    if (pos_world.z < 0.0 || pos_world.z > params.bounds_max.z || tile.zoom == 0u) {
        return 0.0;
    }

    let dz = max(u32(zoom_max) - tile.zoom, 0u);
    let tile_scale = f32(1u << dz);

    let pos_ts = pos_world.xy * inv_tile_size_xy / tile_scale;
    let tile_uv = fract(pos_ts);

    let atlas_pos = vec3u(
        tile.index & ATLAS_MASK_XY,
        (tile.index >> atlas_bits_xy) & ATLAS_MASK_XY,
        (tile.index >> (atlas_bits_xy << 1u)) & ATLAS_MASK_Z
    );

    // This projects into texture space height which is 0-14000.
    let height_adjusted = pos_world.z * cos(y_to_lat(pos_world.y));
    let height_normalized = height_adjusted * INV_HEIGHT_PER_TEXEL * INV_TILE_RESOLUTION_Z;

    let mip_scale = exp2(lod);
    let texel_size = mip_scale * vec3f(INV_TILE_RESOLUTION_XY, INV_TILE_RESOLUTION_XY, INV_TILE_RESOLUTION_Z);
    let safe_uvw = clamp(vec3f(tile_uv, height_normalized), texel_size, vec3f(1.0) - texel_size);
    let atlas_uvw = (safe_uvw + vec3f(atlas_pos)) * ATLAS_INV_SCALE;

    let raw = textureSampleLevel(atlas_texture, atlas_sampler, atlas_uvw, lod).r;
    return raw * TEXTURE_VALUE_SCALE * 0.001;
}

fn calculate_lod(step_size: f32, tile_zoom: u32, distance: f32, view_dir: vec3f) -> f32 {
    let dz = max(u32(zoom_max) - tile_zoom, 0u);
    let tile_size_multiplier = f32(1u << dz);

    // Texel sizes in world space
    let texel_size_xy = (tile_size_xy * tile_size_multiplier) / TILE_RESOLUTION_XY;
    let texel_size_z = HEIGHT_PER_TEXEL;

    // View direction analysis
    let view_xy = length(view_dir.xy);
    let view_z = abs(view_dir.z);

    // Project texels to screen space
    // Account for perspective: texel_size / distance ≈ pixel_angular_size
    let distance_clamped = max(distance, 1.0);

    // Angular size of texels from viewer's perspective
    let angular_size_xy = texel_size_xy / distance_clamped;
    let angular_size_z = texel_size_z / distance_clamped;

    // Weight by view direction to find dominant texel size
    // When looking horizontally, vertical resolution matters most
    // When looking down, horizontal resolution matters most
    let weighted_texel_size = (angular_size_xy * view_xy + angular_size_z * view_z);

    // Step coverage in angular space
    let step_angular_size = step_size / distance_clamped;

    // How many texels does each step cover?
    let texels_per_step = step_angular_size / max(weighted_texel_size, 1e-8);

    // LOD selection:
    // - texels_per_step > 2: we're oversampling, can use coarser mip
    // - texels_per_step < 1: we're undersampling, use finest detail
    //
    // Critical: With only 64 vertical slices, losing detail is very visible
    // Conservative multiplier keeps us at finer LODs
    let lod_raw = log2(max(texels_per_step / 2.0, 1.0));

    // Apply conservative bias - only go coarse when really justified
    // Vertical resolution is precious, so we stay fine
    let conservative_bias = -0.5;  // Bias toward finer detail
    let lod = lod_raw + conservative_bias;

    // Clamp to valid range (0 = finest, 5 = coarsest)
    return clamp(lod, 0.0, 5.0);
}


// Saturate helper
fn saturate(x: f32) -> f32 {
    return clamp(x, 0.0, 1.0);
}

// Henyey-Greenstein phase function for anisotropic scattering
fn henyey_greenstein_phase(cos_angle: f32, g: f32) -> f32 {
    let g2 = g * g;
    let denom = 1.0 + g2 - 2.0 * g * cos_angle;
    return (1.0 - g2) / (4.0 * 3.14159265 * pow(denom, 1.5));
}

// Improved phase function with better forward and back scattering
fn cloud_phase_function(cos_angle: f32) -> f32 {
    let forward = henyey_greenstein_phase(cos_angle, params.scattering_coeff);
    let backward = henyey_greenstein_phase(cos_angle, -params.scattering_coeff * 0.5);
    let isotropic = 0.25 / 3.14159265;
    return forward * 0.5 + backward * 0.2 + isotropic * 0.3;
}

// Calculate how much light reaches a point from the sun (light transmittance)
// Uses cone-based sampling with decreasing LOD as per Nubis/Guerrilla Games approach
fn sample_light_energy(pos: vec3f, sun_dir: vec3f, distance: f32, extinction_coeff: f32, base_lod: f32) -> f32 {
    if (sun_dir.z <= 0.0) {
        return 0.0;
    }

    if (pos.z >= params.bounds_max.z) {
        return 1.0;
    }

    // Calculate maximum ray length to volume boundary
    let max_ray_length = min((params.bounds_max.z - pos.z) / sun_dir.z, 10000.0);
    var step_size = 0.5 * max_ray_length / f32(MAX_LIGHT_STEPS);

    var optical_depth = 0.0;
    var current_distance = step_size * 1.0;

    // March towards sun with decreasing LOD per Nubis approach
    // The decreasing LOD smooths out artifacts from sparse sampling
    for (var i = 0; i < MAX_LIGHT_STEPS; i++) {
        let t = current_distance;
        let sample_pos = pos + sun_dir * t;

        // Get tile info for this sample
        let tile_id = get_tile_id_at_pos(sample_pos);
        let tile = get_tile_info(tile_id);
        let sample_distance = distance + t;

        // Calculate base LOD, then add increasing bias per step
        // This decreasing detail level smooths transitions between samples
        let lod_bias = max(f32(i) * 0.5, 0.0);  // Increase LOD by 0.5 per step
        let lod = base_lod + lod_bias;

        // Sample density with calculated LOD
        let density = sample_volume(sample_pos, lod, tile_id, tile);

        // Accumulate optical depth
        optical_depth += density * step_size * extinction_coeff;

        // Early exit for deep shadows (GPU optimization - avoids unnecessary samples)
        if (optical_depth > 10.0) {
            return 0.0;
        }

        current_distance += step_size;
        step_size *= 1.5;
    }

    // Beer's law for transmittance
    let transmittance = exp(-optical_depth);

    // Multi-scattering approximation for energy conservation
    // Even in shadow, some light gets through via multiple scattering events
    let multi_scatter = 0.3 * exp(-optical_depth * 0.25);

    return transmittance + multi_scatter;
}

// Dynamic step size based on distance
fn get_step_size(distance: f32, ray_direction: vec3f) -> f32 {
    // 0.0 - 1.0 toward the horizon
    let horizon = max((1.0+ray_direction.z), 0.0);
    // Alternative
//    var h = max(asin(horizon) / 1.5708, 0.0);
//    h *= h;
//    let horizon_bonus = h * params.step_size_horizon_factor;
    let horizon_bonus = max(horizon * 2.0 - 1.0, 0.0) * params.step_size_horizon_factor;
    return max(distance * params.step_size_distance_factor, params.step_size_min) + horizon_bonus;
}

// Convert world position to NDC depth for storage
fn world_to_depth(world_pos: vec3f) -> f32 {
    let view_pos = params.camera.view_matrix * vec4f(world_pos, 1.0);
    let clip_pos = params.camera.proj_matrix * view_pos;
    let ndc = clip_pos.xyz / clip_pos.w;
    return ndc.z;
}

fn ign(pixel: vec2f) -> f32 {
    return fract(52.9829189f * fract(dot(pixel, vec2f(0.06711056f, 0.00583715f))));
}

fn r1_sequence(n: u32) -> f32 {
    let g = 1.6180339887498948482;
    let a1 = 1.0 / g;
    return fract(0.5+a1*f32(n));
}

// Combined spatial + temporal noise
fn get_ray_offset(pixel: vec2u, frame: u32) -> f32 {
    let temporal = r1_sequence(frame);
    let spatial = ign(vec2f(pixel));
    return fract(spatial + temporal);
}

@compute @workgroup_size(8, 8, 1)
fn computeMain(@builtin(global_invocation_id) global_id: vec3u) {
    let output_dims = textureDimensions(output_color);
    let pixel_coord = global_id.xy;

    if (pixel_coord.x >= output_dims.x || pixel_coord.y >= output_dims.y) {
        return;
    }

    let pixel_center = vec2f(pixel_coord) + 0.5;
    let texcoords = pixel_center / vec2f(output_dims);

    let ray_jitter = get_ray_offset(pixel_coord, params.frame_index);

    let origin = params.camera.position.xyz;
    let stable_depth_coord = 2 * vec2i(pixel_coord) - vec2i(2.0 * params.jitter);

    let frag_depth = max(textureLoad(depth_texture, stable_depth_coord, 0).x, 1e-6f);
    let frag_pos = unproject(vec3f(texcoords * vec2f(2.0, -2.0) + vec2f(-1.0, 1.0), frag_depth));
    let ray_direction = normalize(frag_pos);

    let fade_near =  1000.0 * params.fade_factor;
    let fade_far = 100000.0 * params.fade_factor;

    // Intersect ray with volume
    let intersection = intersect_aabb(origin, ray_direction, params.bounds_min.xyz, params.bounds_max.xyz);
    let t_near = max(intersection.x, fade_near);
    let t_far = min(intersection.y, length(frag_pos));

    if (t_near >= t_far) {
        textureStore(output_color, pixel_coord, vec4f(0.0, 0.0, 0.0, 1.0));
        textureStore(output_depth, pixel_coord, vec4f(0.0, 0.0, 0.0, 0.0));
        return;
    }

    // Ray marching
    let sun_dir = normalize(-sconf.sun_light_dir.xyz);
    var accu_light = vec3f(0.0);
    var accu_transmittance = 1.0;

    var accu_depth = 0.0;
    var accu_depth_weight = 0.0;

    var t = t_near;
    var steps = 0;

    // Phase function (view-dependent scattering)
    let cos_angle = dot(ray_direction, sun_dir);
    let cloud_phase = cloud_phase_function(cos_angle);

    // Adaptive LOD bias based on view angle
    let view_angle_factor = abs(ray_direction.z);

    var last_step_size = 0.0;
    while (accu_transmittance > 0.01 && steps < MAX_STEPS) {
        let sample_t = min(t - last_step_size * ray_jitter, t_far);
        let pos = origin + ray_direction * sample_t;
        let distance_to_sample = sample_t;

        let tile_id = get_tile_id_at_pos(pos);
        let tile = get_tile_info(tile_id);
        let dz = max(u32(zoom_max) - tile.zoom, 0u);

        let base_step_size = get_step_size(t-t_near, ray_direction);

        let resolution_multiplier = 2.0;
        let angle_multiplier = mix(1.0, 1.5, 1.0 - view_angle_factor);
        var step_size = base_step_size * 2.0;
        step_size = min(step_size, t_far - sample_t);

        if (step_size < 0.01) {
            break;
        }

        let lod = calculate_lod(step_size, tile.zoom, distance_to_sample, ray_direction);
        let base_density = sample_volume(pos, lod, tile_id, tile);

        let dist_cylinder = max(length(pos.xy - origin.xy), (origin.z-pos.z)*0.5);
        let fade_t = saturate((dist_cylinder-fade_near)/(fade_far-fade_near));
        let fade = fade_t * fade_t;

        let density = base_density * fade;

        if (density > DENSITY_THRESHOLD) {
            // Cloud optical properties
            let cloud_extinction = density * params.extinction_coeff;
            let cloud_scattering = cloud_extinction * params.albedo;

            // Sunlight transmittance through clouds
            let cloud_sun_transmittance = sample_light_energy(pos, sun_dir, distance_to_sample,
                                                               params.extinction_coeff, lod);

            // Cloud direct sun in-scattering
            let sun_radiance = sconf.sun_light.rgb * sconf.sun_light.a * params.sun_light_scale;
            let cloud_sun_inscatter = sun_radiance * cloud_sun_transmittance * cloud_phase;

            // Cloud ambient in-scattering
            let height_factor = pos.z / params.bounds_max.z;
            let density_factor = 1.0 - saturate(density * 2.0);
            let ambient_boost = mix(0.5, 1.5, density_factor);
            let ambient_occlusion = mix(0.4, 1.0, height_factor) * ambient_boost;
            let ambient_radiance = sconf.amb_light.rgb * sconf.amb_light.a * params.ambient_light_scale;
            let cloud_ambient_inscatter = ambient_radiance * ambient_occlusion;

            // Atmospheric light (acts as additional ambient source for clouds)
            var atm_ambient_light = vec3f(0.0);
            if (bool(sconf.atmosphere_enabled)) {
                let pos_km = pos / 1000.0;

                let air_density = density_at_height(pos_km.z);
                let rayleigh_coeff = scattering_coefficients();
                let atm_scattering = air_density * rayleigh_coeff;
                let atm_inscatter_density = atmospheric_inscatter_at_point(pos_km, sun_dir);

                // This is light scattered by atmosphere that can illuminate the cloud
                // NOT direct atmospheric scattering toward camera
                atm_ambient_light = sun_radiance * atm_inscatter_density * atm_scattering;
            }

            // Total incoming light at cloud particle (sun + ambient + atmospheric ambient)
            let cloud_total_inscatter = cloud_sun_inscatter + cloud_ambient_inscatter + atm_ambient_light;
            let cloud_contribution = cloud_total_inscatter * cloud_scattering * step_size;

            // Accumulate (atmospheric light is re-scattered by cloud, not directly accumulated)
            accu_light += cloud_contribution * accu_transmittance;

            // Depth accumulation
            accu_depth += t * accu_transmittance;
            accu_depth_weight += accu_transmittance;

            // Cloud extinction only
            let cloud_optical_depth = cloud_extinction * step_size;
            accu_transmittance *= exp(-cloud_optical_depth);

            t += step_size;
            last_step_size = step_size;
        } else {
            t += step_size * 1.5;
            last_step_size = step_size * 1.5;
        }

        if (t >= t_far) {
            break;
        }

        steps += 1;
    }

    // Note: accumulated_light is already "alpha-premultiplied" in a sense
    textureStore(output_color, pixel_coord, vec4f(accu_light, accu_transmittance));

    // Output apparent depth (linear depth in NDC Z)
    var apparent_depth = min(accu_depth / accu_depth_weight, t_far);
    if(accu_depth_weight == 0.0) {
        apparent_depth = 0.0;
    }
    textureStore(output_depth, pixel_coord, vec4f(apparent_depth, 0.0, 0.0, 0.0));
}
