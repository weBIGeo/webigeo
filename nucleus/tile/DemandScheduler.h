/*****************************************************************************
 * weBIGeo
 * Copyright (C) 2026 Gerald Kimmersdorfer
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

#include <QNetworkInformation>
#include <QObject>
#include <QString>
#include <cstdint>
#include <filesystem>
#include <unordered_map>
#include <vector>

#include "Cache.h"
#include "TilePlanner.h"
#include "TileRequestQueue.h"
#include "types.h"
#include <nucleus/utils/ColourTexture.h>

class QTimer;

namespace nucleus::tile {

/// Fetches single tiles driven by a wanted-tile list (see TilePlanner) instead of a camera. Lives on the
/// scheduler thread, like Scheduler. Emits the same gpu_tiles_updated signal TileSource consumes.
class DemandScheduler : public QObject {
    Q_OBJECT
public:
    struct Settings {
        unsigned tile_resolution = 256;
        unsigned gpu_tile_limit = 1024; // must equal the GPU array's tile limit
        unsigned ram_tile_limit = 20000;
        unsigned max_in_flight = 8;
        unsigned max_ship_per_update = 32;
        uint64_t retirement_age_ms = 10ull * 24ull * 3600ull * 1000ull; // 10 days
        unsigned retry_base_ms = 500; // network-error backoff = min(retry_max_ms, retry_base_ms * 2^attempts)
        unsigned retry_max_ms = 30000;
        unsigned update_timeout = 100;
        unsigned purge_timeout = 1000;
        unsigned persist_timeout = 10000;
        TilePlanner::Params planner;
    };

    explicit DemandScheduler(const Settings& settings);
    ~DemandScheduler() override;

    [[nodiscard]] bool enabled() const;
    [[nodiscard]] const QString& name() const;
    [[nodiscard]] std::filesystem::path disk_cache_path() const;

    [[nodiscard]] const Cache<DataTile>& ram_cache() const;
    [[nodiscard]] const TileRequestQueue& request_queue() const;
    [[nodiscard]] unsigned n_gpu_resident() const;
    [[nodiscard]] bool is_gpu_resident(const tile::Id& id) const;

public slots:
    /// Replaces the wanted list of one producer (owner is an opaque key). An empty list removes it.
    void submit_wanted(quintptr owner, const std::vector<WantedTile>& wanted);
    void receive_tile(const Data& tile);
    void update();
    void purge_ram_cache();
    tl::expected<void, QString> persist_tiles();
    tl::expected<void, QString> read_disk_cache();
    void set_network_reachability(QNetworkInformation::Reachability reachability);
    void set_enabled(bool new_enabled);
    void clear_full_cache();
    void set_texture_compression_algorithm(nucleus::utils::ColourTexture::Format compression_algorithm);
    void set_name(const QString& new_name);

signals:
    void tile_requested(const tile::Id& id);
    void tile_aborted(const tile::Id& id);
    void gpu_tiles_updated(const std::vector<tile::Id>& deleted_tiles, const std::vector<GpuTextureTile>& new_tiles);

private:
    struct Backoff {
        unsigned attempts = 0;
        uint64_t next_retry_ms = 0;
    };

    void schedule_update();
    void schedule_purge();
    void schedule_persist();
    void insert_tombstone(const tile::Id& id);

    Settings m;
    QString m_name = "unnamed";
    bool m_enabled = false;
    bool m_network_requests_enabled = true;
    nucleus::utils::ColourTexture::Format m_compression = nucleus::utils::ColourTexture::Format::Uncompressed_RGBA;

    Cache<DataTile> m_ram_cache;
    tile::IdMap<uint64_t> m_gpu_resident; // id -> LRU counter; mirrors what the GPU array holds
    uint64_t m_lru_counter = 0;
    std::unordered_map<quintptr, std::vector<WantedTile>> m_wanted;
    tile::IdMap<Backoff> m_failed;
    TileRequestQueue* m_queue = nullptr;
    QTimer* m_update_timer = nullptr;
    QTimer* m_purge_timer = nullptr;
    QTimer* m_persist_timer = nullptr;
};

} // namespace nucleus::tile
