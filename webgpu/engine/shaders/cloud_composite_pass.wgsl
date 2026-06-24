/*****************************************************************************
* weBIGeo
* Copyright (C) 2026 Gerald Kimmersdorfer
*
* This program is free software: you can redistribute it and / or modify
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

// Blends the upscaled cloud layer over the LUT-sky render target.
// Runs after the sky compute pass so clouds composite on top of the atmosphere
// rather than receiving atmospheric haze computed for terrain behind them.
//
// sky_result:   full-res RGBA16F output of the sky LUT compute pass
// cloud_color:  full-res RGBA16F upscaled cloud color (rgb=premultiplied radiance, a=transmittance)
// composite_out: write target, same format/size as sky_result

@group(0) @binding(0) var sky_result:     texture_2d<f32>;
@group(0) @binding(1) var cloud_color:    texture_2d<f32>;
@group(0) @binding(2) var composite_out:  texture_storage_2d<rgba16float, write>;

@compute @workgroup_size(16, 16, 1)
fn computeMain(@builtin(global_invocation_id) gid: vec3<u32>) {
    let size = vec2<u32>(textureDimensions(composite_out));
    if gid.x >= size.x || gid.y >= size.y { return; }

    let sky   = textureLoad(sky_result,  gid.xy, 0);
    let cloud = textureLoad(cloud_color, gid.xy, 0);

    // cloud.a = transmittance; blend_alpha = 1 - transmittance (same convention as compose_pass)
    let blend_alpha = 1.0 - cloud.a;
    let safe_alpha  = max(blend_alpha, 0.00001);
    let straight_rgb = cloud.rgb / safe_alpha;
    let tonemapped   = straight_rgb / (straight_rgb + 1.0); // Reinhard, matching compose_pass

    let out_rgb = sky.rgb * (1.0 - blend_alpha) + tonemapped * blend_alpha;
    textureStore(composite_out, gid.xy, vec4<f32>(out_rgb, sky.a));
}
