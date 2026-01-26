#include "util/camera_config.wgsl"
#include "util/tile_util.wgsl"
#include "screen_pass_vert.wgsl"

struct tile_info {
    index: u32,
    zoom: u32,
}

struct shader_params {
    bounds_min: vec4f,
    bounds_max: vec4f,
}

@group(0) @binding(0) var<uniform> camera : camera_config;

@group(1) @binding(0) var<uniform> params : shader_params;
@group(1) @binding(1) var cloud_texture: texture_3d<f32>;
@group(1) @binding(2) var cloud_sampler: sampler;
@group(1) @binding(3) var<storage, read> tile_infos: array<tile_info>;

@group(2) @binding(0) var depth_texture: texture_2d<f32>;

// tile size at zoom level 11
override tile_size_x = 19567.879241005121;
override tile_size_y = 19567.879241005121;
override inv_tile_size_x = 1.0 / 19567.879241005121;
override inv_tile_size_y = 1.0 / 19567.879241005121;

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
const MIN_STEP_SIZE = 50.0; // meters
const DENSITY_THRESHOLD = 0.00001;

// Lighting parameters
const SUN_DIRECTION = vec3f(0.5, 0.3, 0.7); // Normalized sun direction
const SUN_COLOR = vec3f(1.0, 0.95, 0.9) * 20.0;
const SUN_INTENSITY = 2.5;
const AMBIENT_COLOR = vec3f(0.5, 0.6, 0.7);
const AMBIENT_INTENSITY = 0.8;

// Scattering parameters
const SCATTERING_COEFF = 0.7; // Forward scattering (0 = isotropic, 0.8 = strong forward)
// const EXTINCTION_COEFF = 0.012; // How much density affects light per meter
const EXTINCTION_COEFF = 0.01;
const SCATTERING_ALBEDO = 0.99; // Proportion of light scattered vs absorbed (clouds ~0.99)

const LIGHT_STEP_SIZE = 150.0; // meters for light sampling
const MAX_LIGHT_STEPS = 8;

// Detail parameters
const DETAIL_SCALE = 1.0; // Scale for detail noise modulation
const DETAIL_STRENGTH = 10.0; // How much detail affects density
const VERTICAL_DETAIL_SCALE = 1.0; // More detail in vertical direction to break layers

fn unproject(normalised_device_coordinates: vec3f) -> vec3f {
    let unprojected = camera.inv_proj_matrix * vec4(normalised_device_coordinates, 1.0);
    let normalised_unprojected = unprojected / unprojected.w;
    return (camera.inv_view_matrix * normalised_unprojected).xyz;
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

    // LOD-aware edge margin to prevent bleeding
    // At LOD 0: margin for 256x256x128 texels
    // At higher LODs: margin scales with mip level
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

    return textureSampleLevel(cloud_texture, cloud_sampler, atlas_uvz, 0.0).r;
}

fn calculate_lod(step_size: f32, tile_zoom: u32) -> f32 {
    // Adaptive LOD based on step size and tile resolution
    let dz = max(u32(zoom_max) - tile_zoom, 0u);
    let base_texel_size = tile_size_x / 256.0; // Assuming 256x256 tile resolution
    let actual_texel_size = base_texel_size * f32(1u << dz);

    // Use higher mip levels when step size >> texel size
    let lod = log2(max(step_size / actual_texel_size, 1.0));
    return clamp(lod, 0.0, 4.0); // Assuming 4 mip levels
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
    // Multiple octaves of scattering for more realistic look
    let forward = henyey_greenstein(cos_angle, SCATTERING_COEFF);
    let backward = henyey_greenstein(cos_angle, -SCATTERING_COEFF * 0.5);
    let isotropic = 0.25 / 3.14159265; // Uniform scattering in all directions

    // Blend: forward scattering + backward + isotropic
    // Isotropic component ensures edges and thin clouds still get light
    return forward * 0.5 + backward * 0.2 + isotropic * 0.3;
}

// Multi-octave hash for better detail
fn hash13(p3: vec3f) -> f32 {
    var p = fract(p3 * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
}

fn detail_noise(pos: vec3f) -> f32 {
    // Multi-octave noise with extra vertical detail
    let p1 = pos * DETAIL_SCALE;
    let p2 = pos * DETAIL_SCALE * 2.3;
    let p3 = pos * vec3f(DETAIL_SCALE, DETAIL_SCALE, VERTICAL_DETAIL_SCALE);

    let n1 = hash13(p1);
    let n2 = hash13(p2) * 0.5;
    let n3 = hash13(p3) * 0.7; // Vertical detail to break layers

    return (n1 + n2 + n3) / 2.2;
}

// Calculate how much light reaches a point from the sun (light transmittance)
fn sample_light_energy(pos: vec3f, sun_dir: vec3f, lod_bias: f32) -> f32 {
    var optical_depth = 0.0;
    var total_weight = 0.0;

    // March towards sun to accumulate density
    for (var i = 1; i <= MAX_LIGHT_STEPS; i++) {
        let step_pos = pos + sun_dir * f32(i) * LIGHT_STEP_SIZE;

        // Quick bounds check
        if (step_pos.z < VOLUME_HEIGHT_MIN || step_pos.z > VOLUME_HEIGHT_MAX) {
            break;
        }

        let tile = get_tile_info_at_pos(step_pos);
        let lod = calculate_lod(LIGHT_STEP_SIZE, tile.zoom) + lod_bias;
        let base_density = sample_volume(step_pos, lod);

        // Add detail to light samples too
        // let detail = detail_noise(step_pos);
        let density = base_density; // * (0.5 + 0.5 * detail);

        optical_depth += density * LIGHT_STEP_SIZE * EXTINCTION_COEFF;
        total_weight += 1.0;

        // Early exit for deep shadows
        if (optical_depth > 10.0) {
            return 0.0;
        }
    }

    // Beer's law
    let transmittance = exp(-optical_depth);

    // Energy conservation: even in shadow, some light gets through via multi-scattering
    // This prevents edges from going completely black
    let multi_scatter = 0.3 * exp(-optical_depth * 0.25);

    return transmittance + multi_scatter;
}

// Simple Reinhard tonemapping
fn tonemap_reinhard(color: vec3f) -> vec3f {
    return color / (1.0 + color);
}

@fragment
fn fragmentMain(vertex_out : VertexOut) -> @location(0) vec4f {
    let origin = camera.position.xyz;
    let depth_texcoord = vec2u(vertex_out.texcoords * vec2f(textureDimensions(depth_texture, 0).xy));
    let frag_depth = max(textureLoad(depth_texture, depth_texcoord, 0).x, 1e-6f);
    let frag_pos = unproject(vec3f(vertex_out.texcoords * vec2f(2.0, -2.0) + vec2f(-1.0, 1.0), frag_depth));
    let ray_direction = normalize(frag_pos);

    // Define volume bounds
    let volume_min = vec3f(params.bounds_min.x, params.bounds_min.y, VOLUME_HEIGHT_MIN);
    let volume_max = vec3f(params.bounds_max.x, params.bounds_max.y, VOLUME_HEIGHT_MAX);

    // Intersect ray with volume
    let intersection = intersect_aabb(origin, ray_direction, volume_min, volume_max);
    let t_near = max(intersection.x, 0.0);
    let t_far = min(intersection.y, length(frag_pos));

    if (t_near >= t_far) {
        discard;
    }

    // Ray marching
    let sun_dir = normalize(SUN_DIRECTION);
    var accumulated_light = vec3f(0.0);
    var transmittance = 1.0;

    let ray_length = t_far - t_near;
    let base_step_size = max(MIN_STEP_SIZE, ray_length / f32(MAX_STEPS));

    // Blue noise dithering to break up banding
    let dither = hash13(vec3f(vertex_out.texcoords * 1000.0, camera.position.x * 0.1));
    var t = t_near + dither * base_step_size * 0.5;
    var steps = 0;

    // Phase function (view-dependent scattering)
    let cos_angle = dot(ray_direction, sun_dir);
    let phase = phase_function(cos_angle);

    // Adaptive LOD bias based on view angle (reduces flickering)
    let view_angle_factor = abs(ray_direction.z);
    let distance_to_cloud = t_near;
    let base_lod_bias = log2(max(distance_to_cloud / 5000.0, 1.0)) * 0.3;

    while (t < t_far && transmittance > 0.01 && steps < MAX_STEPS) {
        let pos = origin + ray_direction * t;

        // Get tile info for adaptive stepping and LOD
        let tile = get_tile_info_at_pos(pos);
        let dz = max(u32(zoom_max) - tile.zoom, 0u);

        // Adaptive step size based on tile resolution and view angle
        let resolution_multiplier = f32(max(1u << (dz / 2u), 1u));
        let angle_multiplier = mix(1.0, 2.0, 1.0 - view_angle_factor); // Larger steps for grazing angles
        let step_size = base_step_size * resolution_multiplier * angle_multiplier;

        // LOD selection with bias to reduce flickering
        let lod = calculate_lod(step_size, tile.zoom) + base_lod_bias;

        // Sample density
        let base_density = sample_volume(pos, lod);

        // Add multi-octave detail with strong vertical component to break layers
        // let detail = detail_noise(pos);
        let density = base_density; // * max(0.1, detail); // Never go below 10% to prevent holes

        if (density > DENSITY_THRESHOLD) {
            // Light energy reaching this point from the sun
            let light_energy = sample_light_energy(pos, sun_dir, base_lod_bias);

            // Calculate in-scattering coefficient
            let scatter_coefficient = density * EXTINCTION_COEFF * SCATTERING_ALBEDO;

            // Direct lighting: Sun light scattered towards camera
            let direct_light = SUN_COLOR * SUN_INTENSITY * light_energy * phase;

            // Improved ambient model
            // Height factor: more sky light reaches upper parts
            let height_factor = (pos.z - VOLUME_HEIGHT_MIN) / (VOLUME_HEIGHT_MAX - VOLUME_HEIGHT_MIN);

            // Density-based ambient: thin clouds get more ambient, thick clouds less
            // This is key to preventing dark edges on thin clouds
            let density_factor = 1.0 - saturate(density * 2.0);
            let ambient_boost = mix(0.5, 1.5, density_factor);

            // Combine height and density effects
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
            // Take larger steps through empty space
            t += step_size * 1.0;
            steps += 1;
            continue;
        }

        t += step_size;
        steps += 1;
    }

    let final_alpha = 1.0 - transmittance;

    if (final_alpha < 0.01) {
        discard;
    }

    // Apply tonemapping
    let tonemapped = tonemap_reinhard(accumulated_light);

    return vec4f(tonemapped, final_alpha);
}