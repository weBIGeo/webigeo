/*****************************************************************************
* weBIGeo
* Copyright (C) 2026 Gerald Kimmersdorfer
*
* This program is free software : you can redistribute it and / or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY ; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program. If not, see <http : //www.gnu.org/licenses/>.
*****************************************************************************/

///use util/camera_config
///use util/shared_config
///use webgpu::position_util
///use webgpu::encoder
///use webgpu::normals_util

@group(0) @binding(0) var overlay_texture:  texture_2d<u32>;        // GBuffer slot 3 (packed RGBA via pack4x8unorm)
@group(0) @binding(1) var<uniform> settings: TileDebugSettings;
@group(0) @binding(2) var output_texture:   texture_storage_2d<rgba8unorm, write>;
@group(0) @binding(3) var prev_output:      texture_2d<f32>;
@group(0) @binding(4) var position_texture: texture_2d<f32>;         // GBuffer slot 1: xyz=pos_cws, w=render tile zoom
@group(0) @binding(5) var depth_texture:    texture_depth_2d;        // GBuffer depth
@group(0) @binding(6) var normal_texture:   texture_2d<u32>;         // GBuffer slot 2: oct-encoded true terrain normal

@group(1) @binding(0) var<uniform> camera: camera_config;
@group(2) @binding(0) var<uniform> conf: shared_config;

struct TileDebugSettings {
    strength: f32,
    scale: f32,
    mode: u32,
    _pad: u32,
    x_region: vec2f,
    _pad2: vec2f,
}


@compute @workgroup_size(16, 16, 1)
fn computeMain(@builtin(global_invocation_id) id: vec3u) {
    let dims = vec2u(textureDimensions(output_texture));
    if id.x >= dims.x || id.y >= dims.y {
        return;
    }
    let tci = id.xy;

    // Region filter: pixels outside x_region pass through transparently.
    let norm_x = f32(tci.x) / f32(dims.x);
    if norm_x < settings.x_region.x || norm_x >= settings.x_region.y {
        textureStore(output_texture, tci, textureLoad(prev_output, tci, 0));
        return;
    }

    //render_tiles.wgsl writes alpha = 1 on geometry, 0 (transparent) on background.
    let packed = textureLoad(overlay_texture, tci, 0).r;
    var overlay_color = unpack4x8unorm(packed);

    // For GBuffer-sourced modes, replace rgb from the raw float data before alpha-scaling.
    let is_geometry = overlay_color.a > 0.0;
    if is_geometry {
        if settings.mode == 5u {
            // Position buffer XYZ (camera-relative world space), scaled by user-controlled divisor.
            let pos = textureLoad(position_texture, tci, 0).xyz;
            overlay_color = vec4f(pos / settings.scale, 1.0);
        } else if settings.mode == 6u {
            // Camera distance computed as length(pos_cws), normalized by scale.
            let cam_dist = length(textureLoad(position_texture, tci, 0).xyz) / settings.scale;
            overlay_color = vec4f(cam_dist, cam_dist, cam_dist, 1.0);
        } else if settings.mode == 7u {
            // Raw (non-linear) clip-space depth from the depth buffer, in [0, 1].
            let raw_depth = textureLoad(depth_texture, tci, 0);
            overlay_color = vec4f(raw_depth, raw_depth, raw_depth, 1.0);
        } else if settings.mode == 8u {
            // Linearized view-space depth (meters), normalized by scale for visibility.
            let raw_depth = textureLoad(depth_texture, tci, 0);
            let ndc = vec4f(0.0, 0.0, raw_depth, 1.0);
            let view = camera.inv_proj_matrix * ndc;
            let lin_depth = abs(view.z / view.w) / settings.scale;
            overlay_color = vec4f(lin_depth, lin_depth, lin_depth, 1.0);
        } else if settings.mode == 9u {
            // True 3D camera distance reconstructed from depth buffer via inv_view_proj_matrix.
            let raw_depth = textureLoad(depth_texture, tci, 0);
            let dist = length(camera_relative_pos_from_depth(tci, dims, raw_depth, camera.inv_view_proj_matrix)) / settings.scale;
            overlay_color = vec4f(dist, dist, dist, 1.0);
        } else if settings.mode == 10u {
            // Camera-relative XYZ position reconstructed from depth buffer, shown as RGB.
            let raw_depth = textureLoad(depth_texture, tci, 0);
            let pos = camera_relative_pos_from_depth(tci, dims, raw_depth, camera.inv_view_proj_matrix) / settings.scale;
            overlay_color = vec4f(pos, 1.0);
        } else if settings.mode == 11u {
            // Absolute difference between position buffer XYZ and depth-reprojected position.
            let raw_depth = textureLoad(depth_texture, tci, 0);
            let pos_buffer = textureLoad(position_texture, tci, 0).xyz;
            let pos_reproj = camera_relative_pos_from_depth(tci, dims, raw_depth, camera.inv_view_proj_matrix);
            let diff = abs(pos_buffer - pos_reproj) / settings.scale;
            overlay_color = vec4f(diff, 1.0);
        } else if settings.mode == 12u {
            // Shading normal: stored true terrain normal tilted by the earth-curvature deformation
            // (same value compose_pass uses for lighting), shown as RGB via *0.5 + 0.5.
            let normal = octNormalDecode2u16(textureLoad(normal_texture, tci, 0).xy);
            let pos_cws = textureLoad(position_texture, tci, 0).xyz;
            let shading_normal = curvature_corrected_normal(normal, pos_cws.xy, conf.planet_radius_m);
            overlay_color = vec4f(shading_normal * 0.5 + 0.5, 1.0);
        }
    }

    overlay_color.a = overlay_color.a * settings.strength;

    //Blend over previous overlay in premultiplied alpha space:
    let prev = textureLoad(prev_output, tci, 0);
    let src_premul = vec4f(overlay_color.rgb * overlay_color.a, overlay_color.a);
    let blended = src_premul + prev * (1.0 - overlay_color.a);
    textureStore(output_texture, tci, blended);
}
