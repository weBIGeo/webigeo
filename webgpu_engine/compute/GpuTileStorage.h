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

#include "radix/tile.h"
#include "webgpu_engine/raii/Buffer.h"
#include "webgpu_engine/raii/TextureWithSampler.h"

namespace webgpu_engine {

struct GpuTileId {
    uint32_t x;
    uint32_t y;
    uint32_t zoomlevel;
    uint32_t alignment = std::numeric_limits<uint32_t>::max();

    GpuTileId() = default;
    GpuTileId(uint32_t x, uint32_t y, uint32_t zoomlevel);
    GpuTileId(const tile::Id& tile_id);

    bool operator==(const GpuTileId& other) const { return x == other.x && y == other.y && zoomlevel == other.zoomlevel; }
};

/// Minimal wrapper over texture array for more convenient usage (intended for storing tile textures).
class TileStorageTexture {

public:
    TileStorageTexture(WGPUDevice device, const glm::uvec2& resolution, size_t capacity, WGPUTextureFormat format,
        WGPUTextureUsageFlags usage = WGPUTextureUsage_StorageBinding | WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst);

    void store(size_t layer, const QByteArray& data);
    size_t store(const QByteArray& data); // store at next free spot
    void clear(); // clear all
    void clear(size_t layer);

    raii::TextureWithSampler& texture();
    const raii::TextureWithSampler& texture() const;

private:
    size_t find_unused_layer_index();

private:
    WGPUDevice m_device;
    WGPUQueue m_queue;
    glm::uvec2 m_resolution;
    size_t m_capacity;
    size_t m_num_stored = 0; // number of stored textures
    std::vector<bool> m_layers_used; // CPU buffer for tracking which layers are currently used
    std::unique_ptr<raii::TextureWithSampler> m_texture_array;
};

/// Manages a set of tiles in GPU memory
/// Supports adding and removing tiles, reading back tiles into host memory
class ComputeTileStorage {
public:
    using ReadBackCallback = std::function<void(size_t layer_index, std::shared_ptr<QByteArray>)>;

    virtual ~ComputeTileStorage() = default;

    virtual void init() = 0;
    virtual void store(const tile::Id& id, std::shared_ptr<QByteArray> data) = 0;
    virtual void clear(const tile::Id& id) = 0;
    virtual std::vector<WGPUBindGroupEntry> create_bind_group_entries(const std::vector<uint32_t>& bindings) const = 0;
    virtual void read_back_async(size_t layer_index, ReadBackCallback callback) = 0;
};

class TextureArrayComputeTileStorage : public ComputeTileStorage {
public:
    struct ReadBackState {
        std::unique_ptr<raii::RawBuffer<char>> buffer;
        ReadBackCallback callback;
        size_t layer_index;
    };

public:
    TextureArrayComputeTileStorage(WGPUDevice device, const glm::uvec2& resolution, size_t capacity, WGPUTextureFormat format,
        WGPUTextureUsageFlags usage = WGPUTextureUsage_StorageBinding | WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst);

    void init() override;
    void store(const tile::Id& id, std::shared_ptr<QByteArray> data) override;
    void clear(const tile::Id& id) override;

    void read_back_async(size_t layer_index, ReadBackCallback callback) override;

    std::vector<WGPUBindGroupEntry> create_bind_group_entries(const std::vector<uint32_t>& bindings) const override;

private:
    WGPUDevice m_device;
    WGPUQueue m_queue;
    std::unique_ptr<TileStorageTexture> m_tile_storage_texture;

    std::unique_ptr<raii::RawBuffer<GpuTileId>> m_tile_ids;
    glm::uvec2 m_resolution;
    size_t m_capacity;

    std::vector<tile::Id> m_layer_index_to_tile_id;
    std::queue<ReadBackState> m_read_back_states;
};

} // namespace webgpu_engine
