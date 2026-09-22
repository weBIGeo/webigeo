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

#include "TilePlanner.h"

#include <algorithm>

namespace nucleus::tile {

namespace {
    struct PendingRequest {
        tile::Id id;
        unsigned tier; // 1 = anchor, 2 = bounded fallback, 3 = the wanted tile itself; lower sorts first
        unsigned zoom;
        uint32_t pixel_count;
        unsigned gap;
    };

    // Strict weak (in fact total, via the id tie-break) order: true if `a` should be planned before `b`.
    bool better(const PendingRequest& a, const PendingRequest& b, TilePlanner::Order order)
    {
        if (a.tier != b.tier)
            return a.tier < b.tier;
        if (order == TilePlanner::Order::ZoomDescThenCount) {
            if (a.zoom != b.zoom)
                return a.zoom > b.zoom;
            if (a.pixel_count != b.pixel_count)
                return a.pixel_count > b.pixel_count;
            return a.id < b.id;
        }
        const uint64_t key_a = uint64_t(a.pixel_count) * (1 + uint64_t(a.gap));
        const uint64_t key_b = uint64_t(b.pixel_count) * (1 + uint64_t(b.gap));
        if (key_a != key_b)
            return key_a > key_b;
        return a.id < b.id;
    }
} // namespace

TilePlanner::Plan TilePlanner::make(std::span<const WantedTile> wanted, const Params& params, const std::function<TileState(const tile::Id&)>& state_of)
{
    tile::IdMap<PendingRequest> requests;
    tile::IdSet touched;
    std::vector<Outcome> outcome(wanted.size(), Outcome::Requested);

    // Walks up past tiles that are known to be missing, to the deepest id still worth asking for.
    //
    // Deliberately only skips tombstones it walks *through*, i.e. a tile is only substituted if it is
    // itself known-missing. D4 originally said a tombstone at T means nothing below T exists either, so
    // descendants should never be requested -- that is not true of real tile services: a 404 at a low
    // zoom usually just means the set has a minimum zoom level (or no overview at that level), while
    // every tile below it is served fine. Suppressing the subtree marked large, perfectly available
    // regions as missing. Each descendant of a hole is therefore requested once and tombstoned on its
    // own 404; the wanted list only ever contains tiles the camera is actually looking at, so the extra
    // requests are bounded and happen once per tile, not once per frame.
    const auto substitute = [&](tile::Id id) {
        while (id.zoom_level > 0 && state_of(id).tombstone)
            id = id.parent();
        return id;
    };

    const auto propose = [&](const tile::Id& id, unsigned tier, unsigned zoom, uint32_t pixel_count, unsigned gap) {
        const PendingRequest candidate { id, tier, zoom, pixel_count, gap };
        const auto it = requests.find(id);
        if (it == requests.end()) {
            requests.emplace(id, candidate);
            return;
        }
        if (better(candidate, it->second, params.order))
            it->second = candidate;
    };

    for (size_t i = 0; i < wanted.size(); ++i) {
        const auto& wanted_tile = wanted[i];
        // 1. Tombstone substitution: a known-missing tile's descendants are never requested.
        const tile::Id e = substitute(wanted_tile.id);
        // Substituted at all -> this exact tile does not exist, whatever happens to its substitute
        // below. Takes precedence over Resident/Skipped: it is the only outcome that will never change.
        const bool unservable = !(e == wanted_tile.id);
        if (unservable)
            outcome[i] = Outcome::NoData;
        if (state_of(e).tombstone) {
            // e itself is a tombstone: only possible at zoom 0, substitute() can't walk past the root.
            // Without this, a 404'd root would be proposed and requested again every single update().
            outcome[i] = Outcome::NoData;
            continue;
        }

        // 2. Nearest resident ancestor-or-self; this is what the shader is actually sampling right now.
        tile::Id r = e;
        unsigned gap = 0;
        bool found = state_of(r).on_gpu;
        while (!found && r.zoom_level > 0) {
            r = r.parent();
            ++gap;
            found = state_of(r).on_gpu;
        }
        if (found) {
            touched.insert(r);
            if (gap == 0) {
                if (!unservable)
                    outcome[i] = Outcome::Resident;
                continue; // exact match already resident, nothing to request
            }
        }
        // else: gap == e.zoom_level (walked all the way to a non-resident root) -- treat as "far away" below.

        // 3. A close-enough fallback is good enough for a tile that barely covers any screen pixels.
        if (wanted_tile.pixel_count < params.min_pixels && gap <= params.max_gap) {
            if (!unservable)
                outcome[i] = Outcome::Skipped;
            continue;
        }

        // 4. Anchors (Tier 1): always keep coverage from anchor_zoom up to (excluding) this tile's zoom.
        {
            tile::Id a = e;
            while (a.zoom_level > 0) {
                a = a.parent();
                if (a.zoom_level < params.anchor_zoom)
                    break;
                const auto s = state_of(a);
                if (!s.on_gpu && !s.tombstone)
                    propose(a, 1, a.zoom_level, wanted_tile.pixel_count, e.zoom_level - a.zoom_level);
            }
        }

        // 5. Bounded fallback (Tier 2): the resident ancestor found in step 2 is too far away, so also
        // request one that's at most max_gap levels up (itself substituted past any tombstone).
        if (gap > params.max_gap) {
            tile::Id b = e;
            unsigned steps = 0;
            while (steps < params.max_gap && b.zoom_level > 0) {
                b = b.parent();
                ++steps;
            }
            b = substitute(b);
            if (!state_of(b).on_gpu)
                propose(b, 2, b.zoom_level, wanted_tile.pixel_count, params.max_gap);
        }

        // 6. The tile itself (Tier 3).
        if (!state_of(e).on_gpu)
            propose(e, 3, e.zoom_level, wanted_tile.pixel_count, gap);
    }

    // 7. Split into ship/fetch (dropping blocked ids), then order each list.
    std::vector<PendingRequest> ship_requests;
    std::vector<PendingRequest> fetch_requests;
    for (const auto& [id, request] : requests) {
        const auto s = state_of(id);
        if (s.blocked)
            continue;
        if (s.in_ram_fresh)
            ship_requests.push_back(request);
        else
            fetch_requests.push_back(request);
    }

    const auto order_cmp = [order = params.order](const PendingRequest& a, const PendingRequest& b) { return better(a, b, order); };
    std::sort(ship_requests.begin(), ship_requests.end(), order_cmp);
    std::sort(fetch_requests.begin(), fetch_requests.end(), order_cmp);

    Plan plan;
    plan.ship.reserve(ship_requests.size());
    for (const auto& request : ship_requests)
        plan.ship.push_back(request.id);
    plan.fetch.reserve(fetch_requests.size());
    for (const auto& request : fetch_requests)
        plan.fetch.push_back(request.id);
    plan.touch.assign(touched.begin(), touched.end());
    plan.outcome = std::move(outcome);
    return plan;
}

} // namespace nucleus::tile
