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
#include "nucleus/tile_scheduler/TileLoadService.h"

namespace webgpu_engine::compute::nodes {

class RequestTilesNode : public Node {
    Q_OBJECT

public:
    enum Input : SocketIndex { TILE_ID_LIST = 0 };
    enum Output : SocketIndex { TILE_TEXTURE_LIST = 0 };

    RequestTilesNode();

    void on_single_tile_received(const nucleus::tile_scheduler::tile_types::TileLayer& tile);

public slots:
    void run_impl() override;

protected:
    Data get_output_data_impl(SocketIndex output_index) override;

private:
    std::unique_ptr<nucleus::tile_scheduler::TileLoadService> m_tile_loader;
    size_t m_num_tiles_received = 0;
    size_t m_num_tiles_requested = 0;
    std::vector<QByteArray> m_received_tile_textures;
    std::vector<tile::Id> m_requested_tile_ids;
};

} // namespace webgpu_engine::compute::nodes
