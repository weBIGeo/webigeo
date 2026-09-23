/*****************************************************************************
 * AlpineMaps.org
 * Copyright (C) 2023 Adam Celarek
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

#include "types.h"
#include <QFile>
#include <algorithm>
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

// The tile cache is written sequentially leaving dead bytes. But when the blob file
// contains disk_cache_compaction_threshold multiple of alive bytes we completely rewrite
// the file on disk
constexpr double disk_cache_compaction_threshold = 2.0;

/// This class is thread safe. be careful with the visit method as it writes the cache and therefore locks an internal mutex.
template<NamedTile T>
class Cache
{
    struct MetaData {
        uint64_t visited;
        uint64_t created;
        uint64_t offset; // byte offset in the tile_cache file
        uint64_t length; // length in bytes of this tile
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

    static std::filesystem::path blob_path(const std::filesystem::path& base_path) { return base_path / "tile_cache.alp"; }

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
    const auto unexpected_error = [](const auto& e) { return tl::unexpected(QString::fromStdString(std::make_error_code(e).message())); };
    static_assert(SerialisableTile<T>);
    std::filesystem::create_directories(base_path);
    // We create a vector instead of a map as its only ever iterated
    std::vector<std::pair<tile::Id, CacheObject>> data;
    {
        auto locker = std::scoped_lock(m_data_mutex);
        data.reserve(m_data.size());
        for (const auto& item : m_data)
            data.emplace_back(item.first, item.second); // copies metadata and references to tiles
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

    // Decides on whether its time to compact the cache (deletes dead bytes)
    const auto blob = blob_path(base_path);
    const uint64_t old_blob_size = std::filesystem::exists(blob) ? uint64_t(std::filesystem::file_size(blob)) : 0;
    uint64_t old_live_bytes = 0;
    for (const auto& item : disk_cached_old)
        old_live_bytes += item.second.length;
    const uint64_t dead_bytes = old_blob_size > old_live_bytes ? old_blob_size - old_live_bytes : 0;
    const bool compact = old_live_bytes > 0 && double(dead_bytes) > disk_cache_compaction_threshold * double(old_live_bytes);

    // We serialize all (updated) tiles into memory first to execute only one draw command
    const auto write_tiles_to_blob
        = [&](const std::vector<std::pair<tile::Id, const CacheObject*>>& items, uint64_t base_offset, QIODeviceBase::OpenMode mode) -> tl::expected<uint64_t, QString> {
        if (items.empty())
            return uint64_t(0);
        std::vector<char> buffer;
        // IMPORTANT: zpp::bits::no_fit_size necessary, otherwise zpp_bits shrink the buffer for every single
        // tile and we would have full reallocations for the tile count which destroys the whole
        // gain we get from serializing them into memory first...
        zpp::bits::out out(buffer, zpp::bits::no_fit_size {});
        for (const auto& [id, cache_object] : items) {
            const uint64_t offset = base_offset + uint64_t(out.position());
            {
                const auto r = out(T::version_information);
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
        std::vector<std::pair<tile::Id, const CacheObject*>> items;
        items.reserve(data.size());
        for (const auto& item : data)
            items.emplace_back(item.first, &item.second);

        const auto r = write_tiles_to_blob(items, 0, QIODeviceBase::WriteOnly); // WriteOnly truncates by default
        if (!r.has_value())
            return tl::unexpected(r.error());
    } else {
        std::vector<std::pair<tile::Id, const CacheObject*>> items;
        for (const auto& item : data) {
            const tile::Id& id = item.first;
            const CacheObject& cache_object = item.second;

            const auto old_entry = disk_cached_old.find(id);
            if (old_entry != disk_cached_old.end() && old_entry->second.created == cache_object.meta.created) {
                // unchanged payload: keep the location but refresh marker
                auto entry = old_entry->second;
                entry.visited = cache_object.meta.visited;
                entry.created = cache_object.meta.created;
                m_disk_cached.emplace(id, entry);
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
    {
        const auto r = out(T::version_information);
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

    return {};
}

template <NamedTile T> tl::expected<void, QString> Cache<T>::read_from_disk(const std::filesystem::path& base_path)
{
    const auto unexpected_error = [](const auto& e) { return tl::unexpected(QString::fromStdString(std::make_error_code(e).message())); };
    auto locker = std::scoped_lock(m_data_mutex, m_disk_cached_mutex);
    assert(SerialisableTile<T>);
    const auto check_version = [&unexpected_error](auto* in, const auto& path) -> tl::expected<void, QString> {
        auto version_info = T::version_information; 
        {
            const auto r = (*in)(version_info);
            if (failure(r))
                return unexpected_error(r);
        }
        if (version_info != T::version_information) {
            version_info[version_info.size() - 1] = 0;  // check 0 termination of string 

            return tl::unexpected(QString("File '%1' has incompatible version! ('%2', expected '%3')")
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
            return tl::unexpected(QString("Couldnt open '%1' for reading").arg(QString::fromStdString(path.string())));
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

    // Reading in blob order keeps the walk through the buffer sequential for better cache alignment
    std::vector<MetaData> entries;
    entries.reserve(m_disk_cached.size());
    for (const auto& entry : m_disk_cached)
        entries.push_back(entry.second);
    std::sort(entries.begin(), entries.end(), [](const MetaData& a, const MetaData& b) { return a.offset < b.offset; });
    m_data.reserve(entries.size());

    for (const MetaData& meta : entries) {
        if (meta.offset + meta.length > uint64_t(blob_bytes->size())) {
            clean_up();
            return tl::unexpected(QString("Tile cache blob %1 is smaller than expected (corrupt or truncated).")
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
        const auto id = d.data.id;
        m_data.emplace(id, std::move(d));
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
