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

#include <algorithm>
#include <functional>
#include <unordered_map>

#include <catch2/catch_test_macros.hpp>

#include "nucleus/tile/TilePlanner.h"
#include "radix/tile.h"

using namespace nucleus::tile;

namespace {
using StateMap = std::unordered_map<Id, TilePlanner::TileState, Id::Hasher>;

std::function<TilePlanner::TileState(const Id&)> lookup(const StateMap& states)
{
    return [&states](const Id& id) {
        const auto it = states.find(id);
        return it == states.end() ? TilePlanner::TileState {} : it->second;
    };
}
} // namespace

TEST_CASE("nucleus/tile/tile planner")
{
    SECTION("wanted tile already resident -> only touched, nothing requested")
    {
        const Id id { 10, { 5, 5 } };
        const StateMap states { { id, { .on_gpu = true } } };
        const std::vector<WantedTile> wanted = { { id, 500 } };

        const auto plan = TilePlanner::make(wanted, TilePlanner::Params {}, lookup(states));
        CHECK(plan.fetch.empty());
        CHECK(plan.ship.empty());
        REQUIRE(plan.touch.size() == 1);
        CHECK(plan.touch[0] == id);
    }

    SECTION("low pixel count with a close resident ancestor requests nothing beyond the touch")
    {
        const Id leaf { 12, { 100, 100 } };
        const Id ancestor = leaf.parent(); // zoom 11, gap 1 (within the default max_gap of 2)
        const StateMap states { { ancestor, { .on_gpu = true } } };
        const std::vector<WantedTile> wanted = { { leaf, 100 } }; // below the default min_pixels of 256

        const auto plan = TilePlanner::make(wanted, TilePlanner::Params {}, lookup(states));
        CHECK(plan.fetch.empty());
        CHECK(plan.ship.empty());
        REQUIRE(plan.touch.size() == 1);
        CHECK(plan.touch[0] == ancestor);
    }

    SECTION("low pixel count with no resident ancestor at all is requested anyway")
    {
        TilePlanner::Params params;
        params.anchor_zoom = 10; // above the tree used here, so no anchors trigger
        const Id leaf { 3, { 1, 1 } }; // gap ends up 3 (> max_gap of 2), so step 3 can't short-circuit
        const std::vector<WantedTile> wanted = { { leaf, 10 } }; // well below min_pixels

        const auto plan = TilePlanner::make(wanted, params, lookup({}));
        CHECK(plan.touch.empty());
        REQUIRE(plan.fetch.size() == 2);
        CHECK(plan.fetch[0] == leaf.parent().parent()); // bounded fallback at zoom(leaf) - max_gap
        CHECK(plan.fetch[1] == leaf);
    }

    SECTION("gap greater than max_gap requests the bounded fallback ancestor before the tile itself")
    {
        TilePlanner::Params params;
        params.anchor_zoom = 100; // disabled for this test
        const Id leaf { 5, { 3, 3 } };
        const std::vector<WantedTile> wanted = { { leaf, 1000 } };

        const auto plan = TilePlanner::make(wanted, params, lookup({}));
        REQUIRE(plan.fetch.size() == 2);
        CHECK(plan.fetch[0] == leaf.parent().parent()); // zoom(leaf) - max_gap(2) = 3
        CHECK(plan.fetch[1] == leaf);
    }

    SECTION("anchors are requested from anchor_zoom up to (excluding) the tile's zoom, only when missing")
    {
        const Id leaf { 5, { 1, 1 } };
        const Id a4 = leaf.parent();
        const Id a3 = a4.parent();
        const Id a2 = a3.parent();
        const StateMap states { { a3, { .on_gpu = true } } }; // already resident -> must not be re-requested

        TilePlanner::Params params;
        params.anchor_zoom = 2;
        params.max_gap = 100; // keep the tier-2 bounded fallback out of this test
        const std::vector<WantedTile> wanted = { { leaf, 1000 } };

        const auto plan = TilePlanner::make(wanted, params, lookup(states));
        CHECK(std::find(plan.fetch.begin(), plan.fetch.end(), a4) != plan.fetch.end());
        CHECK(std::find(plan.fetch.begin(), plan.fetch.end(), a2) != plan.fetch.end());
        CHECK(std::find(plan.fetch.begin(), plan.fetch.end(), a3) == plan.fetch.end());
        CHECK(std::find(plan.fetch.begin(), plan.fetch.end(), leaf) != plan.fetch.end());
    }

    SECTION("a tombstone substitutes the nearest non-tombstoned ancestor and is never itself requested")
    {
        const Id leaf { 5, { 1, 1 } };
        const StateMap states { { leaf, { .tombstone = true } } };

        TilePlanner::Params params;
        params.anchor_zoom = 100; // keep this test focused on the substitution itself
        const std::vector<WantedTile> wanted = { { leaf, 1000 } };

        const auto plan = TilePlanner::make(wanted, params, lookup(states));
        CHECK(std::find(plan.fetch.begin(), plan.fetch.end(), leaf) == plan.fetch.end());
        CHECK(std::find(plan.ship.begin(), plan.ship.end(), leaf) == plan.ship.end());
        const Id substitute = leaf.parent();
        CHECK(std::find(plan.fetch.begin(), plan.fetch.end(), substitute) != plan.fetch.end());
    }

    SECTION("blocked ids are dropped from both ship and fetch, but still touched if resident")
    {
        const Id leaf { 5, { 1, 1 } };
        const Id ancestor3 = leaf.parent().parent(); // zoom 3, gap 2 (within default max_gap)
        const StateMap states { { leaf, { .blocked = true } }, { ancestor3, { .on_gpu = true } } };

        TilePlanner::Params params;
        params.anchor_zoom = 100;
        const std::vector<WantedTile> wanted = { { leaf, 1000 } };

        const auto plan = TilePlanner::make(wanted, params, lookup(states));
        CHECK(plan.fetch.empty());
        CHECK(plan.ship.empty());
        REQUIRE(plan.touch.size() == 1);
        CHECK(plan.touch[0] == ancestor3);
    }

    SECTION("in_ram_fresh tiles go to ship, not fetch")
    {
        const Id leaf { 5, { 1, 1 } };
        const StateMap states { { leaf, { .in_ram_fresh = true } } };

        TilePlanner::Params params;
        params.anchor_zoom = 100;
        const std::vector<WantedTile> wanted = { { leaf, 1000 } };

        const auto plan = TilePlanner::make(wanted, params, lookup(states));
        CHECK(std::find(plan.ship.begin(), plan.ship.end(), leaf) != plan.ship.end());
        CHECK(std::find(plan.fetch.begin(), plan.fetch.end(), leaf) == plan.fetch.end());
    }

    SECTION("Order::ZoomDescThenCount sorts by zoom descending, then pixel count descending")
    {
        TilePlanner::Params params;
        params.anchor_zoom = 100;
        params.max_gap = 100; // keep both tiles at tier 3 only
        params.min_pixels = 0; // don't let step 3 prune the low-pixel-count tile
        params.order = TilePlanner::Order::ZoomDescThenCount;
        const Id low_zoom { 3, { 0, 0 } };
        const Id high_zoom { 5, { 4, 4 } }; // unrelated branch, no shared ancestors
        const std::vector<WantedTile> wanted = { { low_zoom, 900 }, { high_zoom, 100 } };

        const auto plan = TilePlanner::make(wanted, params, lookup({}));
        REQUIRE(plan.fetch.size() == 2);
        CHECK(plan.fetch[0] == high_zoom); // higher zoom first, despite the lower pixel count
        CHECK(plan.fetch[1] == low_zoom);
    }

    SECTION("Order::CountTimesGap sorts by pixel_count * (1 + gap) descending")
    {
        const Id leaf_a { 5, { 0, 0 } };
        const Id leaf_b { 5, { 4, 4 } }; // unrelated branch
        const Id ancestor_a = leaf_a.parent(); // gap 1 for leaf_a
        const Id ancestor_b = leaf_b.parent().parent(); // gap 2 for leaf_b
        const StateMap states { { ancestor_a, { .on_gpu = true } }, { ancestor_b, { .on_gpu = true } } };

        TilePlanner::Params params; // default max_gap(2) keeps both gaps (1, 2) from triggering tier 2
        params.anchor_zoom = 100;
        params.min_pixels = 0;
        params.order = TilePlanner::Order::CountTimesGap;
        // key_a = 400 * (1+1) = 800; key_b = 300 * (1+2) = 900 -> b ranks first despite the smaller pixel count
        const std::vector<WantedTile> wanted = { { leaf_a, 400 }, { leaf_b, 300 } };

        const auto plan = TilePlanner::make(wanted, params, lookup(states));
        const auto pos = [&](const Id& id) { return std::find(plan.fetch.begin(), plan.fetch.end(), id) - plan.fetch.begin(); };
        REQUIRE(std::find(plan.fetch.begin(), plan.fetch.end(), leaf_a) != plan.fetch.end());
        REQUIRE(std::find(plan.fetch.begin(), plan.fetch.end(), leaf_b) != plan.fetch.end());
        CHECK(pos(leaf_b) < pos(leaf_a));
    }

    SECTION("identical-priority ties resolve deterministically across repeated calls")
    {
        TilePlanner::Params params;
        params.anchor_zoom = 100;
        params.min_pixels = 0;
        params.max_gap = 10; // keep both tiles at tier 3 only
        const std::vector<WantedTile> wanted = { { Id { 4, { 0, 0 } }, 500 }, { Id { 4, { 5, 5 } }, 500 } };

        const auto plan_a = TilePlanner::make(wanted, params, lookup({}));
        const auto plan_b = TilePlanner::make(wanted, params, lookup({}));
        CHECK(plan_a.fetch == plan_b.fetch);
        CHECK(plan_a.ship == plan_b.ship);
        REQUIRE(plan_a.fetch.size() == 2);
        CHECK(plan_a.fetch[0] < plan_a.fetch[1]); // equal key -> tie-break by Id
    }

    SECTION("an id proposed at multiple tiers/from multiple wanted tiles is emitted once, at its best tier")
    {
        const Id shared { 4, { 2, 2 } }; // wanted directly, and also an anchor + bounded fallback of `descendant`
        const Id descendant { 6, { 8, 8 } };
        REQUIRE(descendant.parent().parent() == shared);
        const Id unrelated { 4, { 9, 9 } }; // separate branch, tier 3 only, for a relative-order check

        TilePlanner::Params params;
        params.anchor_zoom = 3;
        params.min_pixels = 0;
        params.max_gap = 2;
        const std::vector<WantedTile> wanted = { { shared, 999 }, { descendant, 999 }, { unrelated, 999 } };

        const auto plan = TilePlanner::make(wanted, params, lookup({}));
        CHECK(std::count(plan.fetch.begin(), plan.fetch.end(), shared) == 1);
        const auto pos = [&](const Id& id) { return std::find(plan.fetch.begin(), plan.fetch.end(), id) - plan.fetch.begin(); };
        // `shared` also qualifies as an anchor (tier 1) via `descendant`; that must win the merge over its
        // tier-3 appearance (wanted directly) and tier-2 appearance (descendant's bounded fallback).
        CHECK(pos(shared) < pos(unrelated));
    }
}
