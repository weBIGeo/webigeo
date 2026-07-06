# SlippyTileOverlay — Masterplan

Status: `SlippyTileOverlay` shipped and stacking in the UI. Active track: **GBuffer compression**.
Branch: `feature/slippy-tile-overlay`.

## Goal

`SlippyTileOverlay` paints one or more tile sources (ortho imagery, custom URLs) onto the terrain,
stackable at runtime. Imagery is decoupled from the height-mesh stage: `render_tiles` only
rasterizes geometry into the gbuffer, and each overlay samples its own source in a screen-space
compute pass. With that shipped, the remaining track is to **shrink the gbuffer** (44 B/sample
today) — the fat per-pixel payload it carries for imagery is no longer needed in that form.

## Baseline (shipped)

- **Imagery decoupled from the mesh.** `render_tiles.wgsl` rasterizes height geometry only; ortho /
  imagery is painted by `SlippyTileOverlay`, a **compute** overlay that per pixel reads the gbuffer's
  exact render `tile_id`+`uv` (`tile_ref`), estimates an ideal zoom from camera distance
  (screen-space error, independent of the mesh's zoom), resolves the tile's array layer via a
  per-pixel GPU dictionary (open-addressing hashmap + ancestor walk), and blends premultiplied into
  the pre-shading bucket so the compose pass lights it like today's imagery.
- **Engine-owned tile loading (imagery).** `TileSource` = `TextureScheduler` + tile-array provider +
  director registration, owned by `Context` (`add_tile_source` / `get_or_create_tile_source`). The
  app only configures sources (URLs/patterns) via `TileSourcePresets`.
- **Stacking UI.** `OverlaysPanel` supports add / remove / reorder / stack of multiple
  `SlippyTileOverlay` instances, each with its own source, opacity, max zoom, and
  pixel-error-threshold.
- **Compose model.** Overlays are screen-space ping-pong passes split into pre-shading
  (`z_index < 0`) and post-shading (`z_index >= 0`) buckets; the compose pass blends the pre bucket
  into albedo *before* lighting and the post bucket over the shaded color *after*.

## Current gbuffer (baseline) — 44 B/sample, 5 attachments

| slot | format | bytes | contents |
|---|---|---|---|
| 0 albedo | R32Uint | 4 | neutral white (imagery moved to the overlay) |
| 1 position | RGBA32Float | 16 | `pos_cws.xyz` + `w = render zoom` |
| 2 normal | RG16Uint | 4 | oct-encoded terrain normal |
| 3 overlay | R32Uint | 4 | packed debug color + `alpha = geometry` mask |
| 4 tile_ref | RGBA32Uint | 16 | render `tile_id.x, y, z` + `pack2x16unorm(uv)` |

Two 16-byte slots dominate (`position`, `tile_ref`), forcing a device requirement of
`maxColorAttachmentBytesPerSample >= 44` ([Window.cpp](../webgpu/engine/Window.cpp) — the standing
TODO wants back under 32).

## Design decisions

1. **Engine-owned sources; app only configures.** (Shipped.)
2. **No overlay-side lighting.** Pre-shading overlays write premultiplied color; the compose pass
   folds it into albedo and lights once. (Shipped.)
3. **No per-pixel GPU tile lookup.** Tile → resident-array-layer is resolved on the **CPU** (which
   already owns the dictionary) into a compact per-overlay side table; the GPU does one indexed
   fetch. This removes the hashmap + ancestor walk entirely — so there is no hashmap layout to
   perf-tune.
4. **Independent per-overlay quality via a scalar refinement offset `R`.** An overlay's ideal zoom is
   the render tile's zoom + a per-frame scalar `R` (the `distance` term cancels between mesh SSE and
   overlay SSE, so `R` is not per-pixel); `R` stays small (typically 0-2) and `max_zoom` only clamps
   it near the camera. A per-render-tile block of `4^max(R,0)` entries lets ortho go **finer** than
   the render tile without capping at mesh zoom.
5. **Precision anchor stays exact.** Per pixel we keep the render tile's `uv`; the tile id becomes a
   small **frame-local index** into the side table. Geographic derivations use `id`+`uv`, never the
   lossy absolute world position.
6. **Position is reconstructable from depth.** Distance, altitude, and the no-geometry mask come from
   the depth buffer (already used in [gbuffer_debug.wgsl](../webgpu/engine/shaders/overlays/gbuffer_debug.wgsl)
   modes 9-11 via `camera_relative_pos_from_depth`), so the 16 B `position` slot can go.
7. **Isotropic `derivatives`, precomputed in the rasterizer.** The `render_tiles` fragment shader has
   automatic derivatives, so it stores a per-pixel `log2` footprint scalar (the `derivatives` field)
   the compute overlays use for correct mip selection — which they otherwise can't get (no fragment
   derivatives in a compute pass). Isotropic only for now: a full anisotropic ellipse costs more to
   compute/pack and nothing consumes it yet. The name `derivatives` leaves room to upgrade the field
   to real anisotropic `ddx`/`ddy` gradients later if grazing-angle aliasing proves objectionable.
8. **`id` + `uv` + `derivatives` share one texture.** They are always consumed together by the
   overlay, so they live in a single `RG32Uint` (`tile_ref`): `.r` = `uv`, `.g` = `id | derivatives`.
   One `textureLoad` instead of three, and the whole thing is 8 B.

## Target gbuffer — 12 B/sample, 2 attachments

| slot | format | bytes | contents |
|---|---|---|---|
| 0 normal | RG16Uint | 4 | oct-encoded terrain normal (unchanged) |
| 1 tile_ref | RG32Uint | 8 | `.r` = `pack2x16unorm(uv)`; `.g` = frame-local `id` (low 16) `\| log2 derivatives` (high 16) |

`position`, `albedo`, the debug `overlay` slot, and the render tile's `x/y/z` are all gone; the depth
buffer (not a color attachment → free) covers what `position` provided.

## Plans

Executed one at a time; the repo is built/pushed between plans. The gbuffer shrinks monotonically
(44 → 28 → 20 → 12 B) — no temporary attachment or byte bump. The involved indirection rework is
deliberately done **last** (Plan 3), after the cheap slot removals de-risk the device requirement.

### Plan 1 — Drop the `position` attachment

Reconstruct `pos_cws` from the depth buffer (`camera_relative_pos_from_depth`) everywhere `position`
is read, then remove the 16 B slot (44 → 28 B; lower the device requirement to match):

- [compose_pass.wgsl](../webgpu/engine/shaders/compose_pass.wgsl): geometry mask (`dist>0` → depth ≠
  far), cloud shadows, atmosphere `view_height`, curvature-corrected normal.
- [gbuffer_debug.wgsl](../webgpu/engine/shaders/overlays/gbuffer_debug.wgsl): move the
  `alpha = geometry` mask to depth; position-buffer modes (5,6,11) read the reconstruction.
- [slippy_tile_overlay.wgsl](../webgpu/engine/shaders/overlays/slippy_tile_overlay.wgsl): replace the
  `position_texture` reads — the `distance` for `ideal_zoom` and the `length(pos_cws) <= 0`
  background test — with a depth-derived distance and a `depth == far` background test. The existing
  hashmap / `tile_ref` (RGBA32Uint) path is otherwise unchanged in this plan.
- Audit remaining gbuffer readers (e.g. `TextureOverlay`, `TrackRenderer`) and repoint to depth.

**Verify:** lighting, cloud shadows, and the sky/background mask unchanged at matched camera
positions (near / mid / distant horizon).

### Plan 2 — Drop `albedo` + debug `overlay` slots

After Plan 1 the attachments are `albedo`, `normal`, `overlay`, `tile_ref`. Drop the two 4 B debug
slots (28 → 20 B, 2 attachments = `normal` + `tile_ref` RGBA32Uint; lower the requirement to match):

- Compose uses `pre_overlay_color` as the albedo — drop the `albedo_texture` binding
  ([compose_pass.wgsl](../webgpu/engine/shaders/compose_pass.wgsl)).
- Move any remaining use of the debug `overlay` slot's `alpha = geometry` mask to depth; fold the
  in-rasterizer debug-overlay writes into the `gbuffer_debug` compute path where needed.

**Verify:** full scene (terrain + stacked overlays + all debug modes) renders correctly; device
reports the lower bytes-per-sample requirement.

### Plan 3 — Frame-local tile-id + per-overlay side table + isotropic `derivatives`

The main rework, done last. Replace the per-pixel `x/y/z` tile id **and** the GPU hashmap/walk with a
2-byte frame-local id + CPU-resolved side tables, add the rasterizer-computed `derivatives`, and
collapse `uv` + `id` + `derivatives` into a single `RG32Uint tile_ref` (`.r` = `uv`,
`.g` = `id` low 16 `|` `derivatives` high 16). This drops `tile_ref` RGBA32Uint → RG32Uint, reaching
the final **12 B / 2 attachments** (`normal` + `tile_ref`); lower the requirement to 12.

**Derivatives (isotropic footprint), computed in the rasterizer** `fragmentMain`
([render_tiles.wgsl](../webgpu/engine/shaders/render_tiles.wgsl), uniform control flow — derivatives
are already used there for `normal_by_fragment_position_interpolation`):

```wgsl
let inv = 1.0 / exp2(f32(tile_id.zoomlevel));    // render-tile uv -> global normalized
let dgdx = dpdx(vertex_out.uv) * inv;
let dgdy = dpdy(vertex_out.uv) * inv;
let foot = max(length(dgdx), length(dgdy));      // conservative isotropic footprint (max axis)
let deriv16 = pack_derivatives(log2(max(foot, 1e-30)));  // 16-bit fixed-point over DERIVATIVES_LOG2_RANGE
frag_out.tile_ref = vec2u(pack2x16unorm(vertex_out.uv), (deriv16 << 16u) | frame_local_id);
```

Add `pack_derivatives` / `unpack_derivatives` to [encoder.wgsl](../webgpu/base/shaders/encoder.wgsl)
(home of `range_to_u32`), with `DERIVATIVES_LOG2_RANGE = vec2f(-48.0, 0.0)` — an
**overlay-independent** `log2` footprint in global mercator-normalized units.

**Frame-local id (shared, once per frame, render thread):** each drawn render tile gets a dense id
`0..N-1` (N ≤ 1024, the `limit()` cap in [Window.cpp](../webgpu/engine/Window.cpp)) = its index in
the culled draw list. Passed to the rasterizer as a flat vertex attribute (mirrors the existing
`tile_id` flat attribute) and written into `tile_ref.g` low 16 bits. Background pixels write a
reserved sentinel (`0xFFFF`).

**Per-overlay side table (rebuilt on draw-list / residency change):** a scalar `R` (decision 4) + a
block of `4^max(R,0)` `(tile_array_index: u16, delta_uv)` entries per render tile — the CPU resolves
each sub-cell against that source's tile→layer dictionary (walking to a resident ancestor on a miss;
`delta_uv` remaps the sub-cell uv into the resident tile). Uploaded as a storage buffer (matches the
buffer-based [tile_hashmap.wgsl](../webgpu/compute/shaders/tile_hashmap.wgsl); replaces the 256×256
`dict_ids`/`dict_layers` textures). Worst-case CPU cost `1024 × ~16 × N` dictionary probes, only on
change — single-digit ms at N=4, usually far less.

**GPU per pixel (no hashmap, no walk):** load `tile_ref` once → `uv`, `id`, `derivatives` → overlay
`R` → sub-cell index from `uv` → one indexed fetch → `textureSampleLevel` at
`lod = derivatives + target_zoom` (global-normalized → tile-uv space is a factor `exp2(Z)`, i.e. `+Z`
in `log2` — fixes the current `textureSampleLevel(..., 0.0)` aliasing). `R < 0` collapses the block
to one entry.

Reuse the existing `tile_util.wgsl` arithmetic helpers (`calc_tile_id_and_uv_for_zoom_level`,
`decrease_zoom_level_by_one`) for CPU-side block resolution. Update `SlippyTileOverlay`
([.wgsl](../webgpu/engine/shaders/overlays/slippy_tile_overlay.wgsl) /
[.h](../webgpu/engine/overlay/SlippyTileOverlay.h)): drop `dict_ids`/`dict_layers` + `dict_lookup` +
the walk; add the side-table buffer + `R`; read the new packed `RG32Uint tile_ref`.

**Verify:** stack 2-3 sources with different `max_zoom` / `pixel_error_threshold`; imagery matches
current output, is sharper close up (footprint mip), each honors its own quality (independent `R`),
no per-pixel hashmap artifacts on distant terrain. Add a temporary `gbuffer_debug` mode decoding
`derivatives` (grayscale) and the frame-local id (flat per tile) to confirm both are stable under
camera motion. Confirm the device now reports 12 B/sample and still initializes on hardware with
`maxColorAttachmentBytesPerSample` near 32.

### Plan 4 (optional) — Move the height/geometry scheduler into the engine `Context`

Not required by the gbuffer work; a consolidation. Today the geometry (height) scheduler, its
`DataQuerier`, and the cloud scheduler still live in `RenderingContext` while imagery loading is
engine-owned. This plan moves the `GeometryScheduler` (and the `DataQuerier` built from its
`ram_cache`) into `Context` so all tile loading is engine-owned and the app runs on a single
scheduler thread. Requires re-plumbing the camera→geometry `update_camera` wiring and the
`DataQuerier` handed to the camera controller, plus deciding whether the cloud scheduler moves too.
`TileMeshRenderer` keeps consuming the geometry array via the same provider mechanism.

## Dependency / ordering

```
Plan 1 (drop position) ──► Plan 2 (drop albedo + debug overlay) ──► Plan 3 (frame-local id + side table + derivatives, tile_ref→RG32Uint)
Plan 4 (scheduler consolidation) — independent, optional
```
