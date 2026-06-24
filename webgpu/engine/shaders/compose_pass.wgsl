/*****************************************************************************
* weBIGeo
* Copyright (C) 2022 Gerald Kimmersdorfer
* Copyright (C) 2024 Patrick Komon
* Copyright (C) 2026 Wendelin Muth
*
* This program is free software : you can redistribute it and / or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program. If not, see <http : //www.gnu.org/licenses/>.
*****************************************************************************/

///use util/shared_config
///use util/camera_config
///use webgpu::encoder
///use webgpu::general
///use webgpu::tile_util
///use webgpu_engine::sky/common/medium
///use webgpu_engine::sky/common/transmittance

@group(0) @binding(0) var<uniform> conf: shared_config;
@group(1) @binding(0) var<uniform> camera: camera_config;

@group(2) @binding(0) var albedo_texture: texture_2d<u32>;
@group(2) @binding(1) var position_texture: texture_2d<f32>;
@group(2) @binding(2) var normal_texture: texture_2d<u32>;
@group(2) @binding(3) var overlay_texture: texture_2d<u32>;

@group(2) @binding(4) var cloud_shadow_texture: texture_2d<f32>;
@group(2) @binding(5) var cloud_shadow_sampler: sampler;
@group(2) @binding(6) var depth_texture: texture_2d<f32>;
@group(2) @binding(7) var overlay_renderer_post_texture: texture_2d<f32>;
@group(2) @binding(8) var overlay_renderer_pre_texture: texture_2d<f32>;

@group(3) @binding(0) var output_color:          texture_storage_2d<rgba16float, write>;
@group(3) @binding(1) var<uniform> atmosphere:   Atmosphere;
@group(3) @binding(2) var transmittance_lut:     texture_2d<f32>;
@group(3) @binding(3) var transmittance_sampler: sampler;

const CLOUD_SHADOW_AABB_MIN = vec3f(1045658.54694121, 5811660.13457852, 0.0);
const CLOUD_SHADOW_AABB_MAX = vec3f(1937220.04485951, 6309418.06277159, 14000.0);

//Calculates the diffuse and specular illumination contribution for the given
//parameters according to the Blinn-Phong lighting model.
//All parameters must be normalized.
fn calc_blinn_phong_contribution(
    toLight: vec3<f32>,
    toEye: vec3<f32>,
    normal: vec3<f32>,
    diffFactor: vec3<f32>,
    specFactor: vec3<f32>,
    specShininess: f32
) -> vec3<f32> {
    let nDotL: f32 = max(0.0, dot(normal, toLight)); //Lambertian coefficient
    let h: vec3<f32> = normalize(toLight + toEye);
    let nDotH: f32 = max(0.0, dot(normal, h));
    let specPower: f32 = pow(nDotH, specShininess);
    let diffuse: vec3<f32> = diffFactor * nDotL; //Component-wise product
    let specular: vec3<f32> = specFactor * specPower;
    return diffuse + specular;
}

//Calculates the Blinn-Phong illumination for the given fragment
fn calculate_illumination(
    albedo: vec3<f32>,
    eyePos: vec3<f32>,
    fragPos: vec3<f32>,
    fragNorm: vec3<f32>,
    dirLight: vec4<f32>,
    ambLight: vec4<f32>,
    dirDirection: vec3<f32>,
    material: vec4<f32>,
    ao: f32,
    shadow_term: f32
) -> vec3<f32> {
    let dirColor: vec3<f32> = dirLight.rgb * dirLight.a;
    let ambColor: vec3<f32> = ambLight.rgb * ambLight.a;
    let ambient: vec3<f32> = material.r * albedo;
    let diff: vec3<f32> = material.g * albedo;
    let spec: vec3<f32> = vec3<f32>(material.b);
    let shini: f32 = material.a;

    let ambientIllumination: vec3<f32> = ambient * ambColor * ao;

    let toLightDirWS: vec3<f32> = -normalize(dirDirection);
    let toEyeNrmWS: vec3<f32> = normalize(eyePos - fragPos);
    let diffAndSpecIllumination: vec3<f32> = dirColor * calc_blinn_phong_contribution(toLightDirWS, toEyeNrmWS, fragNorm, diff, spec, shini);

    return ambientIllumination + diffAndSpecIllumination * (1.0 - shadow_term);
}

fn get_cloud_shadow_occlusion(world_pos: vec3f) -> f32 {
    const SHADOW_BIAS = 0.05;
    const ESM_CONSTANT = 4.0;

    let uv = vec2f(
        (world_pos.x - CLOUD_SHADOW_AABB_MIN.x) / (CLOUD_SHADOW_AABB_MAX.x - CLOUD_SHADOW_AABB_MIN.x),
        (CLOUD_SHADOW_AABB_MAX.y - world_pos.y) / (CLOUD_SHADOW_AABB_MAX.y - CLOUD_SHADOW_AABB_MIN.y)
    );

    let shadow_map_val = textureSampleLevel(cloud_shadow_texture, cloud_shadow_sampler, uv, 0.0).r;

    let height_adjusted = world_pos.z / cos(y_to_lat(world_pos.y));
    let h_receiver_norm = height_adjusted / CLOUD_SHADOW_AABB_MAX.z + SHADOW_BIAS;
    let receiver_val = exp(ESM_CONSTANT * h_receiver_norm);

    //factor from 0.0 (in shadow) to 1.0 (lit)
    let shadow_factor = clamp(receiver_val / shadow_map_val, 0.0, 1.0);

    if world_pos.x < CLOUD_SHADOW_AABB_MIN.x || world_pos.x > CLOUD_SHADOW_AABB_MAX.x ||
    world_pos.y < CLOUD_SHADOW_AABB_MIN.y || world_pos.y > CLOUD_SHADOW_AABB_MAX.y {
        return 0.0;
    }

    return 1.0 - shadow_factor;
}

@compute @workgroup_size(16, 16, 1)
fn computeMain(@builtin(global_invocation_id) gid: vec3u) {
    let dims = textureDimensions(output_color);
    if gid.x >= dims.x || gid.y >= dims.y { return; }
    let tci = gid.xy;

    var albedo: vec3f = unpack4x8unorm(textureLoad(albedo_texture, tci, 0).r).xyz;
    let pos_dist = textureLoad(position_texture, tci, 0);
    let encoded_normal = textureLoad(normal_texture, tci, 0).xy;

    let pos_cws = pos_dist.xyz;
    let dist = length(pos_cws);

    let normal = octNormalDecode2u16(encoded_normal);

    var amb_occlusion = 1.0;

    let origin = camera.position.xyz;
    let pos_ws = pos_cws + origin;

    var out_Color = vec4f(0.0);

    var cloud_shadow = 0.0;
    if bool(conf.clouds_enabled) {
        //must be called from uniform control flow :(
        let cloud_shadow_raw = get_cloud_shadow_occlusion(pos_ws);
        cloud_shadow = cloud_shadow_raw * cloud_shadow_raw * cloud_shadow_raw * cloud_shadow_raw;
    }

    if dist > 0.0 {
        //Apply material color by blending with albedo
        albedo = mix(albedo, conf.material_color.rgb, conf.material_color.a);

        var shadow_term = cloud_shadow;
        amb_occlusion *= 1.0 - cloud_shadow * 0.3;

        //Pre-shading overlay renderer output (applied to albedo before lighting)
        let pre_overlay_color = textureLoad(overlay_renderer_pre_texture, tci, 0);
        albedo = albedo * (1.0 - pre_overlay_color.a) + pre_overlay_color.rgb;

        // Atmosphere-derived sun light: physical reddening from transmittance LUT + horizon cutoff.
        let pos_atm      = pos_ws / 1000.0 - atmosphere.planet_center;
        let view_height  = length(pos_atm);
        let pos_atm_norm = pos_atm / view_height;
        let rho   = sqrt(max(0.0, view_height*view_height - atmosphere.bottom_radius*atmosphere.bottom_radius));
        let atm_h = sqrt(max(0.0, atmosphere.top_radius*atmosphere.top_radius - atmosphere.bottom_radius*atmosphere.bottom_radius));
        let cos_zenith_sun = dot(-normalize(conf.sun_light_dir.xyz), pos_atm_norm);
        var effective_sun_light = vec4f(0.0);
        if cos_zenith_sun > -rho / view_height {
            let atm_transmittance = lookup_transmittance(view_height, cos_zenith_sun, rho, atm_h);
            effective_sun_light = vec4f(atm_transmittance * conf.sun_light.a, 1.0);
        }

        var shaded_color = albedo;
        if bool(conf.shading_enabled) {
            shaded_color = calculate_illumination(shaded_color, origin, pos_ws, normal, effective_sun_light, conf.amb_light, conf.sun_light_dir.xyz, conf.material_light_response, amb_occlusion, shadow_term);
        }
        shaded_color = max(vec3(0.0), shaded_color);
        out_Color = vec4(shaded_color, 1.0);
    } else {
        // Black background — the LUT sky compute pass fills sky pixels on top of this
        out_Color = vec4(0.0, 0.0, 0.0, 1.0);
    }

    //Post-shading overlay renderer output
    let post_overlay_color = textureLoad(overlay_renderer_post_texture, tci, 0);
    out_Color = vec4f(out_Color.rgb * (1.0 - post_overlay_color.a) + post_overlay_color.rgb, out_Color.a);

    textureStore(output_color, gid.xy, out_Color);
}
