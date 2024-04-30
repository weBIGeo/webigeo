/*****************************************************************************
* Alpine Renderer
* Copyright (C) 2022 Adam Celarek
* Copyright (C) 2023 Gerald Kimmersdorfer
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

// ===== DEPTH ENCODE =====
// NOTE: Only positions up to e^13 = 442413 are in between [0,1]
// That's not perfect, but I'll leave it as it is...
// Better would be to normalize it in between near and farplane
// or just use the cs Depth immediately.
fn fakeNormalizeWSDepth(depth: f32) -> f32 {
    return log(depth) / 13.0;
}

fn depthWSEncode2n8(depth: f32) -> vec2<f32> {
    let value = fakeNormalizeWSDepth(depth);
    let scaled = u32(value * 65535.0 + 0.5);
    let r = f32(scaled >> 8u) / 255.0;
    let b = f32(scaled & 255u) / 255.0;
    return vec2<f32>(r, b);
}

// ===== OCTAHEDRON MAPPING FOR NORMALS =====
// https://knarkowicz.wordpress.com/2014/04/16/octahedron-normal-vector-encoding/
/* TODO: Port for shading
highp vec2 octWrap( highp vec2 v ) {
    return ( 1.0 - abs( v.yx ) ) * ( ( v.x >= 0.0 && v.y >= 0.0 ) ? 1.0 : -1.0 );
}
highp uvec2 octNormalEncode2u16(highp vec3 normal) {
    normal /= ( abs( normal.x ) + abs( normal.y ) + abs( normal.z ) );
    if (normal.z < 0.0) normal.xy = octWrap( normal.xy );
    normal.xy = normal.xy * 0.5 + 0.5;
    return uvec2(normal * 65535.0);
}
highp vec3 octNormalDecode2u16(highp uvec2 octNormal16) {
    highp vec2 f = vec2(octNormal16) / 65535.0 * 2.0 - 1.0;
    highp vec3 n = vec3( f.x, f.y, 1.0 - abs( f.x ) - abs( f.y ) );
    highp float t = clamp( -n.z, 0.0, 1.0 );
    n.xy += ( n.x >= 0.0 && n.y >= 0.0) ? -t : t;
    return normalize( n );
}*/