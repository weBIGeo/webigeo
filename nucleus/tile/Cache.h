/*****************************************************************************
 * AlpineMaps.org
 * Copyright (C) 2023 Adam Celarek
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

#include "types.h"
#include <QDebug>
#include <QFile>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <nucleus/utils/lang.h>
#include <shared_mutex>
#include <span>
#include <tl/expected.hpp>
#include <unordered_map>
#include <vector>
#include <zpp_bits.h>

namespace glm {

template<typename T>
constexpr auto serialize(auto & archive, const glm::vec<2, T> & vec)
{
    return archive(vec.x, vec.y);
}

template<typename T>
constexpr auto serialize(auto & archive, glm::vec<2, T> & vec)
{
    return archive(vec.x, vec.y);
}

}

namespace nucleus::tile {

/// This class is thread safe. be careful with the visit method as it writes the cache and therefore locks an internal mutex.
template<NamedTile T>
class Cache
{
    struct MetaData {
        uint64_t visited;
        uint64_t created;
        uint64_t offset; // byte offset into the blob file
        uint64_t length; // length in bytes of this tile's serialized payload in the blob
    };

    struct CacheObject {
        MetaData meta;
        T data;
    };

    std::unordered_map<tile::Id, CacheObject, tile::Id::Hasher> m_data;
    mutable std::shared_mutex m_data_mutex;
    std::unordered_map<tile::Id, MetaData, tile::Id::Hasher> m_disk_cached;
    mutable std::shared_mutex m_disk_cached_mutex;

public:
    Cache() = default;
    void insert(const T& tile);
    void touch(const tile::Id& id, uint64_t stamp);
    [[nodiscard]] bool contains(const tile::Id& id) const;
    [[nodiscard]] unsigned n_cached_objects() const;
    /// functor should return true, if the given tile should be marked visited. stops descending if false is returned. don't do heavy lifting in the functort, as it blocks all other access!
    template<typename VisitorFunction>
    void visit(const VisitorFunction& functor);
    const T& peak_at(const tile::Id& id) const;
    std::vector<T> purge(unsigned remaining_capacity);

    [[nodiscard]] tl::expected<void, QString> write_to_disk(const std::filesystem::path& path);
    [[nodiscard]] tl::expected<void, QString> read_from_disk(const std::filesystem::path& path);

private:
    template<typename VisitorFunction>
    void visit(const tile::Id& start_node,
               const VisitorFunction& functor,
               uint64_t visited_stamp); // must stay private or protected by mutex

    static std::filesystem::path blob_path(const std::filesystem::path& base_path) { return base_path / "tiles.blob"; }

    static std::filesystem::path meta_info_path(const std::filesystem::path& base_path)
    {
        return base_path / "meta_info.alp";
    }
};

using MemoryCache = nucleus::tile::Cache<nucleus::tile::DataQuad>;

template <NamedTile T>
void Cache<T>::insert(const T& tile)
{
    auto locker = std::scoped_lock(m_data_mutex);
    const auto time_stamp = nucleus::utils::time_since_epoch();
    m_data[tile.id].meta.visited = time_stamp * 100 - tile.id.zoom_level;
    m_data[tile.id].meta.created = time_stamp;
    m_data[tile.id].data = tile;
}

template <NamedTile T>
void Cache<T>::touch(const tile::Id& id, uint64_t stamp)
{
    auto locker = std::scoped_lock(m_data_mutex);
    auto it = m_data.find(id);
    if (it == m_data.end())
        return;
    it->second.meta.visited = stamp * 100 - id.zoom_level;
}

template <NamedTile T>
bool Cache<T>::contains(const tile::Id& id) const
{
    auto locker = std::shared_lock(m_data_mutex);
    return m_data.contains(id);
}

template <NamedTile T>
unsigned int Cache<T>::n_cached_objects() const
{
    auto locker = std::shared_lock(m_data_mutex);
    return unsigned(m_data.size());
}

template <NamedTile T>
const T& Cache<T>::peak_at(const tile::Id& id) const
{
    auto locker = std::shared_lock(m_data_mutex);
    return m_data.at(id).data;
}

template <NamedTile T> tl::expected<void, QString> Cache<T>::write_to_disk(const std::filesystem::path& base_path)
{
    const auto write_start = std::chrono::steady_clock::now();
    const auto unexpected_error = [](const auto& e) { return tl::unexpected(QString::fromStdString(std::make_error_code(e).message())); };
    static_assert(SerialisableTile<T>);
    std::filesystem::create_directories(base_path);
    std::unordered_map<tile::Id, CacheObject, tile::Id::Hasher> data;
    {
        auto locker = std::scoped_lock(m_data_mutex);
        data = m_data; // copies only metadata and references to tiles
    }
    auto locker = std::scoped_lock(m_disk_cached_mutex);

    const auto write = [](const auto& bytes, const auto& path, QIODeviceBase::OpenMode mode) -> tl::expected<void, QString> {
        QFile file(path);
        const auto success = file.open(mode);
        if (!success)
            return tl::unexpected<QString>(QString("Couldn't open file '%1' for writing!").arg(QString::fromStdString(path.string())));
        file.write(bytes.data(), qint64(bytes.size()));
        return {};
    };

    std::unordered_map<tile::Id, MetaData, tile::Id::Hasher> disk_cached_old;
    std::swap(m_disk_cached, disk_cached_old);
    m_disk_cached.reserve(data.size());

    // Decide whether it's worth rewriting the whole blob to reclaim dead space left by tiles that were
    // removed or updated since the last persist -- judged against state as of before this call.
    const auto blob = blob_path(base_path);
    const uint64_t old_blob_size = std::filesystem::exists(blob) ? uint64_t(std::filesystem::file_size(blob)) : 0;
    uint64_t old_live_bytes = 0;
    for (const auto& item : disk_cached_old)
        old_live_bytes += item.second.length;
    const uint64_t dead_bytes = old_blob_size > old_live_bytes ? old_blob_size - old_live_bytes : 0;
    const bool compact = old_live_bytes > 0 && dead_bytes > old_live_bytes; // more dead than live

    // Serializes the given tiles into one in-memory buffer and writes them to the blob with a single
    // write() call (instead of one per tile, which dominated the cost of the naive per-tile-write version),
    // recording each tile's resulting offset/length in m_disk_cached.
    const auto write_tiles_to_blob
        = [&](const std::vector<std::pair<tile::Id, const CacheObject*>>& items, uint64_t base_offset, QIODeviceBase::OpenMode mode) -> tl::expected<uint64_t, QString> {
        if (items.empty())
            return uint64_t(0);
        std::vector<char> buffer;
        // no_fit_size: without it, zpp::bits::out shrinks the buffer to fit after every single out() call,
        // which for repeated appends into one shared buffer turns every write into a full realloc+copy of
        // everything written so far (O(n^2) over the tile count). We only ever read out.position(), so the
        // buffer's trailing slack from over-allocation is harmless and never touches disk.
        zpp::bits::out out(buffer, zpp::bits::no_fit_size {});
        const std::remove_cvref_t<decltype(T::version_information)> version = T::version_information;
        for (const auto& [id, cache_object] : items) {
            const uint64_t offset = base_offset + uint64_t(out.position());
            {
                const auto r = out(version);
                if (failure(r))
                    return unexpected_error(r);
            }
            {
                const auto r = out(cache_object->data);
                if (failure(r))
                    return unexpected_error(r);
            }
            m_disk_cached[id] = { cache_object->meta.visited, cache_object->meta.created, offset, base_offset + uint64_t(out.position()) - offset };
        }
        const uint64_t size = uint64_t(out.position());
        QFile blob_file(blob);
        if (!blob_file.open(mode))
            return tl::unexpected<QString>(QString("Couldn't open file '%1' for writing!").arg(QString::fromStdString(blob.string())));
        blob_file.write(buffer.data(), qint64(size));
        return size;
    };

    if (compact) {
        const auto compact_start = std::chrono::steady_clock::now();

        std::vector<std::pair<tile::Id, const CacheObject*>> items;
        items.reserve(data.size());
        for (const auto& item : data)
            items.emplace_back(item.first, &item.second);

        const auto r = write_tiles_to_blob(items, 0, QIODeviceBase::WriteOnly); // WriteOnly truncates by default
        if (!r.has_value())
            return tl::unexpected(r.error());

        const double reclaimed_percent = old_blob_size > 0 ? 100.0 * double(old_blob_size - *r) / double(old_blob_size) : 0.0;
        qInfo() << QString("Cache::write_to_disk(%1) compacted %2 -> %3 bytes (-%4%) for %5 tiles in %6ms.")
                       .arg(QString::fromStdString(base_path.filename().string()))
                       .arg(old_blob_size)
                       .arg(*r)
                       .arg(reclaimed_percent, 0, 'f', 1)
                       .arg(data.size())
                       .arg(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - compact_start).count());
    } else {
        std::vector<std::pair<tile::Id, const CacheObject*>> items;
        for (const auto& item : data) {
            const tile::Id& id = item.first;
            const CacheObject& cache_object = item.second;

            if (disk_cached_old.contains(id) && disk_cached_old.at(id).created == cache_object.meta.created) {
                // unchanged payload: keep its existing blob location, just refresh visited/created
                auto entry = disk_cached_old.at(id);
                entry.visited = cache_object.meta.visited;
                entry.created = cache_object.meta.created;
                m_disk_cached[id] = entry;
                continue;
            }

            items.emplace_back(id, &cache_object);
        }

        const auto r = write_tiles_to_blob(items, old_blob_size, QIODeviceBase::Append);
        if (!r.has_value())
            return tl::unexpected(r.error());
    }

    std::vector<char> bytes;
    zpp::bits::out out(bytes);
    const std::remove_cvref_t<decltype(T::version_information)> version = T::version_information;
    {
        const auto r = out(version);
        if (failure(r))
            return unexpected_error(r);
    }

    {
        const auto r = out(m_disk_cached);
        if (failure(r))
            return unexpected_error(r);
    }

    const auto r = write(bytes, meta_info_path(base_path), QIODeviceBase::WriteOnly);
    if (!r.has_value())
        return r;

    qInfo() << QString("Cache::write_to_disk(%1) wrote %2 tiles (%3) in %4ms.")
                   .arg(QString::fromStdString(base_path.filename().string()))
                   .arg(data.size())
                   .arg(compact ? "compacted" : "incremental")
                   .arg(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - write_start).count());

    return {};
}

template <NamedTile T> tl::expected<void, QString> Cache<T>::read_from_disk(const std::filesystem::path& base_path)
{
    const auto unexpected_error = [](const auto& e) { return tl::unexpected(QString::fromStdString(std::make_error_code(e).message())); };
    auto locker = std::scoped_lock(m_data_mutex, m_disk_cached_mutex);
    assert(SerialisableTile<T>);
    const auto check_version = [&unexpected_error](auto* in, const auto& path) -> tl::expected<void, QString> {
        std::remove_cvref_t<decltype(T::version_information)> version_info = {};
        {
            const auto r = (*in)(version_info);
            if (failure(r))
                return unexpected_error(r);
        }
        if (version_info != T::version_information) {
            version_info[version_info.size() - 1] = 0;  // make sure that the string is 0 terminated.

            return tl::unexpected(QString("Cache file '%1' has incompatible version! Disk "
                                          "version is '%2', but we expected '%3'.")
                                      .arg(QString::fromStdString(path.string()))
                                      .arg(version_info.data())
                                      .arg(T::version_information.data()));
        }
        return {};
    };
    const auto read_all = [](const auto& path) -> tl::expected<QByteArray, QString> {
        QFile file(path);
        const auto success = file.open(QIODeviceBase::ReadOnly);
        if (!success)
            return tl::unexpected(QString("Couldn't open file '%1' for reading!").arg(QString::fromStdString(path.string())));
        return file.readAll();
    };
    const auto clean_up = [&]() {
        m_disk_cached.clear();
        m_data.clear();
    };

    clean_up();
    {
        const auto path = meta_info_path(base_path);
        const auto bytes = read_all(path);
        if (!bytes.has_value()) {
            clean_up();
            return tl::unexpected(bytes.error());
        }
        zpp::bits::in in(bytes.value());
        {
            const auto r = check_version(&in, path);
            if (!r.has_value()) {
                clean_up();
                return r;
            }
        }
        {
            const auto r = in(m_disk_cached);
            if (failure(r)) {
                clean_up();
                return unexpected_error(r);
            }
        }
    }

    const auto blob = blob_path(base_path);
    const auto blob_bytes = read_all(blob);
    if (!blob_bytes.has_value()) {
        clean_up();
        return tl::unexpected(blob_bytes.error());
    }

    for (const auto& entry : m_disk_cached) {
        const tile::Id& id = entry.first;
        const MetaData& meta = entry.second;

        if (meta.offset + meta.length > uint64_t(blob_bytes->size())) {
            // Index references bytes the blob doesn't actually have -- corrupt or truncated by a crash.
            clean_up();
            return tl::unexpected(QString("Tile cache blob '%1' is smaller than the index expects (corrupt or truncated).")
                                      .arg(QString::fromStdString(blob.string())));
        }

        zpp::bits::in in(std::span<const char>(blob_bytes->constData() + meta.offset, size_t(meta.length)));
        {
            const auto r = check_version(&in, blob);
            if (!r.has_value()) {
                clean_up();
                return r;
            }
        }

        CacheObject d;
        {
            const auto r = in(d.data);
            if (failure(r)) {
                clean_up();
                return unexpected_error(r);
            }
        }
        d.meta = meta;
        m_data[d.data.id] = d;
    }

    return {};
}

template <NamedTile T>
template <typename VisitorFunction>
void Cache<T>::visit(const VisitorFunction& functor)
{
    auto locker = std::scoped_lock(m_data_mutex);
    const auto visited = nucleus::utils::time_since_epoch();
    static_assert(
        requires {
            { functor(T()) } -> nucleus::utils::convertible_to<bool>;
        }, "VisitorFunction must accept a const NamedTile and return a bool.");
    const auto root = tile::Id { 0, { 0, 0 } };
    visit(root, functor, visited);
}

template <NamedTile T>
template <typename VisitorFunction>
void Cache<T>::visit(const tile::Id& node, const VisitorFunction& functor, uint64_t visited_stamp)
{
    static_assert(requires {
        { functor(T()) } -> nucleus::utils::convertible_to<bool>;
    });
    if (m_data.contains(node)) {
        const auto should_continue = functor(m_data[node].data);
        if (!should_continue)
            return;
        m_data[node].meta.visited = visited_stamp * 100 - m_data[node].data.id.zoom_level;
        const auto children = node.children();
        for (const auto& id : children) {
            visit(id, functor, visited_stamp);
        }
    }
}

template<NamedTile T>
std::vector<T> Cache<T>::purge(unsigned remaining_capacity)
{
    auto locker = std::scoped_lock(m_data_mutex);
    if (remaining_capacity >= m_data.size())
        return {};
    std::vector<std::pair<tile::Id, uint64_t>> tiles;
    tiles.reserve(m_data.size());
    std::transform(m_data.cbegin(), m_data.cend(), std::back_inserter(tiles), [](const auto& entry) { return std::make_pair(entry.first, entry.second.meta.visited); });
    const auto nth_iter = tiles.begin() + remaining_capacity;
    std::nth_element(tiles.begin(), nth_iter, tiles.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    std::vector<T> purged_tiles;
    purged_tiles.reserve(tiles.size() - remaining_capacity);
    std::for_each(nth_iter, tiles.end(), [this, &purged_tiles](const auto& v) {
        purged_tiles.push_back(m_data[v.first].data);
        m_data.erase(v.first);
    });
    return purged_tiles;
}

} // namespace nucleus::tile
