/*****************************************************************************
 * weBIGeo
 * Copyright (C) 2022 Adam Celarek
 * Copyright (C) 2023 Gerald Kimmersdorfer
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

#include "util/shared_config.wgsl"
#include "util/hashing.wgsl"
#include "util/camera_config.wgsl"
#include "util/encoder.wgsl"
#include "util/tile_util.wgsl"
#include "util/normals_util.wgsl"
#include "util/snow.wgsl"
#include "util/filtering.wgsl"

@group(0) @binding(0) var<uniform> config: shared_config;

@group(1) @binding(0) var<uniform> camera: camera_config;

@group(2) @binding(0) var<uniform> n_edge_vertices: i32;
@group(2) @binding(1) var height_texture: texture_2d_array<u32>;
@group(2) @binding(2) var height_sampler: sampler;
@group(2) @binding(3) var ortho_texture: texture_2d_array<f32>;
@group(2) @binding(4) var ortho_sampler: sampler;

struct VertexIn {
    @location(0) bounds: vec4f,
    @location(1) height_texture_layer: i32,
    @location(2) ortho_texture_layer: i32,
    @location(3) tileset_id: i32,
    @location(4) height_zoomlevel: i32,
    @location(5) tile_id: vec4<u32>,
    @location(6) ortho_zoomlevel: i32,
}

struct VertexOut {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
    @location(1) pos_cws: vec3f,
    @location(2) normal: vec3f,
    @location(3) @interpolate(flat) height_texture_layer: i32,
    @location(4) @interpolate(flat) ortho_texture_layer: i32,
    @location(5) @interpolate(flat) color: vec3f,
    @location(6) @interpolate(flat) tile_id: vec3<u32>,
    @location(7) @interpolate(flat) ortho_zoomlevel: i32,
}

struct FragOut {
    @location(0) albedo: u32,
    @location(1) position: vec4f,
    @location(2) normal_enc: vec2u,
    @location(3) overlay: u32,
}

fn camera_world_space_position(
    vertex_index: u32,
    bounds: vec4f,
    height_texture_layer: i32,
    tile_id: TileId,
    height_zoomlevel: i32,
    uv: ptr<function, vec2f>,
    n_quads_per_direction: ptr<function, f32>,
    quad_width: ptr<function, f32>,
    quad_height: ptr<function, f32>,
    altitude_correction_factor: ptr<function, f32>,
    height_uv: ptr<function, vec2f>,
) -> vec3f {
    let n_quads_per_direction_int = n_edge_vertices - 1;
    *n_quads_per_direction = f32(n_quads_per_direction_int);
    *quad_width = (bounds.z - bounds.x) / (*n_quads_per_direction);
    *quad_height = (bounds.w - bounds.y) / (*n_quads_per_direction);

    var row: i32 = i32(vertex_index) / n_edge_vertices;
    var col: i32 = i32(vertex_index) - (row * n_edge_vertices);
    let curtain_vertex_id = i32(vertex_index) - n_edge_vertices * n_edge_vertices;
    if (curtain_vertex_id >= 0) {
        if (curtain_vertex_id < n_edge_vertices) {
            row = (n_edge_vertices - 1) - curtain_vertex_id;
            col = (n_edge_vertices - 1);
        }
        else if (curtain_vertex_id >= n_edge_vertices && curtain_vertex_id < 2 * n_edge_vertices - 1) {
            row = 0;
            col = (n_edge_vertices - 1) - (curtain_vertex_id - n_edge_vertices) - 1;
        }
        else if (curtain_vertex_id >= 2 * n_edge_vertices - 1 && curtain_vertex_id < 3 * n_edge_vertices - 2) {
            row = curtain_vertex_id - 2 * n_edge_vertices + 2;
            col = 0;
        }
        else {
            row = (n_edge_vertices - 1);
            col = curtain_vertex_id - 3 * n_edge_vertices + 3;
        }
    }
    // Note: for higher zoom levels it would be enough to calculate the altitude_correction_factor on cpu
    // for lower zoom levels we could bake it into the texture.
    // but there was no measurable difference despite the cos and atan, so leaving as is for now.
    let var_pos_cws_y: f32 = f32(n_quads_per_direction_int - row) * f32(*quad_width) + bounds.y;
    let pos_y: f32 = var_pos_cws_y + camera.position.y;
    *altitude_correction_factor = 0.125 / cos(y_to_lat(pos_y)); // https://github.com/AlpineMapsOrg/renderer/issues/5

    *uv = vec2f(f32(col) / (*n_quads_per_direction), f32(row) / (*n_quads_per_direction));

    var output_tile_id: TileId;
    decrease_zoom_level_until(tile_id, *uv, u32(height_zoomlevel), &output_tile_id, height_uv);
    let altitude_tex = f32(bilinear_sample_u32(height_texture, height_sampler, *height_uv, u32(height_texture_layer)));
    let adjusted_altitude: f32 = altitude_tex * (*altitude_correction_factor);

    var var_pos_cws = vec3f(f32(col) * (*quad_width) + bounds.x, var_pos_cws_y, adjusted_altitude - camera.position.z);

    if (curtain_vertex_id >= 0) {
        // TODO implement preprocessor constants in shader
        //float curtain_height = CURTAIN_REFERENCE_HEIGHT;

        var curtain_height = f32(1000);
// TODO implement preprocessor if in shader
/*#if CURTAIN_HEIGHT_MODE == 1
        let dist_factor = clamp(length(var_pos_cws) / 100000.0, 0.2, 1.0);
        curtain_height *= dist_factor;
#endif*/
        var_pos_cws.z = var_pos_cws.z - curtain_height;
    }

    return var_pos_cws;
}

fn normal_by_fragment_position_interpolation(pos_cws: vec3<f32>) -> vec3<f32> {
    let dFdxPos = dpdy(pos_cws);
    let dFdyPos = dpdx(pos_cws);
    return normalize(cross(dFdxPos, dFdyPos));
}

@vertex
fn vertexMain(@builtin(vertex_index) vertex_index: u32, vertex_in: VertexIn) -> VertexOut {
    var uv: vec2f;
    var height_uv: vec2f;
    var n_quads_per_direction: f32;
    var quad_width: f32;
    var quad_height: f32;
    var altitude_correction_factor: f32;
    let tile_id = TileId(vertex_in.tile_id.x, vertex_in.tile_id.y, vertex_in.tile_id.z, 4294967295u);
    let var_pos_cws = camera_world_space_position(vertex_index, vertex_in.bounds, vertex_in.height_texture_layer, tile_id, vertex_in.height_zoomlevel, &uv, &n_quads_per_direction, &quad_width, &quad_height, &altitude_correction_factor, &height_uv);

    let pos = vec4f(var_pos_cws, 1);
    let clip_pos = camera.view_proj_matrix * pos;

    var vertex_out: VertexOut;
    vertex_out.position = clip_pos;
    vertex_out.uv = uv;
    vertex_out.pos_cws = var_pos_cws;

    vertex_out.normal = vec3f(0.0);
    if (config.normal_mode == 2) {
        vertex_out.normal = normal_by_finite_difference_method(height_uv, quad_width, quad_height, altitude_correction_factor, vertex_in.height_texture_layer, height_texture);
    }
    vertex_out.height_texture_layer = vertex_in.height_texture_layer;
    vertex_out.ortho_texture_layer = vertex_in.ortho_texture_layer;

    var vertex_color = vec3f(0.0);
    if (config.overlay_mode == 2) {
        vertex_color = color_from_id_hash(u32(vertex_in.tileset_id));
    } else if (config.overlay_mode == 3) {
        vertex_color = color_from_id_hash(u32(vertex_in.height_zoomlevel));
        //vertex_color = color_from_id_hash(u32(vertex_in.ortho_zoomlevel));
    } else if (config.overlay_mode == 4) {
        vertex_color = color_from_id_hash(u32(vertex_index));
    }
    vertex_out.color = vertex_color;
    vertex_out.tile_id = vertex_in.tile_id.xyz;
    vertex_out.ortho_zoomlevel = vertex_in.ortho_zoomlevel;
    return vertex_out;
}

@fragment
fn fragmentMain(vertex_out: VertexOut) -> FragOut {
    let tile_id = TileId(vertex_out.tile_id.x, vertex_out.tile_id.y, vertex_out.tile_id.z, 4294967295u);

    // obtain uv coordinates for desired ortho zoom level and sample
    var ortho_tile_id: TileId;
    var ortho_uv: vec2f;
    let found_ortho = decrease_zoom_level_until(tile_id, vertex_out.uv, u32(vertex_out.ortho_zoomlevel), &ortho_tile_id, &ortho_uv);
    var albedo = textureSample(ortho_texture, ortho_sampler, ortho_uv, vertex_out.ortho_texture_layer).rgb;

    var frag_out: FragOut;

    var dist = length(vertex_out.pos_cws);
    var normal = vertex_out.normal;
    if (config.normal_mode != 0) {
        if (config.normal_mode == 1) {
            normal = normal_by_fragment_position_interpolation(vertex_out.pos_cws);
        }

        frag_out.normal_enc = octNormalEncode2u16(normal);
    }

    // HANDLE OVERLAYS (and mix it with the albedo color) THAT CAN JUST BE DONE IN THIS STAGE
    // NOTE: Performancewise its generally better to handle overlays in the compose step! (overdraw)
    var overlay_color = vec4f(0.0);
    if (config.overlay_mode > 0u && config.overlay_mode < 100u) {
        if (config.overlay_mode == 1) {
            overlay_color = vec4f(normal * 0.5 + 0.5, 1.0);
        } else {
            overlay_color = vec4f(vertex_out.color.xyz, 1);
        }
        //albedo = mix(albedo, overlay_color.xyz, config.overlay_strength * overlay_color.w);
    }
    frag_out.overlay = pack4x8unorm(overlay_color);
    frag_out.albedo = pack4x8unorm(vec4f(albedo, 1.0));

    frag_out.position = vec4f(vertex_out.pos_cws, dist);

    return frag_out;
}
