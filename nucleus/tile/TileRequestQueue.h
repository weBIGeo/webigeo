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

#include <QObject>
#include <unordered_set>
#include <vector>

#include "types.h"

namespace nucleus::tile {

/// Single-tile equivalent of SlotLimiter: throttles concurrent fetches to `limit()` and keeps the rest
/// pending in priority order. Unlike SlotLimiter, a replan (set_requests) can drop an id that is currently
/// in flight -- that id is aborted (tile_aborted) and its slot freed immediately rather than left to finish,
/// since a Wanted source's demand can shift every readback and a stale in-flight request isn't worth holding.
class TileRequestQueue : public QObject {
    Q_OBJECT

    unsigned m_limit = 8;
    std::unordered_set<tile::Id, tile::Id::Hasher> m_in_flight;
    std::vector<tile::Id> m_pending;

public:
    explicit TileRequestQueue(QObject* parent = nullptr);

    void set_limit(unsigned new_limit);
    [[nodiscard]] unsigned limit() const;
    [[nodiscard]] unsigned in_flight() const;
    [[nodiscard]] unsigned pending() const;
    [[nodiscard]] bool is_in_flight(const tile::Id& id) const;
    [[nodiscard]] bool is_pending(const tile::Id& id) const;

public slots:
    /// Replaces the pending list. Ids no longer present that are still in flight are aborted immediately;
    /// ids already in flight and still present keep running (not re-requested).
    void set_requests(const std::vector<tile::Id>& ordered);
    void tile_delivered(const tile::Id& id);

signals:
    void tile_requested(const tile::Id& id);
    void tile_aborted(const tile::Id& id);
};

} // namespace nucleus::tile
