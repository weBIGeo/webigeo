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

Executed one at a time; the repo is built/pushed between plans.

### Plan 1 — Isotropic `derivatives` attachment (additive)

Add a standalone `R32Uint` "derivatives" attachment (gbuffer index 5) written by the rasterizer.
Purely additive — no existing slot changes — so it lands and pushes independently. (Plan 2 folds it,
plus `uv` and the new `id`, into the single `RG32Uint tile_ref`.) The value is an
**overlay-independent** `log2` footprint scalar in **global mercator-normalized** units, so any
overlay/zoom decodes it with a single add.

Rasterizer computation in `fragmentMain`
([render_tiles.wgsl](../webgpu/engine/shaders/render_tiles.wgsl), uniform control flow — derivatives
are already used there for `normal_by_fragment_position_interpolation`):

```wgsl
let inv = 1.0 / exp2(f32(tile_id.zoomlevel));    // render-tile uv -> global normalized
let dgdx = dpdx(vertex_out.uv) * inv;
let dgdy = dpdy(vertex_out.uv) * inv;
let foot = max(length(dgdx), length(dgdy));      // conservative isotropic footprint (max axis)
frag_out.derivatives = pack_derivatives(log2(max(foot, 1e-30)));
```

Add `pack_derivatives` / `unpack_derivatives` to [encoder.wgsl](../webgpu/base/shaders/encoder.wgsl)
(home of `range_to_u32`), with `DERIVATIVES_LOG2_RANGE = vec2f(-48.0, 0.0)`: quantize `log2_foot` to
16 bits over that range. In this additive plan it occupies the low 16 bits of its own `u32`; in Plan
2 the same 16-bit value moves to the **high 16 bits** of `tile_ref.g` (`q << 16`), with `id` in the
low 16.

Future consumption (documented for Plan 2's overlay): for a tile sampled at zoom `Z`,
`lod = log2_foot + f32(Z)` feeds `textureSampleLevel` (global-normalized → tile-uv space is a factor
`exp2(Z)`, i.e. `+Z` in `log2`). This fixes the current `textureSampleLevel(..., 0.0)` aliasing in
the overlay.

C++ wiring (all additive): append the color format in
[TileMeshRenderer.cpp](../webgpu/engine/tile_mesh/TileMeshRenderer.cpp) (the `m_gbuffer` inherits it
via `framebuffer_format()`, and `Framebuffer` allocates + clears it with the rest); add
`@location(5) derivatives: u32` to `FragOut`; bump
`min_required_max_color_attachment_bytes_per_sample` 44 → 48 in
[Window.cpp](../webgpu/engine/Window.cpp) (temporary; Plan 2 brings it to 32, Plan 4 to 12).

**Verify:** build + run; app still initializes (device meets 48 B/sample), terrain unchanged. Add a
temporary `gbuffer_debug` mode decoding `derivatives` (`log2` as grayscale) and confirm it varies
smoothly with distance / grazing angle and is stable under camera motion.

### Plan 2 — Frame-local tile-id + per-overlay side table (consolidate `tile_ref` → `RG32Uint`)

Replace the per-pixel `x/y/z` tile id and the GPU hashmap/walk with a 2-byte frame-local id +
CPU-resolved side tables, and collapse `uv` + `id` + `derivatives` into the single `RG32Uint`
`tile_ref` (dropping the old `RGBA32Uint tile_ref` and the standalone Plan 1 attachment → gbuffer
back to 32 B).

- **Shared, once per frame (render thread):** each drawn render tile gets a dense id `0..N-1`
  (N ≤ 1024, the `limit()` cap in [Window.cpp](../webgpu/engine/Window.cpp)) = its index in the
  culled draw list. Passed to the rasterizer as a flat vertex attribute (mirrors the existing
  `tile_id` flat attribute) and written into `tile_ref.g` low 16 bits. Background pixels write a
  reserved sentinel (`0xFFFF`).
- **Per overlay (rebuilt on draw-list / residency change):** a scalar `R` (decision 4) + a block of
  `4^max(R,0)` `(tile_array_index: u16, delta_uv)` entries per render tile — the CPU resolves each
  sub-cell against that source's tile→layer dictionary (walking to a resident ancestor on a miss;
  `delta_uv` remaps the sub-cell uv into the resident tile). Uploaded as a storage buffer (matches
  the buffer-based [tile_hashmap.wgsl](../webgpu/compute/shaders/tile_hashmap.wgsl); replaces the
  256×256 `dict_ids`/`dict_layers` textures).
- **GPU per pixel (no hashmap, no walk):** load `tile_ref` once → `uv`, `id`, `derivatives` → overlay
  `R` → sub-cell index from `uv` → one indexed fetch → `textureSampleLevel` at
  `lod = derivatives + target_zoom`. `R < 0` collapses the block to one entry.

Reuse the existing `tile_util.wgsl` arithmetic helpers (`calc_tile_id_and_uv_for_zoom_level`,
`decrease_zoom_level_by_one`) for CPU-side block resolution. Update `SlippyTileOverlay`
([.wgsl](../webgpu/engine/shaders/overlays/slippy_tile_overlay.wgsl) /
[.h](../webgpu/engine/overlay/SlippyTileOverlay.h)): drop `dict_ids`/`dict_layers` + `dict_lookup` +
the walk; add the side-table buffer + `R`; read the new packed `tile_ref`.

Worst-case CPU cost: `1024 × ~16 × N` dictionary probes, only on change — single-digit ms at N=4,
usually far less.

**Verify:** stack 2-3 sources with different `max_zoom` / `pixel_error_threshold`; imagery matches
current output, is sharper close up (footprint mip), each honors its own quality (independent `R`),
no per-pixel hashmap artifacts on distant terrain.

### Plan 3 — Drop the `position` attachment

Reconstruct `pos_cws` from the depth buffer (`camera_relative_pos_from_depth`) everywhere `position`
is read, then remove the 16 B slot:

- [compose_pass.wgsl](../webgpu/engine/shaders/compose_pass.wgsl): geometry mask (`dist>0` → depth ≠
  far), cloud shadows, atmosphere `view_height`, curvature-corrected normal.
- [gbuffer_debug.wgsl](../webgpu/engine/shaders/overlays/gbuffer_debug.wgsl): move the
  `alpha = geometry` mask to depth; position-buffer modes (5,6,11) read the reconstruction.
- Audit remaining gbuffer readers (e.g. `TextureOverlay`, `TrackRenderer`) and repoint to depth /
  `tile_ref`.

**Verify:** lighting, cloud shadows, and the sky/background mask unchanged at matched camera
positions (near / mid / distant horizon).

### Plan 4 — Drop `albedo` + debug `overlay` slots, finalize layout

Compose uses `pre_overlay_color` as the albedo (drops the `albedo_texture` binding); move any
remaining use of the debug `overlay` slot's `alpha = geometry` mask to depth. Collapse to the final
2-attachment / 12 B layout (`normal` + `tile_ref`) and lower
`min_required_max_color_attachment_bytes_per_sample` to 12 in
[Window.cpp](../webgpu/engine/Window.cpp).

**Verify:** full scene (terrain + stacked overlays + debug modes) renders correctly; device reports
the lower bytes-per-sample requirement; app still initializes on hardware with
`maxColorAttachmentBytesPerSample` near 32.

### Plan 5 (optional) — Move the height/geometry scheduler into the engine `Context`

Not required by the gbuffer work; a consolidation. Today the geometry (height) scheduler, its
`DataQuerier`, and the cloud scheduler still live in `RenderingContext` while imagery loading is
engine-owned. This plan moves the `GeometryScheduler` (and the `DataQuerier` built from its
`ram_cache`) into `Context` so all tile loading is engine-owned and the app runs on a single
scheduler thread. Requires re-plumbing the camera→geometry `update_camera` wiring and the
`DataQuerier` handed to the camera controller, plus deciding whether the cloud scheduler moves too.
`TileMeshRenderer` keeps consuming the geometry array via the same provider mechanism.

## Dependency / ordering

```
Plan 1 (derivatives) ──► Plan 2 (frame-local id + side table, tile_ref→RG32Uint) ──► Plan 3 (drop position) ──► Plan 4 (finalize)
Plan 5 (scheduler consolidation) — independent, optional
```
