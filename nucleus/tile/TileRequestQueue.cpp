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

#include "TileRequestQueue.h"

#include <algorithm>

using namespace nucleus::tile;

TileRequestQueue::TileRequestQueue(QObject* parent)
    : QObject { parent }
{
}

void TileRequestQueue::set_limit(unsigned new_limit)
{
    assert(new_limit > 0);
    m_limit = new_limit;
}

unsigned TileRequestQueue::limit() const { return m_limit; }
unsigned TileRequestQueue::in_flight() const { return unsigned(m_in_flight.size()); }
unsigned TileRequestQueue::pending() const { return unsigned(m_pending.size()); }
bool TileRequestQueue::is_in_flight(const tile::Id& id) const { return m_in_flight.contains(id); }
bool TileRequestQueue::is_pending(const tile::Id& id) const { return std::find(m_pending.begin(), m_pending.end(), id) != m_pending.end(); }

void TileRequestQueue::set_requests(const std::vector<tile::Id>& ordered)
{
    const std::unordered_set<tile::Id, tile::Id::Hasher> wanted(ordered.begin(), ordered.end());

    // Abort in-flight ids that fell out of the plan; free their slots immediately rather than waiting
    // for them to finish, since the next update() may already want something else entirely.
    for (auto it = m_in_flight.begin(); it != m_in_flight.end();) {
        if (wanted.contains(*it)) {
            ++it;
        } else {
            const auto id = *it;
            it = m_in_flight.erase(it);
            emit tile_aborted(id);
        }
    }

    // Rebuild pending, preserving the plan's order (unlike SlotLimiter, request order is meaningful here).
    m_pending.clear();
    m_pending.reserve(ordered.size());
    for (const auto& id : ordered) {
        if (!m_in_flight.contains(id))
            m_pending.push_back(id);
    }

    while (!m_pending.empty() && m_in_flight.size() < m_limit) {
        const auto id = m_pending.front();
        m_pending.erase(m_pending.begin());
        m_in_flight.insert(id);
        emit tile_requested(id);
    }
}

void TileRequestQueue::tile_delivered(const tile::Id& id)
{
    if (m_in_flight.erase(id) == 0)
        return; // already dropped by a prior abort; don't double-release a slot

    if (m_pending.empty())
        return;

    const auto next = m_pending.front();
    m_pending.erase(m_pending.begin());
    m_in_flight.insert(next);
    emit tile_requested(next);
}
