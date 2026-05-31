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
#include "screen_pass_vert.wgsl"

@group(0) @binding(0) var<uniform> conf: shared_config;
@group(1) @binding(0) var<uniform> camera: camera_config;
@group(2) @binding(0) var position_texture: texture_2d<f32>;
@group(2) @binding(1) var<uniform> settings: TextureOverlaySettings;
@group(2) @binding(2) var overlay_texture: texture_2d<f32>;
@group(2) @binding(3) var overlay_sampler: sampler;

struct TextureOverlaySettings {
    aabb_min:  vec2f,
    aabb_size: vec2f, // aabb_max - aabb_min, precomputed in double precision on CPU
    opacity:   f32,
    // _pad: f32, (implicit, struct size rounds to alignment of 8)
}

@fragment fn fragmentMain(in: VertexOut) -> @location(0) vec4f {
    let tci = vec2i(in.position.xy);
    let pos_dist = textureLoad(position_texture, tci, 0);
    let pos_cws = pos_dist.xyz;
    let dist = length(pos_cws);
    let pos_ws = pos_cws + camera.position.xyz;

    // Compute UV and its screen-space derivatives unconditionally (uniform control flow).
    // dpdx/dpdy must be called before any early return — WGSL requires uniform control flow.
    // UV is well-defined for all pixels (just a linear transform); garbage values for sky
    // pixels are discarded by the dist/AABB check below before any texture sample occurs.
    let uv = vec2f(
        (pos_ws.x - settings.aabb_min.x) / settings.aabb_size.x,
        1.0 - (pos_ws.y - settings.aabb_min.y) / settings.aabb_size.y
    );
    let ddx_uv = dpdx(uv);
    let ddy_uv = dpdy(uv);

    let aabb_max = settings.aabb_min + settings.aabb_size;
    if (dist <= 0.0 || any(pos_ws.xy < settings.aabb_min) || any(pos_ws.xy > aabb_max)) {
        return vec4f(0.0);
    }

    let color = textureSampleGrad(overlay_texture, overlay_sampler, uv, ddx_uv, ddy_uv);
    let eff_a = color.a * settings.opacity;
    // Output premultiplied alpha so the render blend state (One/OneMinusSrcAlpha)
    // correctly composites via Porter-Duff "over" onto the existing overlay texture.
    return vec4f(color.rgb * eff_a, eff_a);
}
