/*****************************************************************************
* weBIGeo
* Copyright (C) 2026 Gerald Kimmersdorfer
*
* This program is free software : you can redistribute it and / or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program. If not, see <http : //www.gnu.org/licenses/>.
*****************************************************************************/

///use util/shared_config
///use util/camera_config
///use webgpu::tile_util

@group(0) @binding(0) var<uniform> conf: shared_config;
@group(1) @binding(0) var<uniform> camera: camera_config;

@group(2) @binding(0) var position_texture: texture_2d<f32>;
@group(2) @binding(1) var<uniform> settings: SlippyTileSettings;
@group(2) @binding(2) var tile_texture: texture_2d_array<f32>;
@group(2) @binding(3) var tile_sampler: sampler;
@group(2) @binding(4) var output_texture: texture_storage_2d<rgba8unorm, write>;
@group(2) @binding(5) var background: texture_2d<f32>;    // ping-pong: previous overlay state (premultiplied)
@group(2) @binding(6) var dict_ids: texture_2d<u32>;      // RG32Uint: packed tile-id keys (256x256)
@group(2) @binding(7) var dict_layers: texture_2d<u32>;   // R16Uint: array-layer values (256x256)
@group(2) @binding(8) var tile_ref_texture: texture_2d<u32>; // RGBA32Uint: exact render tile x, y, zoom + packed uv

struct SlippyTileSettings {
    opacity: f32,
    max_zoom: u32,
    tile_size: u32,
    pixel_error_threshold: f32,
}

const SQRT2: f32 = 1.414213562373095;

// Ideal zoom estimate from camera distance only (no absolute world position -- see slippy_tile_overlay_masterplan.md
// / plan doc for why this stays precise: it only needs "how many screen pixels would a texel at zoom Z cover from
// here", which depends on `distance` (a scalar magnitude) and a per-zoom tile-world-size constant, never on where on
// Earth the tile actually is). Mirrors nucleus::tile::utils::refineFunctor's screen-space-error check, solved in
// closed form for the zoom level instead of testing candidate tiles one by one.
fn ideal_zoom(distance: f32, tile_size: u32, max_zoom: u32, pixel_error_threshold: f32) -> u32 {
    let numerator = camera.viewport_size.y * 0.5 * SQRT2 * EARTH_CIRCUMFERENCE * camera.distance_scaling_factor;
    let denominator = f32(tile_size) * distance * pixel_error_threshold;
    let z = ceil(log2(numerator / denominator));
    return u32(clamp(z, 0.0, f32(max_zoom)));
}

// Ports of nucleus::srs::hash_uint16 / pack, matching GpuArrayHelper::generate_dictionary().
// Components are truncated to 16 bits before multiplying, exactly like the uint16_t arithmetic in srs.cpp.
fn tile_hash_uint16(id: TileId) -> u32 {
    let x = ((id.x & 0xFFFFu) * 60197u + 12253u) & 0xFFFFu;
    let y = ((id.y & 0xFFFFu) * 62117u + 59119u) & 0xFFFFu;
    let z = ((id.zoomlevel & 0xFFFFu) * 46965u + 10859u) & 0xFFFFu;
    return (x + y + z) & 0xFFFFu;
}

fn tile_pack(id: TileId) -> vec2<u32> {
    let a = (id.zoomlevel << 27u) | (id.x >> 3u);
    let b = (id.x << 29u) | id.y;
    return vec2<u32>(a, b);
}

// Open-addressing lookup into the 256x256 dictionary. Returns the array layer if the tile is resident.
fn dict_lookup(id: TileId, out_layer: ptr<function, u32>) -> bool {
    let key = tile_pack(id);
    var hash = tile_hash_uint16(id);
    for (var probe = 0u; probe < 256u; probe = probe + 1u) {
        let px = vec2u(hash & 0xFFu, (hash >> 8u) & 0xFFu);
        let slot = textureLoad(dict_ids, px, 0).xy;
        if slot.x == 0xFFFFFFFFu && slot.y == 0xFFFFFFFFu {
            return false; // empty slot -> not resident
        }
        if slot.x == key.x && slot.y == key.y {
            *out_layer = textureLoad(dict_layers, px, 0).x;
            return true;
        }
        hash = (hash + 1u) & 0xFFFFu;
    }
    return false;
}

@compute @workgroup_size(16, 16, 1)
fn computeMain(@builtin(global_invocation_id) gid: vec3u) {
    let dims = vec2u(textureDimensions(output_texture));
    if gid.x >= dims.x || gid.y >= dims.y {
        return;
    }
    let tci = gid.xy;

    let bg = textureLoad(background, tci, 0);
    let pos_dist = textureLoad(position_texture, tci, 0);
    let pos_cws = pos_dist.xyz;

    // Background pixels (no geometry) pass through unchanged.
    if length(pos_cws) <= 0.0 {
        textureStore(output_texture, tci, bg);
        return;
    }

    // Exact render tile id + local uv (precision-safe reference point, never touches absolute
    // world coordinates -- see calc_tile_id_and_uv_for_zoom_level below).
    let tile_ref = textureLoad(tile_ref_texture, tci, 0);
    let render_tile_id = TileId(tile_ref.x, tile_ref.y, tile_ref.z, 0u);
    let render_uv = unpack2x16unorm(tile_ref.w);

    // Ideal ortho zoom from camera distance (screen-space error), independent of the height mesh's
    // own zoom and of this overlay's own pixel_error_threshold/tile_size (not the shared camera's).
    let distance = max(length(pos_cws), 1.0);
    let target_zoom = ideal_zoom(distance, settings.tile_size, settings.max_zoom, settings.pixel_error_threshold);

    var target_tile_id: TileId;
    var target_uv: vec2f;
    calc_tile_id_and_uv_for_zoom_level(render_tile_id, render_uv, target_zoom, &target_tile_id, &target_uv);

    // Walk to coarser ancestors until a resident tile is found (coarse-tile fallback while loading).
    var src = vec4f(0.0);
    var cur_tile_id = target_tile_id;
    var cur_uv = target_uv;
    loop {
        var layer: u32;
        if dict_lookup(cur_tile_id, &layer) {
            let sample = textureSampleLevel(tile_texture, tile_sampler, cur_uv, i32(layer), 0.0);
            let a = settings.opacity * sample.a;
            src = vec4f(sample.rgb * a, a); // premultiplied
            break;
        }
        var parent_tile_id: TileId;
        var parent_uv: vec2f;
        if !decrease_zoom_level_by_one(cur_tile_id, cur_uv, &parent_tile_id, &parent_uv) {
            break;
        }
        cur_tile_id = parent_tile_id;
        cur_uv = parent_uv;
    }

    // Premultiplied-alpha blend over the previous overlay state.
    let result = src + bg * (1.0 - src.a);
    textureStore(output_texture, tci, result);
}
