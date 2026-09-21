/*****************************************************************************
 * weBIGeo
 * Copyright (C) 2026 Gerald Kimmersdorfer
 * Housekeeping (timers, persistence, reachability) adapted from Scheduler.cpp,
 * Copyright (C) 2023 Adam Celarek
 * Copyright (C) 2024 Lucas Dworschak
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

#include "DemandScheduler.h"

#include <QDebug>
#include <QStandardPaths>
#include <QTimer>
#include <algorithm>
#include <cassert>
#include <limits>
#include <nucleus/utils/image_loader.h>
#include <optional>
#include <nucleus/utils/lang.h>

using namespace nucleus::tile;

DemandScheduler::DemandScheduler(const Settings& settings)
    : m(settings)
{
    m_queue = new TileRequestQueue(this);
    m_queue->set_limit(m.max_in_flight);
    m_queue->set_rate_limit(m.request_rate, m.request_rate_period_ms);
    // Forwarded rather than connected straight through, so the cumulative stats counters see them.
    connect(m_queue, &TileRequestQueue::tile_requested, this, [this](const tile::Id& id) {
        ++m_totals.total_requested;
        emit tile_requested(id);
    });
    connect(m_queue, &TileRequestQueue::tile_aborted, this, [this](const tile::Id& id) {
        ++m_totals.total_aborted;
        emit tile_aborted(id);
    });

    m_update_timer = new QTimer(this);
    m_update_timer->setSingleShot(true);
    connect(m_update_timer, &QTimer::timeout, this, &DemandScheduler::update);

    m_purge_timer = new QTimer(this);
    m_purge_timer->setSingleShot(true);
    connect(m_purge_timer, &QTimer::timeout, this, &DemandScheduler::purge_ram_cache);

    m_persist_timer = new QTimer(this);
    m_persist_timer->setSingleShot(true);
    connect(m_persist_timer, &QTimer::timeout, this, &DemandScheduler::persist_tiles);
}

DemandScheduler::~DemandScheduler() = default;

bool DemandScheduler::enabled() const { return m_enabled; }
const QString& DemandScheduler::name() const { return m_name; }
const Cache<DataTile>& DemandScheduler::ram_cache() const { return m_ram_cache; }
const TileRequestQueue& DemandScheduler::request_queue() const { return *m_queue; }
unsigned DemandScheduler::n_gpu_resident() const { return unsigned(m_gpu_resident.size()); }
bool DemandScheduler::is_gpu_resident(const tile::Id& id) const { return m_gpu_resident.contains(id); }

DemandScheduler::Tuning DemandScheduler::tuning() const { return { m.planner, m.max_in_flight, m.max_ship_per_update, m.gpu_tile_limit, m.request_rate }; }

void DemandScheduler::set_tuning(const Tuning& tuning)
{
    m.planner = tuning.planner;
    m.max_in_flight = tuning.max_in_flight;
    m.max_ship_per_update = tuning.max_ship_per_update;
    m.gpu_tile_limit = tuning.gpu_tile_limit;
    m.request_rate = tuning.request_rate;
    m_queue->set_limit(m.max_in_flight);
    m_queue->set_rate_limit(m.request_rate, m.request_rate_period_ms);
    schedule_update();
}

void DemandScheduler::submit_wanted(quintptr owner, const std::vector<WantedTile>& wanted)
{
    if (wanted.empty())
        m_wanted.erase(owner);
    else
        m_wanted[owner] = wanted;
    schedule_update();
}

void DemandScheduler::insert_tombstone(const tile::Id& id)
{
    m_ram_cache.insert(DataTile { id, { NetworkInfo::Status::NotFound, nucleus::utils::time_since_epoch() }, std::make_shared<QByteArray>() });
}

void DemandScheduler::receive_tile(const Data& tile)
{
    using Status = NetworkInfo::Status;
    switch (tile.network_info.status) {
    case Status::Good:
        m_ram_cache.insert(DataTile { tile.id, tile.network_info, tile.data });
        m_failed.erase(tile.id);
        ++m_totals.total_delivered;
        break;
    case Status::NotFound:
        // tombstone: same struct, empty payload
        m_ram_cache.insert(DataTile { tile.id, tile.network_info, std::make_shared<QByteArray>() });
        m_failed.erase(tile.id);
        ++m_totals.total_not_found;
        break;
    case Status::NetworkError: {
        ++m_totals.total_network_errors;
        auto& backoff = m_failed[tile.id];
        backoff.attempts = std::min(backoff.attempts + 1, 16u);
        const uint64_t delay = std::min<uint64_t>(m.retry_max_ms, uint64_t(m.retry_base_ms) << backoff.attempts);
        backoff.next_retry_ms = nucleus::utils::time_since_epoch() + delay;
        QTimer::singleShot(int(delay), this, [this]() { schedule_update(); });
        break;
    }
    }

    m_queue->tile_delivered(tile.id);
    schedule_purge();
    schedule_update();
    schedule_persist();
}

void DemandScheduler::update()
{
    if (!m_enabled)
        return;
    const auto now = nucleus::utils::time_since_epoch();

    // 1. merge the feeds of all owners (sum pixel counts per tile)
    tile::IdMap<uint32_t> merged_counts;
    for (const auto& [owner, list] : m_wanted)
        for (const auto& w : list)
            merged_counts[w.id] += w.pixel_count;
    std::vector<WantedTile> wanted;
    wanted.reserve(merged_counts.size());
    for (const auto& [id, count] : merged_counts)
        wanted.push_back({ id, count });

    // 2. plan. the planner queries the same ids repeatedly while walking ancestors, so memoise per call.
    tile::IdMap<TilePlanner::TileState> memo;
    const auto state_of = [&](const tile::Id& id) -> TilePlanner::TileState {
        if (const auto it = memo.find(id); it != memo.end())
            return it->second;
        TilePlanner::TileState s;
        s.on_gpu = m_gpu_resident.contains(id);
        if (m_ram_cache.contains(id)) {
            const auto& info = m_ram_cache.peak_at(id).network_info;
            const bool fresh = info.timestamp + m.retirement_age_ms > now;
            s.in_ram_fresh = fresh && info.status == NetworkInfo::Status::Good;
            s.tombstone = fresh && info.status == NetworkInfo::Status::NotFound;
        }
        if (const auto it = m_failed.find(id); it != m_failed.end())
            s.blocked = it->second.next_retry_ms > now;
        memo.emplace(id, s);
        return s;
    };
    const auto plan = TilePlanner::make(wanted, m.planner, state_of);

    // 3. keep the tiles the shader is currently sampling alive
    const tile::IdSet touch_set(plan.touch.begin(), plan.touch.end());
    for (const auto& id : plan.touch) {
        if (const auto it = m_gpu_resident.find(id); it != m_gpu_resident.end())
            it->second = ++m_lru_counter;
        m_ram_cache.touch(id, now);
    }

    // 4. ship RAM-fresh tiles to the GPU
    const tile::IdSet ship_set(plan.ship.begin(), plan.ship.end()); // incl. the ones deferred below
    std::vector<tile::Id> ship = plan.ship;
    const bool ship_deferred = ship.size() > m.max_ship_per_update;
    if (ship_deferred)
        ship.resize(m.max_ship_per_update);

    // Eviction candidates in LRU order -- never what the shader is currently sampling.
    std::vector<std::pair<uint64_t, tile::Id>> evictable;
    evictable.reserve(m_gpu_resident.size());
    for (const auto& [id, stamp] : m_gpu_resident)
        if (!touch_set.contains(id))
            evictable.emplace_back(stamp, id);
    std::sort(evictable.begin(), evictable.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

    std::vector<tile::Id> deleted;
    size_t n_evicted = 0;
    // 4a. gpu_tile_limit can be lowered at runtime (set_tuning), so shrink to it before anything else.
    while (m_gpu_resident.size() - n_evicted > m.gpu_tile_limit && n_evicted < evictable.size())
        deleted.push_back(evictable[n_evicted++].second);

    // 4b. free enough layers for what we are about to upload.
    size_t n_ship_dropped = 0;
    const size_t n_resident_now = m_gpu_resident.size() - n_evicted;
    const size_t n_free = m.gpu_tile_limit > n_resident_now ? m.gpu_tile_limit - n_resident_now : 0;
    if (ship.size() > n_free) {
        const size_t n_missing = ship.size() - n_free;
        const size_t n_evict = std::min(n_missing, evictable.size() - n_evicted);
        for (size_t i = 0; i < n_evict; ++i)
            deleted.push_back(evictable[n_evicted++].second);
        if (n_evict < n_missing) {
            n_ship_dropped = n_missing - n_evict; // array too small for this view; Stats reports it
            ship.resize(ship.size() - n_ship_dropped); // drop the lowest priority ones
        }
    }

    std::vector<GpuTextureTile> new_tiles;
    new_tiles.reserve(ship.size());
    for (const auto& id : ship) {
        const auto data = m_ram_cache.peak_at(id).data;
        std::optional<nucleus::Raster<glm::u8vec4>> raster;
        if (data && !data->isEmpty()) {
            auto decoded = nucleus::utils::image_loader::rgba8(*data);
            if (decoded.has_value() && decoded->width() == m.tile_resolution && decoded->height() == m.tile_resolution)
                raster = std::move(decoded.value());
        }
        if (!raster) {
            qDebug() << QString("DemandScheduler '%1': tile %2 is not a valid %3x%3 image, treating it as missing.")
                            .arg(m_name)
                            .arg(QString::fromStdString(tile::to_string(id)))
                            .arg(m.tile_resolution);
            insert_tombstone(id);
            continue;
        }
        // a single level: the GPU array has no mips, so generating them would only burn scheduler-thread time
        auto texture = std::make_shared<nucleus::utils::MipmappedColourTexture>();
        texture->emplace_back(*raster, m_compression);
        new_tiles.push_back({ id, std::move(texture) });
    }

    for (const auto& id : deleted)
        m_gpu_resident.erase(id);
    for (const auto& tile : new_tiles)
        m_gpu_resident[tile.id] = ++m_lru_counter;
    if (!deleted.empty() || !new_tiles.empty())
        emit gpu_tiles_updated(deleted, new_tiles);

    if (ship_deferred)
        schedule_update();

    // 5. request what is missing. in-flight ids that are still planned keep running, the rest is aborted by the queue.
    if (m_network_requests_enabled)
        m_queue->set_requests(plan.fetch);

    // 6. publish what just happened (after step 5, so in-flight/pending are current).
    Stats stats = m_totals; // carries the cumulative counters over
    stats.n_wanted = unsigned(wanted.size());
    stats.n_resident = unsigned(m_gpu_resident.size());
    stats.gpu_tile_limit = m.gpu_tile_limit;
    stats.n_ram = m_ram_cache.n_cached_objects();
    stats.n_ship_planned = unsigned(plan.ship.size());
    stats.n_shipped = unsigned(new_tiles.size());
    stats.n_ship_deferred = unsigned(plan.ship.size() - ship.size() - n_ship_dropped);
    stats.n_ship_dropped_no_space = unsigned(n_ship_dropped);
    stats.n_evicted = unsigned(deleted.size());
    stats.n_fetch_planned = unsigned(plan.fetch.size());
    stats.n_in_flight = m_queue->in_flight();
    stats.n_pending = m_queue->pending();
    stats.tile_status.reserve(wanted.size());
    for (size_t i = 0; i < wanted.size(); ++i) {
        const auto& id = wanted[i].id;
        const auto outcome = plan.outcome[i];
        TileStatus status = TileStatus::Unknown;
        if (m_gpu_resident.contains(id))
            status = TileStatus::Resident;
        else if (outcome == TilePlanner::Outcome::NoData)
            status = TileStatus::NoData;
        else if (ship_set.contains(id))
            status = TileStatus::Uploading; // decoded, waiting for a ship slot (or dropped for space)
        else if (m_queue->is_in_flight(id))
            status = TileStatus::InFlight;
        else if (m_queue->is_pending(id))
            status = TileStatus::Queued;
        else if (const auto it = m_failed.find(id); it != m_failed.end() && it->second.next_retry_ms > now)
            status = TileStatus::BackingOff;
        else if (outcome == TilePlanner::Outcome::Skipped)
            status = TileStatus::Skipped;
        // else Unknown: the planner requested an ancestor instead of this tile itself
        if (status == TileStatus::NoData)
            ++stats.n_no_data;
        else if (status == TileStatus::Skipped)
            ++stats.n_skipped;
        stats.tile_status.emplace_back(id, status);
    }
    for (const auto& [id, backoff] : m_failed)
        if (backoff.next_retry_ms > now)
            ++stats.n_backoff;
    emit stats_updated(stats);
}

void DemandScheduler::purge_ram_cache()
{
    if (m_ram_cache.n_cached_objects() <= unsigned(float(m.ram_tile_limit) * 1.05f))
        return;
    m_ram_cache.purge(m.ram_tile_limit);
}

tl::expected<void, QString> DemandScheduler::persist_tiles()
{
    if (m_name == "unnamed" || m_name.isEmpty()) {
        return tl::unexpected(QString("Not persisting tiles as the scheduler is not named, and this would cause name conflicts in the file system."));
    }
    const auto r = m_ram_cache.write_to_disk(disk_cache_path());
    if (!r.has_value()) {
        qDebug() << QString("Writing tiles to disk into %1 failed: %2. Removing all files.").arg(QString::fromStdString(disk_cache_path().string())).arg(r.error());
        std::filesystem::remove_all(disk_cache_path());
    }
    return r;
}

tl::expected<void, QString> DemandScheduler::read_disk_cache()
{
    if (m_name == "unnamed" || m_name.isEmpty()) {
        const auto error = QString("Not reading tiles as the scheduler is not named, and this would cause name conflicts in the file system.");
        qDebug() << error;
        return tl::unexpected(error);
    }
    const auto r = m_ram_cache.read_from_disk(disk_cache_path());
    if (r.has_value()) {
        schedule_update();
    } else {
        qDebug() << QString("Reading tiles from disk cache (%1) failed: \n%2\nRemoving all files.").arg(QString::fromStdString(disk_cache_path().string())).arg(r.error());
        std::filesystem::remove_all(disk_cache_path());
    }
    return r;
}

void DemandScheduler::set_network_reachability(QNetworkInformation::Reachability reachability)
{
    switch (reachability) {
    case QNetworkInformation::Reachability::Online:
    case QNetworkInformation::Reachability::Local:
    case QNetworkInformation::Reachability::Site:
    case QNetworkInformation::Reachability::Unknown:
        qDebug() << "enabling network";
        m_network_requests_enabled = true;
        schedule_update();
        break;
    case QNetworkInformation::Reachability::Disconnected:
        qDebug() << "disabling network";
        m_network_requests_enabled = false;
        break;
    }
}

void DemandScheduler::set_enabled(bool new_enabled)
{
    m_enabled = new_enabled;
    schedule_update();
}

void DemandScheduler::clear_full_cache()
{
    m_ram_cache.purge(0);
    m_failed.clear();
    m_totals = {}; // the cumulative counters describe the current cache, which is now empty

    std::vector<tile::Id> deleted;
    deleted.reserve(m_gpu_resident.size());
    for (const auto& [id, stamp] : m_gpu_resident)
        deleted.push_back(id);
    m_gpu_resident.clear();
    if (!deleted.empty())
        emit gpu_tiles_updated(deleted, {});

    persist_tiles(); // writes the now empty cache, which also removes the files on disk
    schedule_update(); // re-plans from the retained wanted lists
}

void DemandScheduler::set_texture_compression_algorithm(nucleus::utils::ColourTexture::Format compression_algorithm) { m_compression = compression_algorithm; }

void DemandScheduler::set_name(const QString& new_name)
{
    setObjectName(QString("%1_demand_scheduler").arg(new_name));
    m_name = new_name;
}

std::filesystem::path DemandScheduler::disk_cache_path() const
{
    const auto base_path = std::filesystem::path(QStandardPaths::writableLocation(QStandardPaths::CacheLocation).toStdString());
    std::filesystem::create_directories(base_path);
    return base_path / ("tile_cache_" + m_name.toStdString() + "_tiles");
}

void DemandScheduler::schedule_update()
{
    assert(m.update_timeout < unsigned(std::numeric_limits<int>::max()));
    if (m_enabled && !m_update_timer->isActive())
        m_update_timer->start(int(m.update_timeout));
}

void DemandScheduler::schedule_purge()
{
    assert(m.purge_timeout < unsigned(std::numeric_limits<int>::max()));
    if (m_enabled && !m_purge_timer->isActive())
        m_purge_timer->start(int(m.purge_timeout));
}

void DemandScheduler::schedule_persist()
{
    assert(m.persist_timeout < unsigned(std::numeric_limits<int>::max()));
    if (!m_persist_timer->isActive())
        m_persist_timer->start(int(m.persist_timeout));
}
