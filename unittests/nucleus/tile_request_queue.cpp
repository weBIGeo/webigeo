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

#include <QSignalSpy>
#include <catch2/catch_test_macros.hpp>
#include <chrono>

#include "nucleus/tile/TileRequestQueue.h"
#include "radix/tile.h"

using namespace nucleus::tile;

TEST_CASE("nucleus/tile/tile request queue")
{
    SECTION("doesn't move when requesting empty array")
    {
        TileRequestQueue q;
        QSignalSpy spy(&q, &TileRequestQueue::tile_requested);
        q.set_requests({});
        CHECK(spy.size() == 0);
        CHECK(q.in_flight() == 0);
        CHECK(q.pending() == 0);
    }

    SECTION("sends requests up to the limit, queues the rest")
    {
        TileRequestQueue q;
        q.set_limit(2);
        QSignalSpy spy(&q, &TileRequestQueue::tile_requested);
        q.set_requests({ Id { 0, { 0, 0 } }, Id { 1, { 0, 0 } }, Id { 1, { 0, 1 } } });
        CHECK(q.in_flight() == 2);
        CHECK(q.pending() == 1);
        REQUIRE(spy.size() == 2);
        CHECK(spy[0][0].value<Id>() == Id { 0, { 0, 0 } });
        CHECK(spy[1][0].value<Id>() == Id { 1, { 0, 0 } });
    }

    SECTION("delivery frees a slot and promotes the next pending id in order")
    {
        TileRequestQueue q;
        q.set_limit(1);
        QSignalSpy spy(&q, &TileRequestQueue::tile_requested);
        q.set_requests({ Id { 0, { 0, 0 } }, Id { 1, { 0, 0 } }, Id { 1, { 0, 1 } } });
        REQUIRE(spy.size() == 1);
        CHECK(spy[0][0].value<Id>() == Id { 0, { 0, 0 } });

        q.tile_delivered(Id { 0, { 0, 0 } });
        CHECK(q.in_flight() == 1);
        CHECK(q.pending() == 1);
        REQUIRE(spy.size() == 2);
        CHECK(spy[1][0].value<Id>() == Id { 1, { 0, 0 } });

        q.tile_delivered(Id { 1, { 0, 0 } });
        REQUIRE(spy.size() == 3);
        CHECK(spy[2][0].value<Id>() == Id { 1, { 0, 1 } });

        q.tile_delivered(Id { 1, { 0, 1 } });
        CHECK(q.in_flight() == 0);
        CHECK(q.pending() == 0);
        CHECK(spy.size() == 3);
    }

    SECTION("replan aborts a dropped in-flight id and immediately reuses its slot")
    {
        TileRequestQueue q;
        q.set_limit(2);
        QSignalSpy requested(&q, &TileRequestQueue::tile_requested);
        QSignalSpy aborted(&q, &TileRequestQueue::tile_aborted);
        q.set_requests({ Id { 0, { 0, 0 } }, Id { 1, { 0, 0 } } });
        REQUIRE(requested.size() == 2);
        CHECK(q.in_flight() == 2);

        // Drop Id{0,{0,0}}, keep Id{1,{0,0}}, add a new one -- net in-flight count stays the same.
        q.set_requests({ Id { 1, { 0, 0 } }, Id { 2, { 0, 0 } } });
        REQUIRE(aborted.size() == 1);
        CHECK(aborted[0][0].value<Id>() == Id { 0, { 0, 0 } });
        CHECK(q.in_flight() == 2);
        CHECK(q.pending() == 0);
        REQUIRE(requested.size() == 3);
        CHECK(requested[2][0].value<Id>() == Id { 2, { 0, 0 } });
    }

    SECTION("an id that's in flight and still wanted is neither re-requested nor aborted")
    {
        TileRequestQueue q;
        q.set_limit(1);
        QSignalSpy requested(&q, &TileRequestQueue::tile_requested);
        QSignalSpy aborted(&q, &TileRequestQueue::tile_aborted);
        q.set_requests({ Id { 0, { 0, 0 } } });
        REQUIRE(requested.size() == 1);

        q.set_requests({ Id { 0, { 0, 0 } } });
        CHECK(requested.size() == 1);
        CHECK(aborted.size() == 0);
        CHECK(q.in_flight() == 1);
    }

    SECTION("set_requests preserves the given order for anything not already in flight")
    {
        TileRequestQueue q;
        q.set_limit(1);
        QSignalSpy spy(&q, &TileRequestQueue::tile_requested);
        q.set_requests({ Id { 5, { 0, 0 } }, Id { 3, { 0, 0 } }, Id { 4, { 0, 0 } } });
        REQUIRE(spy.size() == 1);
        CHECK(spy[0][0].value<Id>() == Id { 5, { 0, 0 } });

        q.tile_delivered(Id { 5, { 0, 0 } });
        REQUIRE(spy.size() == 2);
        CHECK(spy[1][0].value<Id>() == Id { 3, { 0, 0 } });

        q.tile_delivered(Id { 3, { 0, 0 } });
        REQUIRE(spy.size() == 3);
        CHECK(spy[2][0].value<Id>() == Id { 4, { 0, 0 } });
    }

    SECTION("a delivery for an id already removed by a prior abort is a harmless no-op")
    {
        TileRequestQueue q;
        q.set_limit(1);
        QSignalSpy requested(&q, &TileRequestQueue::tile_requested);
        QSignalSpy aborted(&q, &TileRequestQueue::tile_aborted);
        q.set_requests({ Id { 0, { 0, 0 } } });
        REQUIRE(requested.size() == 1);

        q.set_requests({ Id { 1, { 0, 0 } } });
        REQUIRE(aborted.size() == 1);
        CHECK(q.in_flight() == 1); // Id{1,...} took the freed slot
        REQUIRE(requested.size() == 2);

        // The load service's late reply for the aborted id arrives anyway; must not disturb bookkeeping.
        q.tile_delivered(Id { 0, { 0, 0 } });
        CHECK(q.in_flight() == 1);
        CHECK(q.pending() == 0);
        CHECK(requested.size() == 2);
    }

    SECTION("a rate limit holds ids back although slots are free, and releases them once the window has passed")
    {
        TileRequestQueue q;
        q.set_rate_limit(2, 200);
        QSignalSpy spy(&q, &TileRequestQueue::tile_requested);
        const auto clock_start = std::chrono::steady_clock::now();
        q.set_requests({ Id { 0, { 0, 0 } }, Id { 1, { 0, 0 } }, Id { 1, { 0, 1 } } });
        CHECK(spy.size() == 2);
        CHECK(q.in_flight() == 2);
        CHECK(q.pending() == 1);

        REQUIRE(spy.wait(2000));
        const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - clock_start).count();
        CHECK(elapsed_ms >= 200);
        REQUIRE(spy.size() == 3);
        CHECK(spy[2][0].value<Id>() == Id { 1, { 0, 1 } });
        CHECK(q.pending() == 0);
    }

    SECTION("a delivery frees a slot but does not skip the rate window")
    {
        TileRequestQueue q;
        q.set_rate_limit(1, 200);
        QSignalSpy spy(&q, &TileRequestQueue::tile_requested);
        q.set_requests({ Id { 0, { 0, 0 } }, Id { 1, { 0, 0 } } });
        REQUIRE(spy.size() == 1);

        q.tile_delivered(Id { 0, { 0, 0 } });
        CHECK(spy.size() == 1);
        CHECK(q.in_flight() == 0);
        CHECK(q.pending() == 1);

        REQUIRE(spy.wait(2000));
        REQUIRE(spy.size() == 2);
        CHECK(spy[1][0].value<Id>() == Id { 1, { 0, 0 } });
    }

    SECTION("a replan can drop an id that is still waiting for the rate window, it is then never requested")
    {
        TileRequestQueue q;
        q.set_rate_limit(1, 200);
        QSignalSpy requested(&q, &TileRequestQueue::tile_requested);
        QSignalSpy aborted(&q, &TileRequestQueue::tile_aborted);
        q.set_requests({ Id { 0, { 0, 0 } }, Id { 1, { 0, 0 } }, Id { 1, { 0, 1 } } });
        REQUIRE(requested.size() == 1);
        CHECK(q.pending() == 2);

        q.set_requests({ Id { 0, { 0, 0 } }, Id { 1, { 0, 1 } } }); // Id{1,{0,0}} was only waiting, not in flight
        CHECK(aborted.size() == 0);
        CHECK(q.pending() == 1);

        REQUIRE(requested.wait(2000));
        REQUIRE(requested.size() == 2);
        CHECK(requested[1][0].value<Id>() == Id { 1, { 0, 1 } });
        CHECK(q.pending() == 0);
    }

    SECTION("switching the rate limit off releases everything that was waiting for it")
    {
        TileRequestQueue q;
        q.set_rate_limit(1, 60000);
        QSignalSpy spy(&q, &TileRequestQueue::tile_requested);
        q.set_requests({ Id { 0, { 0, 0 } }, Id { 1, { 0, 0 } }, Id { 1, { 0, 1 } } });
        CHECK(spy.size() == 1);

        q.set_rate_limit(0, 60000);
        CHECK(spy.size() == 3);
        CHECK(q.pending() == 0);
    }
}
