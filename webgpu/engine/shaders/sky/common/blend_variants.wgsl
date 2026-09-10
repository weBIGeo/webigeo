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

// Crossfades two equally-sized sky renders by a per-frame factor (0 = a, 1 = b).
@group(0) @binding(0) var<uniform> blend_factor: f32;
@group(0) @binding(1) var tex_a: texture_2d<f32>;
@group(0) @binding(2) var tex_b: texture_2d<f32>;
@group(0) @binding(3) var out_tex: texture_storage_2d<rgba16float, write>;

@compute @workgroup_size(8, 8, 1)
fn main(@builtin(global_invocation_id) id: vec3<u32>) {
    let size = vec2<u32>(textureDimensions(out_tex));
    if id.x >= size.x || id.y >= size.y {
        return;
    }

    // Branch on the (uniform, not per-pixel) factor rather than always sampling both and mixing:
    // avoids reading the other texture at all outside the blend band, where it may still hold a
    // stale render from before the last mode switch.
    if blend_factor <= 0.0 {
        textureStore(out_tex, id.xy, textureLoad(tex_a, id.xy, 0));
    } else if blend_factor >= 1.0 {
        textureStore(out_tex, id.xy, textureLoad(tex_b, id.xy, 0));
    } else {
        let a = textureLoad(tex_a, id.xy, 0);
        let b = textureLoad(tex_b, id.xy, 0);
        textureStore(out_tex, id.xy, mix(a, b, blend_factor));
    }
}
