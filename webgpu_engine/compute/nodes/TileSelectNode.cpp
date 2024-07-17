/*****************************************************************************
 * weBIGeo
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

#include "TileSelectNode.h"

#include "../RectangularTileRegion.h"
#include <QDebug>

namespace webgpu_engine::compute::nodes {

TileSelectNode::TileSelectNode()
    : Node({}, { data_type<const std::vector<tile::Id>*>() })
{
}

void TileSelectNode::run_impl()
{
    qDebug() << "running TileSelectNode ...";

    glm::uvec2 min = { 140288, 169984 };
    RectangularTileRegion region { .min = min,
        .max = min + glm::uvec2 { 12, 12 }, // inclusive, so this region has 13x13 tiles
        .zoom_level = 18,
        .scheme = tile::Scheme::Tms };
    m_output_tile_ids = region.get_tiles();
    emit run_finished();
}

Data TileSelectNode::get_output_data_impl([[maybe_unused]] SocketIndex output_index) { return { &m_output_tile_ids }; }

} // namespace webgpu_engine::compute::nodes
