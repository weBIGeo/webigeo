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

// Trivial full-screen blit used to copy the (legacy gradient or LUT-sky) result texture to the swapchain.

///use screen_pass_vert

@group(0) @binding(0) var src_texture: texture_2d<f32>;

@fragment
fn fragmentMain(vertex_out: VertexOut) -> @location(0) vec4f {
    let dims = textureDimensions(src_texture);
    let tci = min(vec2u(vertex_out.texcoords * vec2f(dims)), dims - vec2u(1u));
    return vec4f(textureLoad(src_texture, tci, 0).rgb, 1.0);
}
