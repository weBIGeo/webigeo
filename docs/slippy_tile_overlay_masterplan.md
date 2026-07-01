# SlippyTileOverlay — Masterplan

Status: Plans 1-4 shipped (verified 2026-07-01). Branch: `feature/slippy-tile-overlay`.

## Goal

Introduce a **`SlippyTileOverlay`**: an overlay that takes a tile source (a custom URL or a
predefined one, e.g. the ortho imagery) and paints it onto the terrain. Multiple sources can be
added and stacked at runtime. To get there we decouple imagery fetching from the terrain-mesh
stage, so `render_tiles` only rasterizes the height geometry, and imagery becomes overlay-driven.

This is a series of plans, executed one at a time. The user builds/pushes between plans.

## Current architecture (baseline)

- **[render_tiles.wgsl](../webgpu/engine/shaders/render_tiles.wgsl)** rasterizes the height-tile mesh
  *and* samples ortho, writing a 4-slot gbuffer:
  - slot 0 `albedo` — `R32Uint`, packed ortho rgb
  - slot 1 `position` — `RGBA32Float`, `pos_cws.xyz + dist` (web-mercator world position)
  - slot 2 `normal` — `RG16Uint`, oct-encoded
  - slot 3 `overlay` — `R32Uint`, packed debug overlay color
- **[TileMeshRenderer](../webgpu/engine/tile_mesh/TileMeshRenderer.h)** owns *both* the height
  texture array and the ortho texture array, fed by two schedulers
  (`update_gpu_tiles_height` / `update_gpu_tiles_ortho`).
- **Two independent "tile lists":**
  - *Scheduler* list — what to load. Each scheduler runs its own
    `quads_for_current_camera_position()` (`onTheFlyTraverse` + `refineFunctor`) on the scheduler
    thread, throttled to ~100 ms, clamped to its own `max_zoom_level` (geometry 18, texture 20).
  - *Renderer* draw list — what to draw. Rebuilt every frame on the render thread in
    [Window::paint](../webgpu/engine/Window.cpp) (`generate_list → limit → cull → sort`), picking
    from whatever tiles are resident.
- **Schedulers, the scheduler `QThread`, and the `SchedulerDirector` all live in `webgpu_app`**
  ([RenderingContext.cpp](../apps/webgpu_app/RenderingContext.cpp)); camera→`update_camera` and
  scheduler→renderer wiring is in [App.cpp](../apps/webgpu_app/App.cpp). Overlays live in the engine.
- **Overlays** are screen-space ping-pong passes split into **pre-shading (`z_index < 0`)** and
  **post-shading (`z_index >= 0`)** buckets ([OverlayRenderer](../webgpu/engine/overlay/OverlayRenderer.cpp)).
  The **[compose pass](../webgpu/engine/shaders/compose_pass.wgsl)** already:
  - blends the **pre** bucket into `albedo` *before* lighting, then lights it
    (`albedo = albedo * (1 - pre.a) + pre.rgb`), and
  - blends the **post** bucket over the shaded color *after* lighting.

## Key decisions

1. **Two separate tracks.** The `SlippyTileOverlay` feature is the priority; the gbuffer/compute work
   is a separate, later track.
2. **Ownership: engine-owned.** Tile-loading machinery moves into the engine `Context` so overlays can
   own their sources. The app shrinks to *configuring* sources (URLs/patterns). This is what makes
   "add/stack as many sources as you want at runtime" work without app-side rewiring.
3. **Lighting: no overlay-side lighting.** The default imagery overlay lives in the **pre-shading
   bucket** and writes premultiplied color only. The existing compose pass blends it into albedo and
   lights it — so imagery stays lit exactly as today, with zero lighting math in the overlay.
4. **Stacking model.** Each `SlippyTileOverlay` exposes **bucket (pre = lit basemap imagery /
   post = unlit data layer) + opacity + z-order**. Multiple pre-shading sources blend into albedo in
   z-order and get lit once; post-shading sources paint on top unlit.
5. **Height/geometry mesh is untouched.** It keeps its dedicated `GeometryScheduler` feeding
   `TileMeshRenderer`. Only ortho + future imagery become overlay-driven `TileSource`s. The shared
   traversal must cover geometry + all overlay sources together.
6. **gbuffer tile-id: dropped.** The position slot already stores web-mercator world position; the
   slippy tile id at any zoom is derived arithmetically (mercator→x/y/z). Storing tile-id is redundant.
7. **gbuffer dx/dy: deferred to the compute track only.** A *fragment* overlay gets screen-space
   derivatives for free, so `textureSampleGrad` already does correct mip/anisotropy. dx/dy in the
   gbuffer only earns its keep for a **compute** overlay (no automatic derivatives): precompute the
   pixel footprint during rasterization and store it, so the compute shader can pick the right LOD —
   which for slippy data means the right *zoom level* in the tile pyramid plus the mip within a
   resident tile. Pure quality feature; not needed for v1.
8. **Shared traversal = option (a).** Keep loading in the schedulers (async, throttled, quad
   granularity); compute the visible quadtree **once** (in the director) and distribute to each
   scheduler, which clamps to its own max zoom. The per-frame Window draw list stays a separate,
   render-only concern. Loading is *not* driven from the per-frame render list.

## Net effect on the gbuffer

The whole gbuffer-format change collapses to **dropping the albedo slot** (Plan 6); the position slot
already does the tile-derivation work. dx/dy is added *only if/when* we go compute (Plan 7).

## Done

### Plan 1 — Extract reusable tile-array provider (done)
Pulled the ortho `GpuArrayHelper` + texture-array + tile-id→layer dictionary out of
`TileMeshRenderer` into a standalone component (`GpuTileTextureArray`/`TileSource`).

### Plan 2 — `TileSource` + move ownership into engine `Context` (done)
`TileSource` = { `TextureScheduler` + provider from Plan 1 + director registration }, owned by
engine `Context` (`Context::add_tile_source`/`get_or_create_tile_source`). App configures sources
via `TileSourcePresets`.

### Plan 3 — Pre-shading `SlippyTileOverlay`, default ortho (done, shipped as compute not fragment)
Ortho sampling removed from `render_tiles.wgsl`; `SlippyTileOverlay` derives the tile from the
gbuffer and writes premultiplied color into the pre bucket. **Deviation from the original plan:**
this shipped as a **compute** overlay (per-pixel dictionary lookup, no fragment derivatives)
instead of the fragment-shader design sketched below — which is why it needed its own zoom/LOD
logic rather than getting mip selection "for free." That logic had two bugs, fixed 2026-07-01: (1)
it capped the lookup zoom at the height-mesh's own (lower) zoom instead of estimating an
independent ideal zoom from camera distance, and (2) it re-derived tile/uv from the reconstructed
absolute world position in `f32`, which floors at a few meters of precision at global scale and
produced visible jitter/blockiness at high zoom. Fixed by adding an exact `tile_ref` (render
tile_id + local uv) gbuffer attachment as a precision anchor, and a per-fragment,
per-overlay-tunable (`pixel_error_threshold`) ideal-zoom estimate from camera distance alone.

### Plan 4 — UI: add / remove / reorder / stack tile sources (done)
`OverlaysPanel` supports add/remove/reorder and stacking multiple `SlippyTileOverlay` instances,
each with its own source combo, opacity, max zoom, and pixel-error-threshold settings.

## Plans

Executed one at a time.

### Plan 5 — Shared traversal
Director computes the visible quadtree once per camera update and distributes to all schedulers (each
clamps to its own max zoom). Replaces the N independent `quads_for_current_camera_position` calls.
Optimization, valuable once N sources exist.

### Plan 6 — Drop the albedo gbuffer slot
Compose uses `pre_overlay_color` as the albedo. gbuffer becomes position + normal (+ debug).

### Plan 7 — Compute track (separate, later)
gbuffer dx/dy → compute `SlippyTileOverlay` → convert the existing `TextureOverlay` to compute.
Note: the overlay already shipped as compute (Plan 3), and the 2026-07-01 fix gave it a distance-based
ideal-zoom estimate (no derivatives needed) that resolved the motivating precision/resolution bug. What's
left here is a *finer-grained* LOD (true per-pixel anisotropic footprint via rasterized dx/dy) rather than
a single scalar zoom estimate — a quality refinement, not a bug fix, if still wanted.

### Plan 8 — Move the height/geometry scheduler into the engine `Context`
Plan 2 deliberately left the geometry (height) scheduler, its `DataQuerier`, and the cloud scheduler in
`RenderingContext` (imagery-only move). This plan finishes the job: move the `GeometryScheduler` (and the
`DataQuerier` built from its `ram_cache`) into the engine `Context`, so all tile loading is engine-owned
and the app runs on a single scheduler thread instead of two. Requires re-plumbing the camera→geometry
`update_camera` wiring and the `DataQuerier` handed to the camera controller (App.cpp:279), plus deciding
whether the cloud scheduler moves too. `TileMeshRenderer` keeps consuming the geometry array via the same
`GpuTileTextureArray` mechanism. Optional consolidation, not required by the overlay feature.

### Plan 9 — Perf-test & tune the per-pixel dictionary lookup
The overlay resolves tile→layer with a per-pixel hashmap lookup (open-addressing over a 16-bit tile hash).
Plan 3 ships it as **two 256×256 textures** — `packed_ids` (`RG32Uint`, keys) + `layers` (`R16Uint`,
values) from `GpuArrayHelper::generate_dictionary()` — chosen for reuse, not measured. This plan profiles
that lookup (worst case: distant terrain, deep walk-ups, many overlapped sources) and evaluates cheaper
layouts: (a) a **single combined `RGBA32Uint`** texture (id in `.rg`, layer in `.b` → one fetch/probe,
one binding); (b) **storage buffers** (`array<u32vec2>` keys + `array<u32>` values), matching the
buffer-based `webgpu/compute/shaders/tile_hashmap.wgsl` already in the codebase. Switch if measurements
justify it. Depends on Plan 3 (and ideally Plan 4 for the multi-source case). The 2026-07-01 fix's
distance-based ideal-zoom estimate should keep the per-pixel walk to 0-1 iterations in the common
case (rather than always walking from `max_zoom` down), which lowers the urgency here but doesn't
replace the need to actually measure it.

### Plan 10 — Derive `position.xy` from `tile_ref`, shrink the `position` gbuffer attachment
Now that the gbuffer carries the render tile's exact `tile_id`+`uv` (added by the 2026-07-01 fix),
`position.xy` (camera-relative world X/Y) can be reconstructed exactly via `calculate_bounds(tile_id)`
(`tile_util.wgsl`) interpolated by `uv`, instead of being stored per-pixel — dropping 8 of the 16
bytes/pixel the `position` attachment costs today. Caveat: `position.z` (camera-relative altitude,
produced by height-texture sampling in the vertex shader) and the "no-geometry background pixel"
sentinel are **not** derivable from `tile_id` alone and must still be stored — e.g. shrink `position`
from `RGBA32Float` (16 B/px) to a single `R32Float` (4 B/px) carrying altitude, with a reserved
sentinel value (mirroring `EMPTY_TILE_ZOOMLEVEL` in `tile_util.wgsl`) taking over the "no geometry
here" signal that today's `length(pos_cws) <= 0.0` check relies on. Net win is real but smaller than
"delete `position` entirely" — quantify during implementation. Depends on Plan 3's `tile_ref`
addition (done).

## Dependency / ordering summary

```
1 ──► 2 ──► 3 ──► 4
            │
            ├──► 5  (shared traversal, optimization)
            ├──► 6  (drop albedo) ──► 7 (compute track: finer LOD)
            ├──► 8  (move height scheduler into Context, cleanup)
            ├──► 9  (perf-test & tune the dictionary lookup)
            └──► 10 (derive position.xy from tile_ref, shrink position attachment)
```
