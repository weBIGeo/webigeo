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

#include <cstdint>
#include <functional>
#include <span>
#include <vector>

#include "types.h"

namespace nucleus::tile {

/// Pure policy: turns a wanted-tile list into ship/fetch/touch decisions. No Qt, no threading, no I/O --
/// DemandScheduler owns the state (GPU residency, RAM cache, in-flight/backoff) and calls make() per update.
struct TilePlanner {
    enum class Order { ZoomDescThenCount, CountTimesGap };

    struct Params {
        unsigned max_gap = 2; // ancestor levels tolerated before requesting a closer fallback (Tier 2)
        uint32_t min_pixels = 256; // below this, a close-enough ancestor fallback is accepted without requesting the tile itself
        unsigned anchor_zoom = 10; // always keep coverage from this zoom down to (excluding) the wanted tile's zoom (Tier 1)
        unsigned max_zoom = 20;
        Order order = Order::CountTimesGap;
    };

    /// Per-tile-id state, queried on demand by make() via state_of().
    struct TileState {
        bool on_gpu = false; // resident in the GPU array
        bool in_ram_fresh = false; // decoded data available and not stale -> can ship without a fetch
        bool tombstone = false; // known 404: never request this id or anything below it
        bool blocked = false; // already in flight or backing off -> drop from the plan entirely
    };

    /// Why one wanted tile was treated the way it was -- the planner's own view, for diagnostics.
    /// An id can be `Requested` here and still not end up in `fetch` (step 7 drops blocked ids);
    /// DemandScheduler refines this with what its queue and backoff map know.
    enum class Outcome : uint8_t {
        Resident, // the exact tile is already on the GPU
        NoData, // the tile or one of its ancestors is a tombstone -- it can never be served exactly
        Skipped, // below min_pixels, and a resident ancestor within max_gap is good enough
        Requested, // the tile (or a tier 1/2 substitute) went into ship/fetch
    };

    struct Plan {
        std::vector<tile::Id> ship; // in_ram_fresh ids to decode/upload
        std::vector<tile::Id> fetch; // ids to request over the network, in priority order
        std::vector<tile::Id> touch; // ids to keep alive (LRU) because the shader is currently using them
        std::vector<Outcome> outcome; // parallel to the `wanted` span passed to make()
    };

    static Plan make(std::span<const WantedTile> wanted, const Params& params, const std::function<TileState(const tile::Id&)>& state_of);
};

} // namespace nucleus::tile
