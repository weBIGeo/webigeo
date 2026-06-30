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

#pragma once

#include <memory>
#include <string>

#include <nucleus/tile/GpuArrayHelper.h>
#include <nucleus/tile/types.h>
#include <webgpu/base/Context.h>
#include <webgpu/base/raii/TextureWithSampler.h>
#include <webgpu/webgpu.h>

namespace webgpu_engine {

/// Owns a 2D-array texture together with the tile-id <-> array-layer bookkeeping for a set of
/// GPU-resident tiles. Extracted from TileMeshRenderer so the terrain mesh and tile overlays can
/// share the same residency machinery.
///
/// Lifecycle: construct -> set_tile_limit(n) -> init(ctx). The array texture is sized to the tile
/// limit at init() time, so set_tile_limit() must be called before init().
///
/// Pixel data is uploaded by the caller (the source type differs per layer kind): reserve a layer
/// with add_tile() and write into texture() at the returned index.
class GpuTileTextureArray {
public:
    /// resolution: edge length in texels of each (square) array layer.
    /// sampler_desc.label must point to storage that outlives init() (e.g. a string literal).
    GpuTileTextureArray(uint32_t resolution, WGPUTextureFormat format, const WGPUSamplerDescriptor& sampler_desc, std::string label);

    void init(webgpu::Context& ctx);
    void set_tile_limit(unsigned new_limit);

    /// Reserve the array layer for tile_id and return its index; upload the texels via texture().
    unsigned add_tile(const nucleus::tile::Id& tile_id);
    void remove_tile(const nucleus::tile::Id& tile_id);

    [[nodiscard]] nucleus::tile::GpuArrayHelper::LayerInfo layer(nucleus::tile::Id tile_id) const;
    [[nodiscard]] bool contains(nucleus::tile::Id tile_id) const;
    [[nodiscard]] unsigned capacity() const;
    /// Hashed tile-id -> layer table for GPU-side lookups (used by overlays sampling this array).
    [[nodiscard]] nucleus::tile::GpuArrayHelper::Dictionary generate_dictionary() const;

    [[nodiscard]] webgpu::raii::Texture& texture() { return m_texture->texture(); }
    [[nodiscard]] const webgpu::raii::Texture& texture() const { return m_texture->texture(); }
    [[nodiscard]] const webgpu::raii::TextureView& texture_view() const { return m_texture->texture_view(); }
    [[nodiscard]] const webgpu::raii::Sampler& sampler() const { return m_texture->sampler(); }

private:
    uint32_t m_resolution;
    WGPUTextureFormat m_format;
    WGPUSamplerDescriptor m_sampler_desc;
    std::string m_label;

    webgpu::Context* m_ctx = nullptr;
    nucleus::tile::GpuArrayHelper m_helper;
    std::unique_ptr<webgpu::raii::TextureWithSampler> m_texture;
};

} // namespace webgpu_engine
