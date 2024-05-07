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

fn v3f32_to_oct(v: vec3<f32>) -> vec2<f32> {
    var p:vec2<f32> = v.xy * (1.0 / (abs(v.x) + abs(v.y) + abs(v.z)));
    if (v.z <= 0.0) {
        p = (1.0 - abs(p.yx)) * sign(p);
    }
    return p;
}

fn oct_to_v3f32(e: vec2<f32>) -> vec3<f32> {
    var v:vec3<f32> = vec3f(e.xy, 1.0 - abs(e.x) - abs(e.y));
    if (v.z < 0.0) {
        let xy:vec2<f32> = (1.0 - abs(v.yx)) * sign(v.xy);
        v = vec3f(xy, v.z);
    }
    return normalize(v);
}

fn v2f32_to_v2u16(v: vec2<f32>) -> vec2<u32> {
    return vec2<u32>(u32(v.x * 65535.0), u32(v.y * 65535.0));
}

fn v2u16_to_v2f32(e: vec2<u32>) -> vec2<f32> {
    return vec2<f32>(f32(e.x) / 65535.0, f32(e.y) / 65535.0);
}

fn octNormalEncode2u16(n: vec3<f32>) -> vec2<u32> {
    return v2f32_to_v2u16(fma(v3f32_to_oct(n), vec2f(0.5), vec2f(0.5)));
}

fn octNormalDecode2u16(e: vec2<u32>) -> vec3<f32> {
    return oct_to_v3f32(fma(v2u16_to_v2f32(e), vec2f(2.0), vec2f(-1.0)));
}

fn test_encode_decode(index: u32) -> bool {
    // Hardcoded array of 30 random 3D normal vectors on the sphere
    const normals = array<vec3<f32>, 30>(
        vec3<f32>(0.39699535, -0.81505162, -0.42200183),
        vec3<f32>(-0.31692511, 0.59624728, 0.73759586),
        vec3<f32>(-0.14564543, 0.12842754, -0.98096574), //
        vec3<f32>(-0.80710207, -0.46301556, -0.36633707),
        vec3<f32>(-0.68746201, -0.14618284, -0.71135544),
        vec3<f32>(0.02965749, -0.99245204, -0.11899317), //
        vec3<f32>(0.60817772, 0.35842544, -0.7082733),
        vec3<f32>(0.97508881, 0.08914032, 0.2031153),
        vec3<f32>(0.66715458, -0.19226677, 0.71967927),
        vec3<f32>(-0.98279948, -0.17054225, 0.07085561),
        vec3<f32>(-0.74318295, -0.50567332, -0.43814792),
        vec3<f32>(0.21984913, 0.32365938, -0.92027766),
        vec3<f32>(-0.07416941, -0.84925083, -0.52275417),
        vec3<f32>(-0.76293398, 0.63764431, -0.1064964), //
        vec3<f32>(-0.49378689, -0.28316461, -0.82218752),
        vec3<f32>(0.5039764, 0.48216794, 0.7166044),
        vec3<f32>(-0.98353394, 0.17788784, 0.03188891),
        vec3<f32>(-0.49572083, 0.02600753, -0.86809243), //
        vec3<f32>(0.0365897, 0.85014579, -0.52527453),
        vec3<f32>(-0.99443364, -0.10047565, -0.03172355),
        vec3<f32>(0.36831011, 0.49694383, 0.78574455),
        vec3<f32>(-0.19069606, 0.52194128, 0.83139179),
        vec3<f32>(0.45796809, 0.87327089, -0.16632255),
        vec3<f32>(0.65061251, 0.54547444, 0.52835689),
        vec3<f32>(0.41517041, 0.25270464, 0.87394159),
        vec3<f32>(-0.20196329, 0.19700557, 0.95937461), //
        vec3<f32>(0.78059361, -0.58029439, -0.23223271),
        vec3<f32>(0.84343753, -0.39897854, 0.35976277), //
        vec3<f32>(-0.44146773, 0.88629247, -0.13997106), //
        vec3<f32>(-0.32489965, 0.94552451, -0.02058183) //
    );

    // Encode and decode the vector at the specified index
    let encoded = octNormalEncode2u16(normals[index]);
    let decoded = octNormalDecode2u16(encoded);

    //let encoded = float32x3_to_oct(normals[index]);
    //if (encoded.x < -1.0 || encoded.x > 1.0 || encoded.y < -1.0 || encoded.y > 1.0) {
    //    return false;
    //}
    //let decoded = oct_to_float32x3(encoded);

    // Compute the error between the original and decoded vectors
    let error = length(decoded - normals[index]);
    let epsilon = 0.001;

    // Check if the error is within the acceptable epsilon range
    return error < epsilon;
}
