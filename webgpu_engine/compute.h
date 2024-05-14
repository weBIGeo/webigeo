/*****************************************************************************
 * weBIGeo
 * Copyright (C) 2024 Gerald Kimmersdorfer
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

#include "PipelineManager.h"
#include "nucleus/tile_scheduler/TileLoadService.h"
#include "nucleus/tile_scheduler/tile_types.h"
#include "nucleus/timing/CpuTimer.h"
#include "raii/Buffer.h"
#include "raii/TextureWithSampler.h"
#include <QByteArray>
#include <QObject>
#include <queue>
#include <vector>

namespace webgpu_engine {

struct GpuTileId {
    uint32_t x;
    uint32_t y;
    uint32_t zoomlevel;
};

struct RectangularTileRegion {
    glm::uvec2 min;
    glm::uvec2 max;
    unsigned int zoom_level;
    tile::Scheme scheme;

    std::vector<tile::Id> get_tiles() const;
};

template <typename T, std::unsigned_integral HashType = uint16_t> HashType gpu_hash(const T&)
{
    static_assert(sizeof(T) != sizeof(T), "default implementation should not be called, add a template specialization for your types");
}
template <> uint16_t gpu_hash<tile::Id, uint16_t>(const tile::Id& id);

template <typename T, typename HashType = uint16_t>
concept GpuHashable = requires(T a) {
    {
        gpu_hash<T, HashType>(a)
    } -> std::same_as<HashType>;
};

/// Hashmap storing values on the GPU.
///
/// Keys are hashed using static template class member function
///
///     GpuHashFunc::hash(const KeyType&) -> HashType
///
/// To add a new type usable as KeyType, you need to add a template specialization for GpuHashFunc::hash.
///
/// KeyType needs to be convertible to GpuKeyType (either through conversion function or converting constructor).
/// ValueType needs to be convertible to GpuValueType (either through conversion function or converting constructor).
///
/// Usage: see unit test test_GpuHashMap.cpp
template <typename KeyType, typename ValueType, typename GpuKeyType = KeyType, typename GpuValueType = ValueType, typename HashType = uint16_t>
requires GpuHashable<KeyType, HashType>
class GpuHashMap {
public:
    GpuHashMap(WGPUDevice device, const KeyType& empty_key, const ValueType& empty_value)
        : m_device { device }
        , m_queue { wgpuDeviceGetQueue(device) }
        , m_capacity { std::numeric_limits<HashType>::max() + 1 }
        , m_empty_gpu_key { empty_key }
        , m_empty_gpu_value { empty_value }
        , m_stored_map(m_capacity)
        , m_id_map { std::make_unique<raii::RawBuffer<GpuKeyType>>(
              m_device, WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc, m_capacity, "hashmap id map buffer") }
        , m_value_map { std::make_unique<raii::RawBuffer<GpuValueType>>(
              m_device, WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc, m_capacity, "hashmap value map buffer") }
    {
    }

    /// Stores value at id.
    /// Need to call update_gpu_data for effects to be visible on GPU side.
    void store(const KeyType& id, const ValueType& value) { m_stored_map[id] = value; }

    /// Clears value at id.
    /// Need to call update_gpu_data for effects to be visible on GPU side.
    void clear(const KeyType& id) { m_stored_map.erase(id); }

    /// Update GPU buffers.
    void update_gpu_data()
    {
        assert(m_stored_map.size() <= m_capacity);

        // hash keys and write key/value into calculated position in key/value vector
        std::vector<GpuKeyType> keys(m_capacity, GpuKeyType(m_empty_gpu_key));
        std::vector<GpuValueType> values(m_capacity, GpuValueType(m_empty_gpu_value));
        for (const auto& key_value_pair : m_stored_map) {
            HashType hash = gpu_hash<KeyType, HashType>(key_value_pair.first);
            while (keys.at(hash) != m_empty_gpu_key) {
                hash++;
            }
            keys[hash] = GpuKeyType(key_value_pair.first);
            values[hash] = GpuValueType(key_value_pair.second);
        }

        // update GPU buffers
        m_id_map->write(m_queue, keys.data(), keys.size());
        m_value_map->write(m_queue, values.data(), values.size());
    }

    raii::RawBuffer<GpuKeyType>& key_buffer() { return *m_id_map; }
    raii::RawBuffer<GpuValueType>& value_buffer() { return *m_value_map; }

private:
    WGPUDevice m_device;
    WGPUQueue m_queue;

    uint32_t m_capacity;
    KeyType m_empty_gpu_key;
    ValueType m_empty_gpu_value;
    // TODO for now KeyType::Hasher works for tile::Id only
    //  either make this a concept and require it for KeyType or required std::hash<KeyType>
    //  or manage using vector of pairs instead of hashmap (tho that would be slower, probably)
    std::unordered_map<KeyType, ValueType, typename KeyType::Hasher> m_stored_map;

    std::unique_ptr<raii::RawBuffer<GpuKeyType>> m_id_map;
    std::unique_ptr<raii::RawBuffer<GpuValueType>> m_value_map;
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
    std::unique_ptr<raii::TextureWithSampler> m_texture_array;
    std::unique_ptr<raii::RawBuffer<GpuTileId>> m_tile_ids;
    glm::uvec2 m_resolution;
    size_t m_capacity;

    std::vector<tile::Id> m_layer_index_to_tile_id;
    std::queue<ReadBackState> m_read_back_states;
};

class ComputeController : public QObject {
    Q_OBJECT

public:
    ComputeController(WGPUDevice device, const PipelineManager& pipeline_manager);
    ~ComputeController() = default;

    void request_tiles(const RectangularTileRegion& region);
    void run_pipeline();

    // write tile data to files only for debugging, writes next to app.exe
    void write_output_tiles(const std::filesystem::path& dir = ".") const;

    float get_last_tile_request_timing();
    float get_last_pipeline_run_timing();

public slots:
    void on_single_tile_received(const nucleus::tile_scheduler::tile_types::TileLayer& tile);

signals:
    void tiles_requested();
    void tiles_received();
    void pipeline_run_queued();
    void pipeline_done();

private:
    const size_t m_max_num_tiles = 256;
    const glm::uvec2 m_input_tile_resolution = { 65, 65 };
    const glm::uvec2 m_output_tile_resolution = { 256, 256 };

    size_t m_num_tiles_received = 0;
    size_t m_num_tiles_requested = 0;

    const PipelineManager* m_pipeline_manager;
    WGPUDevice m_device;
    WGPUQueue m_queue;
    std::unique_ptr<nucleus::tile_scheduler::TileLoadService> m_tile_loader;

    std::unique_ptr<raii::BindGroup> m_compute_bind_group;

    std::unique_ptr<ComputeTileStorage> m_input_tile_storage;
    std::unique_ptr<ComputeTileStorage> m_output_tile_storage;

    nucleus::timing::CpuTimer m_tile_request_timer;
    nucleus::timing::CpuTimer m_pipeline_run_timer;
};

} // namespace webgpu_engine
