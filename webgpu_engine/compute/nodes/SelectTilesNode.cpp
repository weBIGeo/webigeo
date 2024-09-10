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

#include "SelectTilesNode.h"

#include "webgpu_engine/compute/RectangularTileRegion.h"
#include <QDebug>
#include <nucleus/srs.h>

namespace webgpu_engine::compute::nodes {

SelectTilesNode::SelectTilesNode()
    : SelectTilesNode([]() { return std::vector<tile::Id> {}; })
{
}

SelectTilesNode::SelectTilesNode(TileIdGenerator tile_id_generator)
    : Node({}, { data_type<const std::vector<tile::Id>*>() })
    , m_tile_id_generator(tile_id_generator)
{
}

void SelectTilesNode::set_tile_id_generator(TileIdGenerator tile_id_generator) { m_tile_id_generator = tile_id_generator; }

void SelectTilesNode::select_tiles_in_world_aabb(const geometry::Aabb<3, double>& aabb, unsigned int zoomlevel)
{
    const auto lower_left_tile = nucleus::srs::world_xy_to_tile_id(glm::dvec2(aabb.min), zoomlevel);
    const auto upper_right_tile = nucleus::srs::world_xy_to_tile_id(glm::dvec2(aabb.max), zoomlevel);

    set_tile_id_generator([lower_left_tile, upper_right_tile]() {
        compute::RectangularTileRegion region { .min = lower_left_tile.coords, .max = upper_right_tile.coords, .zoom_level = 18, .scheme = tile::Scheme::Tms };
        return region.get_tiles();
    });
}

void SelectTilesNode::run_impl()
{
    qDebug() << "running TileSelectNode ...";
    m_output_tile_ids.clear();
    const auto& tile_ids = m_tile_id_generator();

    if (tile_ids.empty()) {
        qWarning() << "no tiles selected";
    }

    m_output_tile_ids.insert(m_output_tile_ids.begin(), tile_ids.begin(), tile_ids.end());
    emit run_finished();
}

Data SelectTilesNode::get_output_data_impl([[maybe_unused]] SocketIndex output_index) { return { &m_output_tile_ids }; }

} // namespace webgpu_engine::compute::nodes
