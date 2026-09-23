/*****************************************************************************
 * AlpineMaps.org
 * Copyright (C) 2024 Adam Celarek
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

#include "TextureScheduler.h"
#include "conversion.h"
#include <QDebug>
#include <QtAssert>
#include <nucleus/utils/image_loader.h>

namespace nucleus::tile {

TextureScheduler::TextureScheduler(const Scheduler::Settings& settings)
    : nucleus::tile::Scheduler(settings)
    , m_default_raster(glm::uvec2(settings.tile_resolution), { 255, 255, 255, 255 })
{
}

TextureScheduler::~TextureScheduler() = default;

void TextureScheduler::transform_and_emit(const std::vector<tile::DataQuad>& new_quads, const std::vector<tile::Id>& deleted_quads)
{
    std::vector<GpuTextureTile> new_gpu_tiles;
    new_gpu_tiles.reserve(new_gpu_tiles.size() * 4);

    for (const auto& quad : new_quads) {
        GpuTextureTile gpu_tile;
        gpu_tile.id = quad.id;
        auto ortho_raster = to_raster(quad, m_default_raster);
        gpu_tile.texture = std::make_shared<nucleus::utils::MipmappedColourTexture>(generate_mipmapped_colour_texture(ortho_raster, m_compression_algorithm));
        new_gpu_tiles.push_back(gpu_tile);
    }

    // we are merging the tiles. so deleted quads become deleted tiles.
    emit gpu_tiles_updated(deleted_quads, new_gpu_tiles);
}

void TextureScheduler::set_texture_compression_algorithm(nucleus::utils::ColourTexture::Format compression_algorithm)
{
    m_compression_algorithm = compression_algorithm;
}

radix::Raster<glm::u8vec4> TextureScheduler::to_raster(const tile::DataQuad& quad, const radix::Raster<glm::u8vec4>& default_raster)
{
    Q_ASSERT(quad.n_tiles == 4);

    std::array<radix::Raster<glm::u8vec4>, 4> quad_rasters;
    std::array<tile::Id, 4> quad_ids;
    for (const auto& tile : quad.tiles) {
        const auto quad_index = unsigned(quad_position(tile.id));
        quad_ids[quad_index] = tile.id;
        if (tile.data->size()) {
            // Ortho image is available
            const auto ortho_raster = nucleus::utils::image_loader::rgba8(*tile.data.get()).value_or(default_raster);
            quad_rasters[quad_index] = std::move(ortho_raster);
        } else {
            // Ortho image is not available (use white default tile)
            quad_rasters[quad_index] = default_raster;
        }
    }

    auto top
        = radix::raster::concatenate_horizontally(quad_rasters[unsigned(tile::QuadPosition::TopLeft)], quad_rasters[unsigned(tile::QuadPosition::TopRight)]);
    auto bottom = radix::raster::concatenate_horizontally(
        quad_rasters[unsigned(tile::QuadPosition::BottomLeft)], quad_rasters[unsigned(tile::QuadPosition::BottomRight)]);
    if (!top || !bottom) {
        Q_ASSERT(false && "Texture quad rasters must have compatible dimensions");
        return {};
    }

    auto ortho_raster = radix::raster::concatenate_vertically(*top, *bottom);
    if (!ortho_raster) {
        Q_ASSERT(false && "Texture quad raster rows must have compatible dimensions");
        return {};
    }

    return std::move(*ortho_raster);
}

} // namespace nucleus::tile
