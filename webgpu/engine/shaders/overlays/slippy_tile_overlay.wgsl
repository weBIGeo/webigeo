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
///use webgpu::encoder
///use webgpu::tile_util
///use webgpu::normals_util
///use webgpu::general
///use webgpu::color_mapping
///use webgpu::position_util

@group(0) @binding(0) var<uniform> conf: shared_config;
@group(1) @binding(0) var<uniform> camera: camera_config;

@group(2) @binding(0) var depth_texture: texture_2d<f32>;
@group(2) @binding(1) var<uniform> settings: SlippyTileSettings;
@group(2) @binding(2) var tile_texture: texture_2d_array<f32>;
@group(2) @binding(3) var tile_sampler: sampler;
@group(2) @binding(4) var output_texture: texture_storage_2d<rgba8unorm, write>;
@group(2) @binding(5) var background: texture_2d<f32>;      // ping-pong: previous overlay state (premultiplied)
@group(2) @binding(6) var dict_texture: texture_2d<u32>;     // RGBA32Uint (256x256): xy = packed tile-id key, z = array layer, w = unused
@group(2) @binding(8) var tile_ref_texture: texture_2d<u32>; // RG32Uint: packed uv + (derivatives | frame-local id)
@group(2) @binding(9) var<storage, read> frame_tile_ids: array<vec2u>; // frame-local id -> packed render tile id
// Per-frame "wanted tiles" hash set (open addressing, WANTED_TILE_SLOTS entries): which *ideal* target
// tiles the visible pixels asked for this frame (resident or not) plus a pixel count per tile, for the
// CPU to read back and prioritize requests/eviction by. Zeroed by the CPU before every draw, so a
// slot's key is stored inverted (~key): 0 means empty (see record_wanted_tile). Compute path only --
@group(2) @binding(10) var<storage, read_write> wanted_tiles: array<WantedTileSlot>;
@group(2) @binding(11) var normal_texture: texture_2d<u32>; // gbuffer normal, used by DATA_MODE_SNOW_AVG_NORMALS's slope mask

struct WantedTileSlot {
    key_lo: atomic<u32>, // ~tile_pack(id).x, 0 = empty
    key_hi: atomic<u32>, // ~tile_pack(id).y, 0 = empty
    count: atomic<u32>, // number of (recorded) pixels that targeted this tile
}
const WANTED_TILE_SLOTS: u32 = 4096u; // must match SlippyTileOverlay's k_wanted_tile_slots (power of two)

// Temporary: masks the snow-depth ramp by surface steepness, same heuristic as ScreenSpaceSnowOverlay
// (steep slopes don't hold snow), used by DATA_MODE_SNOW_AVG_NORMALS.
const SNOW_ANGLE_MIN: f32 = 0.0; // deg, steepness below which snow is unmasked
const SNOW_ANGLE_MAX: f32 = 45.0; // deg, steepness above which snow is fully masked out
const SNOW_ANGLE_BLEND: f32 = 5.0; // deg, falloff width around angle_max

struct SlippyTileSettings {
    opacity: f32,
    max_zoom: u32,
    min_zoom: u32, // floor for the resolved per-pixel target zoom (e.g. ~3 for sources with no coarser tiles)
    // Texels per side of one *dictionary tile*, i.e. of one tile_texture array layer -- for quad
    // sources that is twice the raw tile resolution (a layer holds a stitched 2x2 quad). Only used
    // to turn pixel_error_threshold into a target zoom.
    tile_size: u32,
    pixel_error_threshold: f32,
    debug_view: u32, // 0 = none, 1 = zoom level (see zoom_level_color)
    zoom_selection_mode: u32, // 0 = per-pixel (derivatives), 1 = per-tile (true per-pixel distance)
    data_mode: u32, // see DATA_MODE_* consts
    blend_zoom_transitions: u32, // 0/1 -- cross-fade across zoom transitions instead of popping
    zoom_blend_band: f32, // width (in zoom units) of the cross-fade band around each integer boundary
    wanted_tiles_stride: u32, // record every Nth pixel (in x and y) into wanted_tiles; 0 = off, 1 = every pixel
    highlight_x: u32, // target tile to tint (UI hover), highlight_zoom == 0xFFFFFFFF = none
    highlight_y: u32,
    highlight_zoom: u32,
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

// Open-addressing lookup into the 256x256 dictionary (RGBA32Uint: xy = key, z = layer, one texture/one
// textureLoad per probe). Returns the array layer if the tile is resident.
fn dict_lookup(id: TileId, out_layer: ptr<function, u32>) -> bool {
    let key = tile_pack(id);
    var hash = tile_hash_uint16(id);
    for (var probe = 0u; probe < 256u; probe = probe + 1u) {
        let px = vec2u(hash & 0xFFu, (hash >> 8u) & 0xFFu);
        let slot = textureLoad(dict_texture, px, 0);
        if slot.x == 0xFFFFFFFFu && slot.y == 0xFFFFFFFFu {
            return false; // empty slot -> not resident
        }
        if slot.x == key.x && slot.y == key.y {
            *out_layer = slot.z;
            return true;
        }
        hash = (hash + 1u) & 0xFFFFu;
    }
    return false;
}

// Inserts id into the per-frame wanted_tiles set (or bumps its count if already there). The packed
// key is two words but WGSL atomics are 32-bit, so the insert is a two-word compare-exchange: each
// word only ever transitions 0 -> value once, so whichever (lo, hi) pair ends up in a slot owns it and
// a thread whose hi-word CAS loses to a different tile sharing its lo-word simply keeps probing. No
// spinning, no locks. The atomicLoad fast path means all but the first pixel of a tile skip the CAS
// and only issue one (return-value-free, i.e. reduction) atomicAdd.
fn record_wanted_tile(id: TileId) {
    let key = ~tile_pack(id); // inverted: 0 (the cleared buffer) means empty, and ~pack is never 0
    var h = tile_hash_uint16(id) & (WANTED_TILE_SLOTS - 1u);
    for (var probe = 0u; probe < 64u; probe = probe + 1u) {
        if atomicLoad(&wanted_tiles[h].key_lo) == key.x && atomicLoad(&wanted_tiles[h].key_hi) == key.y {
            atomicAdd(&wanted_tiles[h].count, 1u);
            return;
        }
        let lo = atomicCompareExchangeWeak(&wanted_tiles[h].key_lo, 0u, key.x);
        if lo.exchanged || lo.old_value == key.x {
            let hi = atomicCompareExchangeWeak(&wanted_tiles[h].key_hi, 0u, key.y);
            if hi.exchanged || hi.old_value == key.y {
                atomicAdd(&wanted_tiles[h].count, 1u);
                return;
            }
            if hi.old_value == 0u {
                continue; // spurious CAS failure -> retry the same slot
            }
            // slot owned by another tile that shares our lo-word -> keep probing
        } else if lo.old_value == 0u {
            continue; // spurious CAS failure -> retry the same slot
        }
        h = (h + 1u) & (WANTED_TILE_SLOTS - 1u);
    }
    // table full along this probe chain -- drop (the set is best-effort, next frame retries)
}

// Result of resolving a screen pixel to a resident tile sample -- shared between computeMain (the
// on-screen overlay) and slippy_tile_gbuffer_normals_pass.wgsl's fragmentMain (which overwrites the
// gbuffer normal for DATA_MODE_NORMALS), so the tile-residency walk logic exists exactly once.
// Caller must have already checked depth_texture (this assumes geometry is present at tci).
struct ResolvedTileSample {
    found: bool,
    color: vec4f, // valid only if found
    target_zoom: u32, // the *ideal* (ceil()ed, see below) target zoom, valid regardless of found
    target_tile_id: TileId, // the *ideal* target tile id (before residency fallback), valid regardless of found
    resolved_zoom: u32, // the actually-resolved (resident) tile's zoom, valid only if found
    zoom_lo: u32, // round(desired_zoom_f), used by the debug view's own zoom-transition blend
    zoom_hi: u32, // min(zoom_lo + 1, max_zoom)
    blend_t: f32, // 0 = target is zoom_lo, 1 = target is zoom_hi, fractional inside the transition band
}

const SQRT2: f32 = 1.4142135623730951;

// Same shape as nucleus::tile::utils::refineFunctor / AabbDecorator's screen-space-error criterion,
// solved directly for the ideal zoom instead of walking candidates -- to_screen_space is a pure
// k/distance relation, so it inverts in closed form. Returns the continuous (unrounded) zoom so
// callers can blend between its floor/ceil across a transition instead of popping at round().
fn closed_form_target_zoom_f(distance: f32) -> f32 {
    let screen_px_per_meter = camera.viewport_size.y * 0.5 * camera.distance_scaling_factor / distance;
    let desired_pixel_size_m = settings.pixel_error_threshold / max(screen_px_per_meter, 1e-9) / SQRT2;
    let desired_tile_size_m = desired_pixel_size_m * f32(settings.tile_size);
    let desired_zoom = log2(EARTH_CIRCUMFERENCE / max(desired_tile_size_m, 1e-3));
    return clamp(desired_zoom, f32(settings.min_zoom), f32(settings.max_zoom));
}

// NOTE: we tried a second PER_TILE variant that corrected this closed-form guess by walking the
// hierarchy with the *real* refineFunctor/AabbDecorator test (nearest-point distance to a candidate
// tile's own rectangle, that tile's own world size) to get grid-aligned transitions instead of the
// circular iso-distance rings this closed-form guess produces. That test needs a height for the
// candidate tile, and the real AabbDecorator gets a stable per-tile height *range* from a coarse
// heightmap that isn't bound in this shader. Substituting a fixed sea-level reference made the
// decision coherent (no jagged elevation-contour artifacts) but wrong near real elevation extremes
// (e.g. close to the summit of the Grossglockner, still "thinks" it's at 0m and badly underestimates
// how close the camera actually is) -- i.e. strictly worse than just accepting this mode's rings. We
// can't faithfully reproduce the CPU refiner's result here without access to per-tile heights, so
// that variant was removed; PER_TILE stays this closed-form, per-pixel approximation only.

struct SingleTileResult {
    found: bool,
    color: vec4f, // valid only if found
    resolved_zoom: u32, // valid only if found
}

// Caps on how many dict_lookup probes resolve_single_tile may spend walking away from target_zoom
// in each direction -- without these, a pixel whose whole ancestor chain is non-resident (e.g. far
// outside any loaded region, or a source whose data doesn't go below some min_zoom) could cost up
// to max_zoom probes, each itself a 256-probe open-addressing search. Tune and rebuild to profile.
const MAX_ASCEND_JUMPS: u32 = 8u; // probes while walking target_zoom towards min_zoom (ancestors)
const MAX_DESCEND_JUMPS: u32 = 8u; // probes while walking target_zoom towards max_zoom (descendants)

// Jumps straight to target_zoom, then interleaves two independent walks away from it on a miss:
// one step toward the root (ancestor, decrease_zoom_level_by_one), one step toward the leaves
// (descendant, increase_zoom_level_by_one), alternating -- so the probe order is target, parent,
// child, grandparent, grandchild, ... Each direction is bounded by its own MAX_ASCEND_JUMPS /
// MAX_DESCEND_JUMPS budget and stops early at min_zoom / max_zoom respectively.
//
// The scheduler guarantees every ancestor of a resident tile is also resident, so the ascend side
// alone is normally sufficient (and sufficient to prove a hit there is the *deepest* resident
// ancestor). The descend side is a best-effort addition for the rare case (mostly cold start, or a
// jump budget too small for the actual gap) where no ancestor is found either -- it trades a
// guaranteed-correct answer for a bounded number of extra probes, on the assumption that some
// descendant of the target tile happening to be resident is still a better result than nothing.
// Both cursors start at the same tile and only ever move away from it, so they never probe the
// same tile twice; down_uv's fractional bits keep tracing the exact path towards render_uv at each
// finer level, same as increase_zoom_level_until does. Split out of resolve_tile_sample so the
// zoom-transition blend below can call it once (fast path, away from a transition) or twice (blend
// path, near one) without duplicating the walk.
fn resolve_single_tile(render_tile_id: TileId, render_uv: vec2f, target_zoom: u32) -> SingleTileResult {
    var result: SingleTileResult;
    result.found = false;
    result.color = vec4f(0.0);
    result.resolved_zoom = 0u;

    var up_id: TileId;
    var up_uv: vec2f;
    calc_tile_id_and_uv_for_zoom_level(render_tile_id, render_uv, target_zoom, &up_id, &up_uv);
    var down_id = up_id;
    var down_uv = up_uv;

    var layer: u32;
    if dict_lookup(up_id, &layer) {
        result.found = true;
        result.resolved_zoom = up_id.zoomlevel;
        result.color = textureSampleLevel(tile_texture, tile_sampler, up_uv, i32(layer), 0.0);
        return result;
    }

    var up_active = up_id.zoomlevel > settings.min_zoom;
    var down_active = down_id.zoomlevel < settings.max_zoom;
    var up_steps = 0u;
    var down_steps = 0u;

    loop {
        if !up_active && !down_active {
            break;
        }

        if up_active && up_steps < MAX_ASCEND_JUMPS {
            var parent_id: TileId;
            var parent_uv: vec2f;
            decrease_zoom_level_by_one(up_id, up_uv, &parent_id, &parent_uv);
            up_id = parent_id;
            up_uv = parent_uv;
            up_steps = up_steps + 1u;
            if dict_lookup(up_id, &layer) {
                result.found = true;
                result.resolved_zoom = up_id.zoomlevel;
                result.color = textureSampleLevel(tile_texture, tile_sampler, up_uv, i32(layer), 0.0);
                return result;
            }
            up_active = up_id.zoomlevel > settings.min_zoom;
        } else {
            up_active = false;
        }

        if down_active && down_steps < MAX_DESCEND_JUMPS {
            var child_id: TileId;
            var child_uv: vec2f;
            increase_zoom_level_by_one(down_id, down_uv, settings.max_zoom, &child_id, &child_uv);
            down_id = child_id;
            down_uv = child_uv;
            down_steps = down_steps + 1u;
            if dict_lookup(down_id, &layer) {
                result.found = true;
                result.resolved_zoom = down_id.zoomlevel;
                // No mipmaps on the tile array (GpuTileTextureArray::init: mipLevelCount = 1) -- the
                // discrete zoom choice above already is the "mip" selection, so always sample lod 0.
                result.color = textureSampleLevel(tile_texture, tile_sampler, down_uv, i32(layer), 0.0);
                return result;
            }
            down_active = down_id.zoomlevel < settings.max_zoom;
        } else {
            down_active = false;
        }
    }
    return result;
}

fn resolve_tile_sample(tci: vec2u, raw_depth: f32) -> ResolvedTileSample {
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

    // Continuous target zoom -- see Settings::zoom_selection_mode (SlippyTileOverlay.h) for the
    // per-pixel vs. per-tile comparison. Kept unrounded so floor/ceil can be cross-faded below
    // instead of popping at round().
    var desired_zoom_f: f32;
    if settings.zoom_selection_mode == ZOOM_SELECTION_MODE_PER_TILE {
        // Uses this pixel's own true distance, not an aggregate over the whole render tile, so it's
        // independent of the render/geometry tile's own granularity. XY comes analytically from
        // (render_tile_id, render_uv) -- exact, no depth precision loss -- Z from the depth buffer,
        // un-bent from curved to flat space (curvature only ever touches Z, see
        // apply_earth_curvature) so it lands in the same flat world frame calculate_bounds/
        // camera.position use.
        let world_xy = tile_uv_to_world_xy(render_tile_id, render_uv);
        let dims = vec2u(textureDimensions(depth_texture));
        let pos_cws_curved = camera_relative_pos_from_depth(tci, dims, raw_depth, camera.inv_view_proj_matrix);
        let d_sq = pos_cws_curved.x * pos_cws_curved.x + pos_cws_curved.y * pos_cws_curved.y;
        let world_z = camera.position.z + pos_cws_curved.z + earth_curvature_drop(d_sq, conf.planet_radius_m);
        let distance = max(1.0, length(vec3f(world_xy - camera.position.xy, world_z - camera.position.z)));
        desired_zoom_f = closed_form_target_zoom_f(distance);
    } else {
        let target_zoom_f = -derivatives - log2(f32(settings.tile_size) * settings.pixel_error_threshold);
        desired_zoom_f = clamp(target_zoom_f, f32(settings.min_zoom), f32(settings.max_zoom));
    }

    // Round *up*: at ceil(desired) the settings.tile_size texels of the tile are at most
    // pixel_error_threshold px apart, while round() would accept texels up to sqrt(2) too coarse.
    // Quad sources used to get this for free -- their array layer holds one zoom level more than the
    // id in the dictionary says, so a round()ed target missed the dictionary and the ancestor walk
    // landed on ceil anyway. Now that settings.tile_size is the layer's real texel count, the rule
    // has to be explicit, or single-tile (DemandScheduler) sources render half as sharp.
    let target_zoom = u32(clamp(ceil(desired_zoom_f), f32(settings.min_zoom), f32(settings.max_zoom)));
    // The choice above steps at every integer, so the cross-fade band straddles the *nearest* integer
    // boundary: zoom_lo below it, zoom_hi above it (blend_t agrees with target_zoom at both ends).
    let boundary = round(desired_zoom_f);
    let zoom_lo = u32(clamp(boundary, f32(settings.min_zoom), f32(settings.max_zoom)));
    let zoom_hi = min(zoom_lo + 1u, settings.max_zoom);
    let half_band = settings.zoom_blend_band * 0.5;
    let blend_t = smoothstep(-half_band, half_band, desired_zoom_f - boundary);

    result.target_zoom = target_zoom;
    result.zoom_lo = zoom_lo;
    result.zoom_hi = zoom_hi;
    result.blend_t = blend_t;

    // target_tile_id always reflects the target zoom, independent of whether we end up blending
    // the actual sampled color below (debug view DEBUG_VIEW_TARGET_TILE_ID relies on this).
    var target_id: TileId;
    var target_uv: vec2f;
    calc_tile_id_and_uv_for_zoom_level(render_tile_id, render_uv, target_zoom, &target_id, &target_uv);
    result.target_tile_id = target_id;

    // DATA_MODE_NORMALS(_OVERWRITE) can't be cross-faded with a plain color lerp -- that would need
    // decoding both encoded normals, averaging, renormalizing and re-encoding -- so they always take
    // the single-resolve path below, same as when the toggle is off or we're outside the band.
    let blendable = settings.blend_zoom_transitions != 0u
        && settings.data_mode != DATA_MODE_NORMALS && settings.data_mode != DATA_MODE_NORMALS_OVERWRITE;

    if !blendable || blend_t <= 0.0 || blend_t >= 1.0 {
        let r = resolve_single_tile(render_tile_id, render_uv, target_zoom);
        result.found = r.found;
        result.color = r.color;
        result.resolved_zoom = r.resolved_zoom;
    } else {
        let lo = resolve_single_tile(render_tile_id, render_uv, zoom_lo);
        let hi = resolve_single_tile(render_tile_id, render_uv, zoom_hi);
        if lo.found && hi.found {
            result.found = true;
            result.color = mix(lo.color, hi.color, blend_t);
            // Ambiguous once two different resident levels are blended together -- only used by
            // DEBUG_VIEW_ZOOM_LEVEL, which isn't part of this blend (that debug view still reads as
            // "whichever side currently has more weight").
            result.resolved_zoom = select(lo.resolved_zoom, hi.resolved_zoom, blend_t >= 0.5);
        } else if lo.found || hi.found {
            // Only one side is actually resident (e.g. the other's ancestor fallback also missed) --
            // just show it fully rather than fading toward nothing.
            result.found = true;
            result.color = select(hi.color, lo.color, lo.found);
            result.resolved_zoom = select(hi.resolved_zoom, lo.resolved_zoom, lo.found);
        }
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

    let resolved = resolve_tile_sample(tci, raw_depth);

    let stride = settings.wanted_tiles_stride;
    if stride > 0u && (tci.x % stride) == 0u && (tci.y % stride) == 0u {
        record_wanted_tile(resolved.target_tile_id);
    }

    // Debug visualization of the *ideal* target zoom itself -- color-coded, fully opaque, and
    // computed before any residency resolution, so it shows what zoom we'd *like* to sample
    // regardless of which tiles are actually loaded (unlike DEBUG_VIEW_ZOOM_LEVEL, which colors the
    // resolved/resident tile after the ancestor walk).
    if settings.debug_view == DEBUG_VIEW_TARGET_ZOOM_LEVEL {
        var color = zoom_level_color(resolved.target_zoom);
        if settings.blend_zoom_transitions != 0u {
            // Cross-fades the zoom-level color ramp itself, so the transition band set up in
            // resolve_tile_sample is visible directly (a smooth gradient instead of a hard edge).
            color = mix(zoom_level_color(resolved.zoom_lo), zoom_level_color(resolved.zoom_hi), resolved.blend_t);
        }
        textureStore(output_texture, tci, vec4f(color, 1.0));
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
    var result = src + bg * (1.0 - src.a);

    let target_id = resolved.target_tile_id;
    if target_id.zoomlevel == settings.highlight_zoom && target_id.x == settings.highlight_x && target_id.y == settings.highlight_y {
        result = vec4f(vec3f(1.0, 0.0, 0.0) * 0.7, 0.7) + result * 0.3;
    }
    textureStore(output_texture, tci, result);
}
