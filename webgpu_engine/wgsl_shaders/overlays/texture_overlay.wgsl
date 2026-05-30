/*****************************************************************************
 * weBIGeo
 * Copyright (C) 2026 Gerald Kimmersdorfer
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

#include "util/shared_config.wgsl"
#include "util/camera_config.wgsl"

@group(0) @binding(0) var<uniform> conf: shared_config;
@group(1) @binding(0) var<uniform> camera: camera_config;
@group(2) @binding(0) var position_texture: texture_2d<f32>;
@group(2) @binding(1) var<uniform> settings: TextureOverlaySettings;
@group(2) @binding(2) var overlay_texture: texture_2d<f32>;
@group(2) @binding(3) var overlay_sampler: sampler;
@group(2) @binding(4) var output_texture: texture_storage_2d<r32uint, write>;

struct TextureOverlaySettings {
    aabb_min:  vec2f,
    aabb_size: vec2f, // aabb_max - aabb_min, precomputed in double precision on CPU
}

@compute @workgroup_size(16, 16, 1)
fn computeMain(@builtin(global_invocation_id) id: vec3u) {
    let dims = vec2u(textureDimensions(position_texture));
    if (id.x >= dims.x || id.y >= dims.y) {
        return;
    }
    let tci = id.xy;

    let pos_dist = textureLoad(position_texture, tci, 0);
    let pos_cws = pos_dist.xyz;
    let dist = length(pos_cws);
    let pos_ws = pos_cws + camera.position.xyz;

    var out_color = vec4f(0.0);

    let aabb_max = settings.aabb_min + settings.aabb_size;
    if (dist > 0.0 && all(pos_ws.xy >= settings.aabb_min) && all(pos_ws.xy <= aabb_max)) {
        let uv_raw = (pos_ws.xy - settings.aabb_min) / settings.aabb_size;
        let uv = vec2f(uv_raw.x, 1.0 - uv_raw.y);

        // NOTE: Mip level is estimated analytically: (world meters per screen pixel) / (world meters per texel).
        // pixel_world ~ dist / viewport_width. It assumes a perspective projection and ignores FOV and terrain slope.
        // This is less accurate than the old fragment-shader approach in compose_pass.wgsl, which uses dpdx/dpdy
        // on the UV coordinates to get exact per-pixel gradients including terrain angle and projection distortion.
        // dpdx/dpdy are not available in compute shaders and have no equivalent in compute.
        // The approximation only captures distance-based mip variation; oblique viewing angles are ignored.
        let texel_world = settings.aabb_size / vec2f(textureDimensions(overlay_texture));
        let pixel_world = dist / camera.viewport_size.x;
        let mip = max(0.0, log2(max(pixel_world / texel_world.x, pixel_world / texel_world.y)));

        // 0 = with mip, 1 = no mip (mip 0), 2 = visualize mip level
        const MODE: u32 = 0u;
        if (MODE == 0u) {
            out_color = textureSampleLevel(overlay_texture, overlay_sampler, uv, mip);
        } else if (MODE == 1u) {
            out_color = textureSampleLevel(overlay_texture, overlay_sampler, uv, 0.0);
        } else if (MODE == 2u) {
            out_color = vec4f(mip / 5.0, 0.0, 1.0 - mip / 5.0, 1.0);
        }
    }

    textureStore(output_texture, tci, vec4u(pack4x8unorm(out_color), 0u, 0u, 0u));
}
