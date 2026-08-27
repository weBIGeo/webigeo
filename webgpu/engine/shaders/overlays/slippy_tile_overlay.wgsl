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
///use webgpu::encoder
///use webgpu::tile_util
///use webgpu::normals_util
///use webgpu::general
///use webgpu::color_mapping

@group(0) @binding(0) var<uniform> conf: shared_config;

@group(2) @binding(0) var depth_texture: texture_2d<f32>;
@group(2) @binding(1) var<uniform> settings: SlippyTileSettings;
@group(2) @binding(2) var tile_texture: texture_2d_array<f32>;
@group(2) @binding(3) var tile_sampler: sampler;
@group(2) @binding(4) var output_texture: texture_storage_2d<rgba8unorm, write>;
@group(2) @binding(5) var background: texture_2d<f32>;      // ping-pong: previous overlay state (premultiplied)
@group(2) @binding(6) var dict_ids: texture_2d<u32>;         // RG32Uint: packed tile-id keys (256x256)
@group(2) @binding(7) var dict_layers: texture_2d<u32>;      // R16Uint: array-layer values (256x256)
@group(2) @binding(8) var tile_ref_texture: texture_2d<u32>; // RG32Uint: packed uv + (derivatives | frame-local id)
@group(2) @binding(9) var<storage, read> frame_tile_ids: array<vec2u>; // frame-local id -> packed render tile id
@group(2) @binding(10) var<storage, read> frame_target_zoom: array<u32>; // frame-local id -> per-tile target zoom (zoom_selection_mode == PER_TILE only)
@group(2) @binding(11) var normal_texture: texture_2d<u32>; // gbuffer normal, used by DATA_MODE_SNOW_AVG_NORMALS's slope mask

// Temporary: masks the snow-depth ramp by surface steepness, same heuristic as ScreenSpaceSnowOverlay
// (steep slopes don't hold snow), used by DATA_MODE_SNOW_AVG_NORMALS.
const SNOW_ANGLE_MIN: f32 = 0.0; // deg, steepness below which snow is unmasked
const SNOW_ANGLE_MAX: f32 = 45.0; // deg, steepness above which snow is fully masked out
const SNOW_ANGLE_BLEND: f32 = 5.0; // deg, falloff width around angle_max

struct SlippyTileSettings {
    opacity: f32,
    max_zoom: u32,
    tile_size: u32,
    pixel_error_threshold: f32,
    debug_view: u32, // 0 = none, 1 = zoom level (see zoom_level_color)
    zoom_selection_mode: u32, // 0 = per-pixel (derivatives), 1 = per-tile (frame_target_zoom)
    data_mode: u32, // see DATA_MODE_* consts
    _pad1: u32,
}

const DATA_MODE_RGBA: u32 = 0u;
const DATA_MODE_SNOW_AVG: u32 = 1u;
const DATA_MODE_SNOW_AVG_NORMALS: u32 = 2u;
// Decodes and paints the normal as color (opaque debug visualization) -- for comparing side by side
// against DATA_MODE_NORMALS_OVERWRITE's actual gbuffer write, e.g. to tell apart "the baked normal
// data itself is wrong" from "the overwrite pass is wrong".
const DATA_MODE_NORMALS: u32 = 3u;
// Paints nothing on screen -- slippy_tile_gbuffer_normals_pass.wgsl does the real work of
// overwriting the gbuffer normal directly, so lighting reacts to it.
const DATA_MODE_NORMALS_OVERWRITE: u32 = 4u;

fn decode_hemioct_normal_127(sample_rg: vec2f) -> vec3f {
    let byte = round(sample_rg * 255.0);
    let e = (byte - vec2f(127.0)) / 127.0;
    let t = vec2f(e.x + e.y, e.x - e.y) * 0.5;
    let n = vec3f(t, 1.0 - abs(t.x) - abs(t.y));
    return normalize(n);
}

// Snow tiles quantize [0, SNOW_CEILING_CM] linearly into a byte (see nucleus_extra's _encode_cm).
const SNOW_CEILING_CM: f32 = 500.0;
fn decode_snow_cm(byte_unorm: f32) -> f32 {
    return byte_unorm * SNOW_CEILING_CM;
}

const DEBUG_VIEW_NONE: u32 = 0u;
const DEBUG_VIEW_ZOOM_LEVEL: u32 = 1u;
const DEBUG_VIEW_TARGET_ZOOM_LEVEL: u32 = 2u;
const DEBUG_VIEW_TARGET_TILE_ID: u32 = 3u;
const ZOOM_SELECTION_MODE_PER_PIXEL: u32 = 0u;
const ZOOM_SELECTION_MODE_PER_TILE: u32 = 1u;

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

// Result of resolving a screen pixel to a resident tile sample -- shared between computeMain (the
// on-screen overlay) and slippy_tile_gbuffer_normals_pass.wgsl's fragmentMain (which overwrites the
// gbuffer normal for DATA_MODE_NORMALS), so the tile-residency walk logic exists exactly once.
// Caller must have already checked depth_texture (this assumes geometry is present at tci).
struct ResolvedTileSample {
    found: bool,
    color: vec4f, // valid only if found
    target_zoom: u32, // the *ideal* target zoom, valid regardless of found
    target_tile_id: TileId, // the *ideal* target tile id (before residency fallback), valid regardless of found
    resolved_zoom: u32, // the actually-resolved (resident) tile's zoom, valid only if found
}

fn resolve_tile_sample(tci: vec2u) -> ResolvedTileSample {
    var result: ResolvedTileSample;
    result.found = false;
    result.color = vec4f(0.0);
    result.resolved_zoom = 0u;

    // Exact render-tile local uv + frame-local render-tile id + isotropic mip footprint, all packed
    // by render_tiles.wgsl's fragmentMain (see docs/masterplan.md Plan 3).
    let tile_ref = textureLoad(tile_ref_texture, tci, 0);
    let render_uv = unpack2x16unorm(tile_ref.x);
    let frame_local_id = tile_ref.y & 0xFFFFu;
    let derivatives = unpack_derivatives(tile_ref.y >> 16u);
    let render_tile_id = unpack_tile_id(frame_tile_ids[frame_local_id]);

    // Target zoom -- see Settings::zoom_selection_mode (SlippyTileOverlay.h) for the per-pixel vs.
    // per-tile comparison. per-tile is one CPU-computed value for the whole render tile (uniform
    // across every pixel of it); per-pixel is derived here from the rasterizer-computed footprint
    // (hardware-accurate, and immune to the render tile's own zoom being clamped).
    var target_zoom: u32;
    if settings.zoom_selection_mode == ZOOM_SELECTION_MODE_PER_TILE {
        target_zoom = frame_target_zoom[frame_local_id];
    } else {
        let target_zoom_f = round(-derivatives - log2(f32(settings.tile_size) * settings.pixel_error_threshold));
        target_zoom = u32(clamp(target_zoom_f, 0.0, f32(settings.max_zoom)));
    }
    result.target_zoom = target_zoom;

    // Jump straight to target_zoom (ascend or descend in one call), then walk up on a miss. The
    // scheduler guarantees every ancestor of a resident tile is also resident, so if target_zoom
    // itself isn't resident yet, no level between it and the render tile can be either -- walking
    // up from target_zoom always reaches the deepest actually-resident tile directly.
    var cur_id: TileId;
    var cur_uv: vec2f;
    calc_tile_id_and_uv_for_zoom_level(render_tile_id, render_uv, target_zoom, &cur_id, &cur_uv);
    result.target_tile_id = cur_id;

    var layer: u32;
    loop {
        if dict_lookup(cur_id, &layer) {
            result.found = true;
            break;
        }
        var parent_id: TileId;
        var parent_uv: vec2f;
        if !decrease_zoom_level_by_one(cur_id, cur_uv, &parent_id, &parent_uv) {
            break;
        }
        cur_id = parent_id;
        cur_uv = parent_uv;
    }
    if result.found {
        result.resolved_zoom = cur_id.zoomlevel;
        // No mipmaps on the tile array (GpuTileTextureArray::init: mipLevelCount = 1) -- the
        // discrete zoom choice above already is the "mip" selection, so always sample lod 0.
        result.color = textureSampleLevel(tile_texture, tile_sampler, cur_uv, i32(layer), 0.0);
    }
    return result;
}

@compute @workgroup_size(16, 16, 1)
fn computeMain(@builtin(global_invocation_id) gid: vec3u) {
    let dims = vec2u(textureDimensions(output_texture));
    if gid.x >= dims.x || gid.y >= dims.y {
        return;
    }
    let tci = gid.xy;

    let bg = textureLoad(background, tci, 0);
    let raw_depth = textureLoad(depth_texture, tci, 0).r;

    // Background pixels (no geometry) pass through unchanged.
    if raw_depth <= 0.0 {
        textureStore(output_texture, tci, bg);
        return;
    }

    let resolved = resolve_tile_sample(tci);

    // Debug visualization of the *ideal* target zoom itself -- color-coded, fully opaque, and
    // computed before any residency resolution, so it shows what zoom we'd *like* to sample
    // regardless of which tiles are actually loaded (unlike DEBUG_VIEW_ZOOM_LEVEL, which colors the
    // resolved/resident tile after the ancestor walk).
    if settings.debug_view == DEBUG_VIEW_TARGET_ZOOM_LEVEL {
        textureStore(output_texture, tci, vec4f(zoom_level_color(resolved.target_zoom), 1.0));
        return;
    } else if settings.debug_view == DEBUG_VIEW_TARGET_TILE_ID {
        // Same idea as DEBUG_VIEW_TARGET_ZOOM_LEVEL, but color-codes the tile itself (not just its
        // zoom) so individual target tiles -- not just zoom bands -- are visually distinguishable.
        textureStore(output_texture, tci, vec4f(tile_id_debug_color(resolved.target_tile_id), 1.0));
        return;
    }

    var src = vec4f(0.0);
    if resolved.found {
        if settings.debug_view == DEBUG_VIEW_ZOOM_LEVEL {
            // Debug visualization: fully opaque, overrides whatever's beneath so the color-coding
            // reads unambiguously.
            src = vec4f(zoom_level_color(resolved.resolved_zoom), 1.0);
        } else if settings.data_mode == DATA_MODE_NORMALS {
            // Debug visualization: decode and paint the normal as color, fully opaque.
            let normal = decode_hemioct_normal_127(resolved.color.rg);
            src = vec4f(normal * 0.5 + vec3f(0.5), 1.0);
        } else if settings.data_mode == DATA_MODE_NORMALS_OVERWRITE {
            // Nothing to paint on screen -- slippy_tile_gbuffer_normals_pass.wgsl overwrites the
            // gbuffer normal directly, which is where this mode's actual effect is visible.
        } else {
            let sample = resolved.color;
            if settings.data_mode == DATA_MODE_RGBA {
                let a = settings.opacity * sample.a;
                src = vec4f(sample.rgb * a, a); // premultiplied
            } else {
                let avg_cm = decode_snow_cm(sample.r);
                var ramp = clamp(avg_cm / 20.0, 0.0, 1.0);
                if settings.data_mode == DATA_MODE_SNOW_AVG_NORMALS {
                    let normal = octNormalDecode2u16(textureLoad(normal_texture, tci, 0).xy);
                    let steepness_deg = degrees(get_slope_angle(normal));
                    ramp *= calculate_band_falloff(steepness_deg, SNOW_ANGLE_MIN, SNOW_ANGLE_MAX, SNOW_ANGLE_BLEND);
                }
                let a = settings.opacity * sample.a * ramp;
                src = vec4f(a, a, a, a); // white, premultiplied
            }
        }
    }

    // Premultiplied-alpha blend over the previous overlay state.
    let result = src + bg * (1.0 - src.a);
    textureStore(output_texture, tci, result);
}
