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

#include "GpuTileStorage.h"

#include "nucleus/utils/tile_conversion.h"

namespace webgpu_engine {
    
TextureArrayComputeTileStorage::TextureArrayComputeTileStorage(
    WGPUDevice device, const glm::uvec2& resolution, size_t capacity, WGPUTextureFormat format, WGPUTextureUsageFlags usage)
    : m_device { device }
    , m_queue { wgpuDeviceGetQueue(device) }
    , m_resolution { resolution }
    , m_capacity { capacity }
{
    WGPUTextureDescriptor height_texture_desc {};
    height_texture_desc.label = "compute storage texture";
    height_texture_desc.dimension = WGPUTextureDimension::WGPUTextureDimension_2D;
    height_texture_desc.size = { uint32_t(m_resolution.x), uint32_t(m_resolution.y), uint32_t(m_capacity) };
    height_texture_desc.mipLevelCount = 1;
    height_texture_desc.sampleCount = 1;
    height_texture_desc.format = format;
    height_texture_desc.usage = usage;

    WGPUSamplerDescriptor height_sampler_desc {};
    height_sampler_desc.label = "compute storage sampler";
    height_sampler_desc.addressModeU = WGPUAddressMode::WGPUAddressMode_ClampToEdge;
    height_sampler_desc.addressModeV = WGPUAddressMode::WGPUAddressMode_ClampToEdge;
    height_sampler_desc.addressModeW = WGPUAddressMode::WGPUAddressMode_ClampToEdge;
    height_sampler_desc.magFilter = WGPUFilterMode::WGPUFilterMode_Linear;
    height_sampler_desc.minFilter = WGPUFilterMode::WGPUFilterMode_Linear;
    height_sampler_desc.mipmapFilter = WGPUMipmapFilterMode::WGPUMipmapFilterMode_Linear;
    height_sampler_desc.lodMinClamp = 0.0f;
    height_sampler_desc.lodMaxClamp = 1.0f;
    height_sampler_desc.compare = WGPUCompareFunction::WGPUCompareFunction_Undefined;
    height_sampler_desc.maxAnisotropy = 1;

    m_texture_array = std::make_unique<raii::TextureWithSampler>(m_device, height_texture_desc, height_sampler_desc);

    m_tile_ids = std::make_unique<raii::RawBuffer<GpuTileId>>(
        m_device, WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst, uint32_t(m_capacity), "compute tile storage tile id buffer");

    m_layer_index_to_tile_id.clear();
    m_layer_index_to_tile_id.resize(m_capacity, tile::Id { unsigned(-1), {} });
}

void TextureArrayComputeTileStorage::init() { }

void TextureArrayComputeTileStorage::store(const tile::Id& id, std::shared_ptr<QByteArray> data)
{
    // TODO maybe rather use hash map than list

    // already contained, return
    if (std::find(m_layer_index_to_tile_id.begin(), m_layer_index_to_tile_id.end(), id) != m_layer_index_to_tile_id.end()) {
        return;
    }

    // find free spot
    const auto found = std::find(m_layer_index_to_tile_id.begin(), m_layer_index_to_tile_id.end(), tile::Id { unsigned(-1), {} });
    if (found == m_layer_index_to_tile_id.end()) {
        // TODO capacity is reached! do something (but what?)
    }
    *found = id;
    const size_t found_index = found - m_layer_index_to_tile_id.begin();

    // convert to raster and store in texture array
    const nucleus::Raster<glm::u8vec4> height_image = nucleus::stb::load_8bit_rgba_image_from_memory(*data);
    const auto heightraster = nucleus::utils::tile_conversion::u8vec4raster_to_u16raster(height_image);
    m_texture_array->texture().write(m_queue, heightraster, uint32_t(found_index));

    GpuTileId gpu_tile_id = { .x = id.coords.x, .y = id.coords.y, .zoomlevel = id.zoom_level };
    m_tile_ids->write(m_queue, &gpu_tile_id, 1, found_index);
}

void TextureArrayComputeTileStorage::clear(const tile::Id& id)
{
    auto found = std::find(m_layer_index_to_tile_id.begin(), m_layer_index_to_tile_id.end(), id);
    if (found != m_layer_index_to_tile_id.end()) {
        *found = tile::Id { unsigned(-1), {} };
    }
}

void TextureArrayComputeTileStorage::read_back_async(size_t layer_index, ReadBackCallback callback)
{
    size_t buffer_size_bytes = 4 * m_resolution.x * m_resolution.y; // RGBA8 -> hardcoded, depends actual on format used!

    // create buffer and add buffer and callback to back of queue
    m_read_back_states.emplace(std::make_unique<raii::RawBuffer<char>>(
                                   m_device, WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead, uint32_t(buffer_size_bytes), "tile storage read back buffer"),
        callback, layer_index);

    m_texture_array->texture().copy_to_buffer(m_device, *m_read_back_states.back().buffer, uint32_t(layer_index));

    auto on_buffer_mapped = [](WGPUBufferMapAsyncStatus status, void* user_data) {
        TextureArrayComputeTileStorage* _this = reinterpret_cast<TextureArrayComputeTileStorage*>(user_data);

        if (status != WGPUBufferMapAsyncStatus_Success) {
            std::cout << "error: failed mapping buffer for ComputeTileStorage read back" << std::endl;
            _this->m_read_back_states.pop();
            return;
        }

        const ReadBackState& current_state = _this->m_read_back_states.front();
        size_t buffer_size_bytes = 4 * _this->m_resolution.x * _this->m_resolution.y; // RGBA8 -> hardcoded, depends actual on format used!
        const char* buffer_data = (const char*)wgpuBufferGetConstMappedRange(current_state.buffer->handle(), 0, buffer_size_bytes);
        current_state.callback(current_state.layer_index, std::make_shared<QByteArray>(buffer_data, buffer_size_bytes));
        wgpuBufferUnmap(current_state.buffer->handle());

        _this->m_read_back_states.pop();
    };

    wgpuBufferMapAsync(m_read_back_states.back().buffer->handle(), WGPUMapMode_Read, 0, uint32_t(buffer_size_bytes), on_buffer_mapped, this);
}

std::vector<WGPUBindGroupEntry> TextureArrayComputeTileStorage::create_bind_group_entries(const std::vector<uint32_t>& bindings) const
{
    assert(bindings.size() == 1 || bindings.size() == 2);
    if (bindings.size() == 1) {
        return { m_texture_array->texture_view().create_bind_group_entry(bindings.at(0)) };
    }
    return { m_texture_array->texture_view().create_bind_group_entry(bindings.at(0)), m_tile_ids->create_bind_group_entry(bindings.at(1)) };
}

} // namespace webgpu_engine
