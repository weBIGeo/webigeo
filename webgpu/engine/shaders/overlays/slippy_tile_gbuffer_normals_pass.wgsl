/*****************************************************************************
 * weBIGeo
 * Copyright (C) 2026 Gerald Kimmersdorfer
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

// DataMode::NormalsOverwrite: a dedicated render pass (not the compute-based overlay above --
// WebGPU can't storage-write an RG16Uint texture, which is the gbuffer normal attachment's format)
// that overwrites the gbuffer normal directly for pixels covered by resident "Normals"-encoded tile
// data, using loadOp=Load + discard so uncovered pixels keep the geometric normal untouched. See
// SlippyTileOverlay::write_normals_to_gbuffer. decode_hemioct_normal_127 lives in
// slippy_tile_overlay.wgsl, shared with DATA_MODE_NORMALS's on-screen color visualization there.

///use screen_pass_vert
///use webgpu_engine::overlays/slippy_tile_overlay
///use webgpu::encoder

@fragment
fn fragmentMain(vertex_out: VertexOut) -> @location(0) vec2u {
    if settings.data_mode != DATA_MODE_NORMALS_OVERWRITE {
        discard;
    }

    let tci = vec2u(vertex_out.position.xy);
    let raw_depth = textureLoad(depth_texture, tci, 0).r;
    if raw_depth <= 0.0 {
        discard;
    }

    let resolved = resolve_tile_sample(tci, raw_depth);
    if !resolved.found {
        discard;
    }

    let normal = decode_hemioct_normal_127(resolved.color.rg);
    return octNormalEncode2u16(normal);
}
