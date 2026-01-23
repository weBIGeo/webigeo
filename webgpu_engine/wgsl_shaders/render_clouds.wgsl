/*****************************************************************************
 * weBIGeo
 * Copyright (C) 2022 Adam Celarek
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

#include "util/camera_config.wgsl"
#include "screen_pass_vert.wgsl"

struct tile_info {
    index: u32,
    zoom: u32,
}

struct shader_params {
    bounds_min: vec4f,
    bounds_max: vec4f,
}

@group(0) @binding(0) var<uniform> camera : camera_config;

@group(1) @binding(0) var<uniform> params : shader_params;
@group(1) @binding(1) var cloud_texture: texture_3d<f32>;
@group(1) @binding(2) var cloud_sampler: sampler;
@group(1) @binding(3) var<storage, read> tile_infos: array<tile_info>;

// tile size at zoom level 11
override tile_size_x = 19567.879241005121;
override tile_size_y = 19567.879241005121;
override inv_tile_size_x = 1.0 / 19567.879241005121;
override inv_tile_size_y = 1.0 / 19567.879241005121;

override tile_count_x = 46/2;
override tile_count_y = 26/2;

override zoom_max = 10;

override tile_coords_offset_x = 538;
override tile_coords_offset_y = 660;

fn unproject(normalised_device_coordinates: vec2f) -> vec3f {
    let unprojected = camera.inv_proj_matrix * vec4(normalised_device_coordinates, 1.0, 1.0);
    let normalised_unprojected = unprojected / unprojected.w;
    return normalize((camera.inv_view_matrix * normalised_unprojected).xyz);
}

@fragment
fn fragmentMain(vertex_out : VertexOut) -> @location(0) vec4f {

    let origin = camera.position.xyz;
    let ray_direction = unproject(vertex_out.texcoords * vec2f(2.0, -2.0) + vec2f(-1.0, 1.0));

    let cloud_height = 10000.0;
    let t = (cloud_height - origin.z) / ray_direction.z;
    if (t < 0.0) {
        discard;
    }

    let hit_pos = origin + ray_direction * t;
    // bs ... bounds space
    let hit_pos_bs = hit_pos - params.bounds_min.xyz;
    // ts ... tile space
    let hit_pos_ts = hit_pos_bs.xy * vec2f(inv_tile_size_x, inv_tile_size_y);
    let tile_id = vec3i(vec2i(floor(hit_pos_ts)), zoom_max);
    // Convert to absolute tile coordinates at zoom_max
    let tile_id_abs = tile_id.xy + vec2i(tile_coords_offset_x, tile_coords_offset_y);

    if(tile_id.x < 0 || tile_id.x > tile_count_x || tile_id.y < 0 || tile_id.y > tile_count_y) {
        discard;
    }

    let tile_index = tile_id.x + tile_id.y * tile_count_x;
    let tile_info = tile_infos[tile_index];

    let dz = max(u32(zoom_max) - tile_info.zoom, 0);
    let tile_size = 1 << dz;
    let parent_tile_abs = tile_id_abs / tile_size;

    // Convert back to relative coordinates at zoom_max
    let tile_corner_abs = parent_tile_abs * tile_size;
    let tile_corner = tile_corner_abs - vec2i(tile_coords_offset_x, tile_coords_offset_y);

    var tile_uv = (hit_pos_ts - vec2f(tile_corner)) / f32(tile_size);

    var color = vec4f(1.0);

    let atlas_x = tile_info.index & 3u;
    let atlas_y = (tile_info.index >> 2) & 3u;
    let atlas_z = (tile_info.index >> 4) & 3u;

    let tile_uvz = vec3f(tile_uv.x + f32(atlas_x), tile_uv.y + f32(atlas_y), 0.05f + f32(atlas_z));
    let atlas_uvz = tile_uvz * 0.25f;

    let rgba = textureSampleLevel(cloud_texture, cloud_sampler, atlas_uvz, 2.0);
    color.a = smoothstep(0.4, 0.6, rgba.r);

    color = rgba;
//    color.a = 1.0;
//    color = mix(vec4f(f32(tile_corner.x % 8) / 7.0, f32(tile_corner.y % 8) / 7.0, 0.0, color.a), color, 0.0);
//    color = mix(vec4f(f32(tile_id.x) / f32(tile_count_x), f32(tile_id.y) / f32(tile_count_y), 0.0, color.a), color, 0.2);
//    color = vec4f(f32(tile_id.x) / f32(tile_count_x), f32(tile_id.y) / f32(tile_count_y), 0.0, 1.0);
//    color = vec4f(tile_uv.x, tile_uv.y, 0.0, 1.0);
//    color = vec4f(hit_pos_ts.x, hit_pos_ts.y, 0.0, 1.0);
//    color = vec4f(hit_pos_bs.x / (params.bounds_max.x - params.bounds_min.x), hit_pos_bs.y / (params.bounds_max.y - params.bounds_min.y), 0.0, 1.0);
//    color = vec4f(f32(tile_id.x) / f32(tile_count_x), f32(tile_id.y) / f32(tile_count_y), 0.0, 1.0);
//    color = vec4f(f32(atlas_x) * 0.25, f32(atlas_y) * 0.25 * 0.0, f32(atlas_z) * 0.25 * 0.0, 1.0);
//    color = vec4f(f32(tile_info.index % 16) / 16.0, 0.0, 0.0, 1.0);

    return color;
}
