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

#pragma once

#include "Node.h"

namespace webgpu_engine::compute::nodes {

class SelectTilesNode : public Node {
    Q_OBJECT

public:
    using TileIdGenerator = std::function<std::vector<tile::Id>()>;

    enum Input {};
    enum Output { TILE_ID_LIST = 0 };

    SelectTilesNode();
    SelectTilesNode(TileIdGenerator tile_id_generator);

    void set_tile_id_generator(TileIdGenerator tile_id_generator);
    void select_tiles_in_world_aabb(const geometry::Aabb<3, double>& aabb, unsigned int zoomlevel);

public slots:
    void run_impl() override;

protected:
    Data get_output_data_impl(SocketIndex output_index) override;

private:
    TileIdGenerator m_tile_id_generator;
    std::vector<tile::Id> m_output_tile_ids;
};
} // namespace webgpu_engine::compute::nodes
