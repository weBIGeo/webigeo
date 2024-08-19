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

const PI: f32 = 3.1415926535897932384626433;
const SEMI_MAJOR_AXIS: f32 = 6378137;
const EARTH_CIRCUMFERENCE: f32 = 2 * PI * SEMI_MAJOR_AXIS;
const ORIGIN_SHIFT: f32 = PI * SEMI_MAJOR_AXIS;

struct TileId {
    x: u32,
    y: u32,
    zoomlevel: u32,
    alignment: u32,
}

const EMPTY_TILE_ZOOMLEVEL: u32 = 4294967295u;

fn tile_ids_equal(a: TileId, b: TileId) -> bool { return a.x == b.x && a.y == b.y && a.zoomlevel == b.zoomlevel; }
fn tile_id_empty(id: TileId) -> bool { return id.zoomlevel == EMPTY_TILE_ZOOMLEVEL; }

struct Bounds {
    min: vec2f,
    max: vec2f,
}

fn y_to_lat(y: f32) -> f32 {
    let mercN = y * PI / ORIGIN_SHIFT;
    let latRad = 2.f * (atan(exp(mercN)) - (PI / 4.0));
    return latRad;
}

fn calc_altitude_correction_factor(y: f32) -> f32 { return 0.125 / cos(y_to_lat(y)); }

// equivalent of nucleus::srs::number_of_horizontal_tiles_for_zoom_level
fn number_of_horizontal_tiles_for_zoom_level(z: u32) -> u32 { return u32(1 << z); }

// equivalent of nucleus::srs::number_of_vertical_tiles_for_zoom_level
fn number_of_vertical_tiles_for_zoom_level(z: u32) -> u32 { return u32(1 << z); }

// equivalent of nucleus::srs::tile_bounds(tile::Id)
fn calculate_bounds(tile_id: TileId) -> vec4<f32> {
    const absolute_min = vec2f(-ORIGIN_SHIFT, -ORIGIN_SHIFT);
    let width_of_a_tile: f32 = EARTH_CIRCUMFERENCE / f32(number_of_horizontal_tiles_for_zoom_level(tile_id.zoomlevel));
    let height_of_a_tile: f32 = EARTH_CIRCUMFERENCE / f32(number_of_vertical_tiles_for_zoom_level(tile_id.zoomlevel));
    let min = absolute_min + vec2f(f32(tile_id.x) * width_of_a_tile, f32(tile_id.y) * height_of_a_tile);
    let max = min + vec2f(width_of_a_tile, height_of_a_tile);
    return vec4f(min.x, min.y, max.x, max.y);
}

fn normal_by_finite_difference_method(
    uv: vec2<f32>,
    quad_width: f32,
    quad_height: f32,
    altitude_correction_factor: f32,
    texture_layer: i32,
    texture_array: texture_2d_array<u32>) -> vec3<f32>
{
    let height_texture_size = textureDimensions(texture_array);
    // from here: https://stackoverflow.com/questions/6656358/calculating-normals-in-a-triangle-mesh/21660173#21660173
    let height = quad_width + quad_height;
    
    // 0 is texel center of first texel, 1 is texel center of last texel
    let uv_tex = vec2i(floor(uv * vec2f(height_texture_size - 1) + 0.5));
    
    let upper_bounds = vec2<i32>(height_texture_size - 1);
    let lower_bounds = vec2<i32>(0, 0);
    let hL_uv = clamp(uv_tex - vec2<i32>(1, 0), lower_bounds, upper_bounds);
    let hL_sample = textureLoad(texture_array, hL_uv, texture_layer, 0);
    let hL = f32(hL_sample.r) * altitude_correction_factor;

    let hR_uv = clamp(uv_tex + vec2<i32>(1, 0), lower_bounds, upper_bounds);
    let hR_sample = textureLoad(texture_array, hR_uv, texture_layer, 0);
    let hR = f32(hR_sample.r) * altitude_correction_factor;

    let hD_uv = clamp(uv_tex + vec2<i32>(0, 1), lower_bounds, upper_bounds);
    let hD_sample = textureLoad(texture_array, hD_uv, texture_layer, 0);
    let hD = f32(hD_sample.r) * altitude_correction_factor;

    let hU_uv = clamp(uv_tex - vec2<i32>(0, 1), lower_bounds, upper_bounds);
    let hU_sample = textureLoad(texture_array, hU_uv, texture_layer, 0);
    let hU = f32(hU_sample.r) * altitude_correction_factor;

    return normalize(vec3<f32>(hL - hR, hD - hU, height));
}

fn world_to_lat_long_alt(pos_ws: vec3f) -> vec3f {
    let mercN = pos_ws.y * PI / ORIGIN_SHIFT;
    let latRad = 2.0 * (atan(exp(mercN)) - (PI / 4.0));
    let latitude = latRad * 180.0 / PI;
    let longitude = (pos_ws.x + ORIGIN_SHIFT) / (ORIGIN_SHIFT / 180.0) - 180.0;
    let altitude = pos_ws.z * cos(latitude * PI / 180.0);
    return vec3f(latitude, longitude, altitude);
}
