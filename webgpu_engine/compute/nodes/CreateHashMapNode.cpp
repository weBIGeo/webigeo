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

#include "CreateHashMapNode.h"

#include <QDebug>

namespace webgpu_engine::compute::nodes {

CreateHashMapNode::CreateHashMapNode(WGPUDevice device, const glm::uvec2& resolution, size_t capacity, WGPUTextureFormat format)
    : Node({ data_type<const std::vector<tile::Id>*>(), data_type<const std::vector<QByteArray>*>() },
        { data_type<GpuHashMap<tile::Id, uint32_t, GpuTileId>*>(), data_type<TileStorageTexture*>() })
    , m_device { device }
    , m_queue { wgpuDeviceGetQueue(device) }
    , m_output_tile_id_to_index(device, tile::Id { unsigned(-1), {} }, -1)
    , m_output_tile_textures(device, resolution, capacity, format, WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst)
{
    m_output_tile_id_to_index.update_gpu_data();
}

void CreateHashMapNode::run_impl()
{
    qDebug() << "running ConvertToHashMapNode ...";

    // clear hash map and storage texture
    m_output_tile_id_to_index.clear();
    m_output_tile_textures.clear();

    // get input data
    // TODO maybe make get_input_data a template (so usage would become get_input_data<type>(socket_index))
    const auto& tile_ids = *std::get<data_type<const std::vector<tile::Id>*>()>(get_input_data(0)); // input 1, list of tile ids
    const auto& textures = *std::get<data_type<const std::vector<QByteArray>*>()>(get_input_data(1)); // input 2, list of tile corresponding textures

    assert(tile_ids.size() == textures.size());

    // store each texture in texture array and store resulting index in hashmap
    for (size_t i = 0; i < tile_ids.size(); i++) {
        auto index = m_output_tile_textures.store(textures[i]);
        m_output_tile_id_to_index.store(tile_ids[i], uint32_t(index));
    }
    m_output_tile_id_to_index.update_gpu_data();

    wgpuQueueOnSubmittedWorkDone(
        m_queue,
        []([[maybe_unused]] WGPUQueueWorkDoneStatus status, void* user_data) {
            CreateHashMapNode* _this = reinterpret_cast<CreateHashMapNode*>(user_data);
            // qDebug() << "hash=" << gpu_hash(tile::Id { 11, { 1096, 1328 } }) << Qt::endl;
            // std::cout << " done, reading back buffer for debugging purposes..." << std::endl;
            // std::vector<GpuTileId> key_buffer_contents = _this->m_output_tile_id_to_index.key_buffer().read_back_sync(_this->m_device, 10000);
            // std::vector<uint32_t> value_buffer_contents = _this->m_output_tile_id_to_index.value_buffer().read_back_sync(_this->m_device, 10000);
            // std::cout << "done" << std::endl;

            emit _this->run_finished();
        },
        this);
}

Data CreateHashMapNode::get_output_data_impl(SocketIndex output_index)
{
    // return pointers to hash map and texture array respectively
    switch (output_index) {
    case Output::TILE_ID_TO_TEXTURE_ARRAY_INDEX_MAP:
        return { &m_output_tile_id_to_index };
    case Output::TEXTURE_ARRAY:
        return { &m_output_tile_textures };
    }
    exit(-1);
}

} // namespace webgpu_engine::compute::nodes
