/*****************************************************************************
 * Alpine Renderer
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

#include "UnittestWebgpuContext.h"
#include "webgpu_engine/compute/GpuHashMap.h"
#include "webgpu_engine/compute/GpuTileId.h"
#include <catch2/catch_test_macros.hpp>

TEST_CASE("webgpu hash map")
{
    UnittestWebgpuContext context;

    struct HashMapValue {
        uint32_t index = std::numeric_limits<uint32_t>::max();

        bool operator==(const HashMapValue& other) const { return index == other.index; }
    };

    HashMapValue default_value = {};
    tile::Id default_key = tile::Id { unsigned(-1), {} };

    SECTION("store values")
    {
        // non-colliding tile ids
        tile::Id key1 { 1, { 1, 1 } };
        tile::Id key2 { 1, { 2, 3 } };
        const HashMapValue value1 { 1 };
        const HashMapValue value2 { 2 };
        const uint16_t hash1 = webgpu_engine::compute::gpu_hash(key1);
        const uint16_t hash2 = webgpu_engine::compute::gpu_hash(key2);
        assert(hash1 != hash2);

        // store values in gpu hash map
        webgpu_engine::compute::GpuHashMap<tile::Id, HashMapValue, compute::GpuTileId> gpu_hash_map(context.device, default_key, default_value);
        gpu_hash_map.store(key1, value1);
        gpu_hash_map.store(key2, value2);
        gpu_hash_map.update_gpu_data();

        std::vector<compute::GpuTileId> ids = gpu_hash_map.key_buffer().read_back_sync(context.device, 1000);
        std::vector<HashMapValue> values = gpu_hash_map.value_buffer().read_back_sync(context.device, 1000);

        CHECK(ids.size() == values.size());
        CHECK(ids.size() == std::numeric_limits<uint16_t>::max() + 1);
        for (uint16_t i = 0; i < static_cast<uint16_t>(ids.size()); i++) {
            if (i == hash1) {
                CHECK(ids[i] == compute::GpuTileId(key1));
                CHECK(values[i] == value1);
            } else if (i == hash2) {
                CHECK(ids[i] == compute::GpuTileId(key2));
                CHECK(values[i] == value2);
            } else {
                CHECK(ids[i] == compute::GpuTileId(default_key));
                CHECK(values[i] == default_value);
            }
        }

        HashMapValue new_value2 = { 4 };
        gpu_hash_map.clear(key1);
        gpu_hash_map.store(key2, new_value2);
        gpu_hash_map.update_gpu_data();

        ids = gpu_hash_map.key_buffer().read_back_sync(context.device, 1000);
        values = gpu_hash_map.value_buffer().read_back_sync(context.device, 1000);

        CHECK(ids.size() == values.size());
        CHECK(ids.size() == std::numeric_limits<uint16_t>::max() + 1);
        for (uint16_t i = 0; i < static_cast<uint16_t>(ids.size()); i++) {
            if (i == hash2) {
                CHECK(ids[i] == key2);
                CHECK(values[i] == new_value2);
            } else {
                CHECK(ids[i] == default_key);
                CHECK(values[i] == default_value);
            }
        }
    }

    SECTION("collision handling")
    {
        // colliding tile ids
        tile::Id key1 { 11, { 59333, 45444 } };
        tile::Id key2 { 5, { 20012, 35075 } };
        const HashMapValue value1 { 1 };
        const HashMapValue value2 { 2 };
        const uint16_t hash1 = webgpu_engine::compute::gpu_hash(key1);
        assert(hash1 == webgpu_engine::compute::gpu_hash(key2));

        // store values in gpu hash map
        webgpu_engine::compute::GpuHashMap<tile::Id, HashMapValue> gpu_hash_map(context.device, default_key, default_value);
        gpu_hash_map.store(key1, value1);
        gpu_hash_map.store(key2, value2);
        gpu_hash_map.update_gpu_data();

        std::vector<tile::Id> ids = gpu_hash_map.key_buffer().read_back_sync(context.device, 1000);
        std::vector<HashMapValue> values = gpu_hash_map.value_buffer().read_back_sync(context.device, 1000);

        CHECK(ids.size() == values.size());
        CHECK(ids.size() == std::numeric_limits<uint16_t>::max() + 1);
        for (uint16_t i = 0; i < static_cast<uint16_t>(ids.size()); i++) {
            if (i == hash1) {
                CHECK(ids[i] == key1);
                CHECK(values[i] == value1);
            } else if (i == hash1 + 1) {
                CHECK(ids[i] == key2);
                CHECK(values[i] == value2);
            } else {
                CHECK(ids[i] == default_key);
                CHECK(values[i] == default_value);
            }
        }
    }
}
