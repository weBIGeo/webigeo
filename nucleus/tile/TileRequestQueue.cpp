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

#include <QTimer>
#include <algorithm>
#include <chrono>

using namespace nucleus::tile;

namespace {
// Monotonic on purpose: a wall-clock jump must not stall or flood the limiter.
uint64_t steady_now_ms() { return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()); }
} // namespace

TileRequestQueue::TileRequestQueue(QObject* parent)
    : QObject { parent }
    , m_rate_timer(std::make_unique<QTimer>(this))
{
    m_rate_timer->setSingleShot(true);
    connect(m_rate_timer.get(), &QTimer::timeout, this, &TileRequestQueue::start_pending);
}

TileRequestQueue::~TileRequestQueue() = default;

void TileRequestQueue::set_limit(unsigned new_limit)
{
    assert(new_limit > 0);
    m_limit = new_limit;
}

void TileRequestQueue::set_rate_limit(unsigned rate, unsigned period_msecs)
{
    assert(period_msecs > 0);
    m_rate = rate;
    m_rate_period_msecs = period_msecs;
    if (m_rate == 0) {
        m_start_times.clear();
        m_rate_timer->stop();
    }
    start_pending(); // a raised limit may let waiting ids go right away
}

std::pair<unsigned, unsigned> TileRequestQueue::rate_limit() const { return { m_rate, m_rate_period_msecs }; }

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

    start_pending();
}

void TileRequestQueue::tile_delivered(const tile::Id& id)
{
    if (m_in_flight.erase(id) == 0)
        return; // already dropped by a prior abort; don't double-release a slot

    start_pending();
}

void TileRequestQueue::start_pending()
{
    const auto now = steady_now_ms();
    if (m_rate > 0) {
        while (!m_start_times.empty() && m_start_times.front() + m_rate_period_msecs <= now)
            m_start_times.pop_front();
    }

    while (!m_pending.empty() && m_in_flight.size() < m_limit) {
        if (m_rate > 0) {
            if (m_start_times.size() >= m_rate) {
                // Slots are free but the window is full, and no delivery or replan is guaranteed to wake us for that.
                // (Re-arming an already running timer is harmless: the deadline it computes is the same absolute time.)
                m_rate_timer->start(int(m_start_times.front() + m_rate_period_msecs - now) + 1);
                return;
            }
            m_start_times.push_back(now);
        }
        const auto id = m_pending.front();
        m_pending.erase(m_pending.begin());
        m_in_flight.insert(id);
        emit tile_requested(id);
    }
}
