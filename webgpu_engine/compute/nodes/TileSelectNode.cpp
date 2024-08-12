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

TileSelectNode::TileSelectNode(const TileIdGenerator& tile_id_generator)
    : Node({}, { data_type<const std::vector<tile::Id>*>() })
    , m_tile_id_generator(tile_id_generator)
{
}

void TileSelectNode::run_impl()
{
    qDebug() << "running TileSelectNode ...";
    m_output_tile_ids.clear();
    const auto& tile_ids = m_tile_id_generator();
    m_output_tile_ids.insert(m_output_tile_ids.begin(), tile_ids.begin(), tile_ids.end());
    emit run_finished();
}

Data TileSelectNode::get_output_data_impl([[maybe_unused]] SocketIndex output_index) { return { &m_output_tile_ids }; }

} // namespace webgpu_engine::compute::nodes
