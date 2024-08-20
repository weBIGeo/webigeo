/*****************************************************************************
 * weBIGeo
 * Copyright (C) 2024 Patrick Komon
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

#include "shared_config.wgsl"
#include "camera_config.wgsl"

@group(0) @binding(0) var<uniform> config: shared_config;

@group(1) @binding(0) var<uniform> camera: camera_config;

@group(2) @binding(0) var<storage> positions: array<vec4f>;

struct VertexOut {
    @builtin(position) position: vec4f,
}

struct FragOut {
    @location(0) color: vec4f,
}

@vertex
fn vertexMain(@builtin(vertex_index) vertex_index: u32) -> VertexOut {
    
    var vertex_out: VertexOut;
    let pos = positions[vertex_index];
    
    vertex_out.position = camera.view_proj_matrix * vec4f(pos.xyz - camera.position.xyz, 1);
    return vertex_out;
}

@fragment
fn fragmentMain(vertex_out: VertexOut) -> FragOut {
    var frag_out: FragOut;
    frag_out.color = vec4f(1, 0, 0, 1);
    return frag_out;
}