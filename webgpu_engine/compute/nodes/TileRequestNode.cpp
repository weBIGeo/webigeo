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

#include "TileRequestNode.h"

#include <QDebug>

namespace webgpu_engine::compute::nodes {

TileRequestNode::TileRequestNode()
    : Node({ data_type<const std::vector<tile::Id>*>() }, { data_type<const std::vector<QByteArray>*>() })
    , m_tile_loader { std::make_unique<nucleus::tile_scheduler::TileLoadService>(
          "https://alpinemaps.cg.tuwien.ac.at/tiles/alpine_png/", nucleus::tile_scheduler::TileLoadService::UrlPattern::ZXY, ".png") } // TODO dont hardcode
{
    connect(m_tile_loader.get(), &nucleus::tile_scheduler::TileLoadService::load_finished, this, &TileRequestNode::on_single_tile_received);
}

void TileRequestNode::run()
{
    qDebug() << "running HeightRequestNode ...";

    // get tile ids to request
    // TODO maybe make get_input_data a template (so usage would become get_input_data<type>(socket_index))
    const auto& tile_ids = *std::get<data_type<const std::vector<tile::Id>*>()>(get_input_data(0)); // input 1, list of tile ids

    // send request for each tile
    m_received_tile_textures.resize(tile_ids.size());
    m_requested_tile_ids = tile_ids;
    m_num_tiles_requested = m_received_tile_textures.size();
    m_num_tiles_received = 0;
    qDebug() << "requesting " << m_num_tiles_requested << " tiles ...";
    for (const auto& tile_id : tile_ids) {
        m_tile_loader->load(tile_id);
    }
}

Data TileRequestNode::get_output_data_impl([[maybe_unused]] SocketIndex output_index) { return { &m_received_tile_textures }; }

void TileRequestNode::on_single_tile_received(const nucleus::tile_scheduler::tile_types::TileLayer& tile)
{
    auto found_it = std::find(m_requested_tile_ids.begin(), m_requested_tile_ids.end(), tile.id);

    assert(found_it != m_requested_tile_ids.end()); // cannot receive tile id that was not requested

    size_t found_index = found_it - m_requested_tile_ids.begin();
    m_received_tile_textures[found_index] = *tile.data;

    m_num_tiles_received++;
    if (m_num_tiles_received == m_num_tiles_requested) {
        // emit all_tiles_received();
        emit run_finished();
    }
}

} // namespace webgpu_engine::compute::nodes
