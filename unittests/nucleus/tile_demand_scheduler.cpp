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

#include <QFile>
#include <QSignalSpy>
#include <QThread>
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <initializer_list>
#include <memory>

#include "nucleus/tile/DemandScheduler.h"
#include "nucleus/tile/setup.h"
#include "nucleus/tile/types.h"
#include "radix/tile.h"

using namespace nucleus::tile;

namespace {

DemandScheduler::Settings base_settings()
{
    DemandScheduler::Settings s;
    s.update_timeout = 1'000'000; // timers never fire, the tests call update() directly
    s.purge_timeout = 1'000'000;
    s.persist_timeout = 1'000'000;
    s.planner.anchor_zoom = 100; // no anchors
    s.planner.min_pixels = 0; // no pruning of low pixel count tiles
    s.planner.max_gap = 100; // no bounded fallback tier, only the tile itself is requested
    return s;
}

std::unique_ptr<DemandScheduler> make_scheduler(const DemandScheduler::Settings& settings = base_settings())
{
    auto sch = std::make_unique<DemandScheduler>(settings);
    sch->set_enabled(true);
    return sch;
}

QByteArray read_test_file(const char* name)
{
    QFile file(QString("%1%2").arg(ALP_TEST_DATA_DIR, name));
    REQUIRE(file.open(QFile::ReadOnly));
    return file.readAll();
}

const QByteArray& tile_256px() // 256x256 jpeg
{
    static const auto data = read_test_file("test-tile_ortho.jpeg");
    return data;
}

Data delivery(const Id& id, NetworkInfo::Status status = NetworkInfo::Status::Good, const QByteArray& payload = tile_256px())
{
    const bool has_payload = status == NetworkInfo::Status::Good;
    return Data { id, { status, nucleus::utils::time_since_epoch() }, std::make_shared<QByteArray>(has_payload ? payload : QByteArray()) };
}

std::vector<WantedTile> wanted(std::initializer_list<WantedTile> l) { return std::vector<WantedTile>(l); }

// three unrelated zoom 5 tiles with unrelated ancestors
const Id x1 { 5, { 1, 1 } };
const Id x2 { 5, { 4, 4 } };
const Id x3 { 5, { 9, 9 } };

std::vector<Id> requested_ids(const QSignalSpy& spy)
{
    std::vector<Id> ids;
    for (const auto& args : spy)
        ids.push_back(args[0].value<Id>());
    return ids;
}

} // namespace

TEST_CASE("nucleus/tile/DemandScheduler")
{
    SECTION("requests wanted tiles in planned order, capped by max_in_flight")
    {
        auto s = base_settings();
        s.max_in_flight = 2;
        s.planner.order = TilePlanner::Order::ZoomDescThenCount;
        auto sch = make_scheduler(s);
        QSignalSpy spy(sch.get(), &DemandScheduler::tile_requested);

        sch->submit_wanted(1, wanted({ { Id { 3, { 0, 0 } }, 100 }, { Id { 5, { 4, 4 } }, 100 }, { Id { 4, { 9, 9 } }, 100 } }));
        sch->update();

        REQUIRE(spy.size() == 2);
        CHECK(spy[0][0].value<Id>() == Id { 5, { 4, 4 } });
        CHECK(spy[1][0].value<Id>() == Id { 4, { 9, 9 } });
        CHECK(sch->request_queue().in_flight() == 2);
        CHECK(sch->request_queue().pending() == 1);
    }

    SECTION("a disabled scheduler requests nothing")
    {
        DemandScheduler sch(base_settings());
        QSignalSpy spy(&sch, &DemandScheduler::tile_requested);
        sch.submit_wanted(1, wanted({ { x1, 1000 } }));
        sch.update();
        CHECK(spy.size() == 0);
    }

    SECTION("without network nothing is requested, but tiles already in RAM still ship")
    {
        auto sch = make_scheduler();
        sch->set_network_reachability(QNetworkInformation::Reachability::Disconnected);
        QSignalSpy requested(sch.get(), &DemandScheduler::tile_requested);
        QSignalSpy gpu(sch.get(), &DemandScheduler::gpu_tiles_updated);

        sch->receive_tile(delivery(x1));
        sch->submit_wanted(1, wanted({ { x1, 1000 }, { x2, 1000 } }));
        sch->update();

        CHECK(requested.size() == 0);
        REQUIRE(gpu.size() == 1);
        CHECK(sch->is_gpu_resident(x1));
    }

    SECTION("a delivered tile is shipped as a single-level texture and not requested again")
    {
        auto sch = make_scheduler();
        QSignalSpy requested(sch.get(), &DemandScheduler::tile_requested);
        QSignalSpy gpu(sch.get(), &DemandScheduler::gpu_tiles_updated);

        sch->submit_wanted(1, wanted({ { x1, 1000 } }));
        sch->update();
        REQUIRE(requested.size() == 1);
        CHECK(requested[0][0].value<Id>() == x1);
        CHECK(gpu.size() == 0);

        sch->receive_tile(delivery(x1));
        sch->update();
        REQUIRE(gpu.size() == 1);
        CHECK(gpu[0][0].value<std::vector<Id>>().empty());
        const auto tiles = gpu[0][1].value<std::vector<GpuTextureTile>>();
        REQUIRE(tiles.size() == 1);
        CHECK(tiles[0].id == x1);
        REQUIRE(tiles[0].texture);
        CHECK(tiles[0].texture->size() == 1);
        CHECK(tiles[0].texture->at(0).width() == 256);
        CHECK(tiles[0].texture->at(0).height() == 256);
        CHECK(sch->is_gpu_resident(x1));

        sch->update();
        CHECK(requested.size() == 1);
        CHECK(gpu.size() == 1);
    }

    SECTION("in-flight tiles that stay wanted are not requested again")
    {
        auto sch = make_scheduler();
        QSignalSpy requested(sch.get(), &DemandScheduler::tile_requested);
        QSignalSpy aborted(sch.get(), &DemandScheduler::tile_aborted);
        sch->submit_wanted(1, wanted({ { x1, 1000 } }));
        sch->update();
        sch->update();
        CHECK(requested.size() == 1);
        CHECK(aborted.size() == 0);
    }

    SECTION("a tile that falls out of the wanted list is aborted and its slot reused")
    {
        auto s = base_settings();
        s.max_in_flight = 1;
        s.planner.order = TilePlanner::Order::ZoomDescThenCount;
        auto sch = make_scheduler(s);
        QSignalSpy requested(sch.get(), &DemandScheduler::tile_requested);
        QSignalSpy aborted(sch.get(), &DemandScheduler::tile_aborted);

        const Id a { 5, { 1, 1 } };
        const Id b { 4, { 9, 9 } };
        sch->submit_wanted(1, wanted({ { a, 1000 }, { b, 500 } }));
        sch->update();
        REQUIRE(requested.size() == 1);
        CHECK(requested[0][0].value<Id>() == a);
        CHECK(sch->request_queue().pending() == 1);

        sch->submit_wanted(1, wanted({ { b, 500 } }));
        sch->update();
        REQUIRE(aborted.size() == 1);
        CHECK(aborted[0][0].value<Id>() == a);
        REQUIRE(requested.size() == 2);
        CHECK(requested[1][0].value<Id>() == b);
        CHECK(sch->request_queue().in_flight() == 1);
    }

    SECTION("not found tiles become tombstones: the parent is requested instead and the tombstone never again")
    {
        auto sch = make_scheduler();
        QSignalSpy requested(sch.get(), &DemandScheduler::tile_requested);
        sch->submit_wanted(1, wanted({ { x1, 1000 } }));
        sch->update();
        REQUIRE(requested.size() == 1);
        CHECK(requested[0][0].value<Id>() == x1);

        sch->receive_tile(delivery(x1, NetworkInfo::Status::NotFound));
        CHECK(sch->ram_cache().contains(x1));
        sch->update();
        REQUIRE(requested.size() == 2);
        CHECK(requested[1][0].value<Id>() == x1.parent());

        sch->receive_tile(delivery(x1.parent(), NetworkInfo::Status::NotFound));
        sch->update();
        REQUIRE(requested.size() == 3);
        CHECK(requested[2][0].value<Id>() == x1.parent().parent());

        sch->update();
        CHECK(requested.size() == 3);
        const auto ids = requested_ids(requested);
        CHECK(std::count(ids.begin(), ids.end(), x1) == 1);
        CHECK(std::count(ids.begin(), ids.end(), x1.parent()) == 1);
    }

    SECTION("network errors are not cached, back off, and are retried later")
    {
        auto s = base_settings();
        s.retry_base_ms = 1;
        s.retry_max_ms = 1000;
        auto sch = make_scheduler(s);
        QSignalSpy requested(sch.get(), &DemandScheduler::tile_requested);
        sch->submit_wanted(1, wanted({ { x1, 1000 } }));
        sch->update();
        REQUIRE(requested.size() == 1);

        sch->receive_tile(delivery(x1, NetworkInfo::Status::NetworkError));
        CHECK(!sch->ram_cache().contains(x1));
        CHECK(sch->request_queue().in_flight() == 0);

        sch->update(); // still backing off (1ms * 2^1 = 2ms)
        CHECK(requested.size() == 1);

        QThread::msleep(20);
        sch->update();
        REQUIRE(requested.size() == 2);
        CHECK(requested[1][0].value<Id>() == x1);

        sch->receive_tile(delivery(x1));
        CHECK(sch->ram_cache().contains(x1));
        sch->update();
        CHECK(sch->is_gpu_resident(x1));
    }

    SECTION("images with the wrong size become tombstones and are not shipped")
    {
        auto sch = make_scheduler();
        QSignalSpy requested(sch.get(), &DemandScheduler::tile_requested);
        QSignalSpy gpu(sch.get(), &DemandScheduler::gpu_tiles_updated);
        sch->submit_wanted(1, wanted({ { x1, 1000 } }));
        sch->update();

        sch->receive_tile(delivery(x1, NetworkInfo::Status::Good, read_test_file("170px-Jeune_bouquetin_de_face.jpg")));
        sch->update();
        CHECK(gpu.size() == 0);
        CHECK(!sch->is_gpu_resident(x1));
        REQUIRE(sch->ram_cache().contains(x1));
        CHECK(sch->ram_cache().peak_at(x1).network_info.status == NetworkInfo::Status::NotFound);

        sch->update(); // the parent is requested instead
        REQUIRE(requested.size() == 2);
        CHECK(requested[1][0].value<Id>() == x1.parent());
    }

    SECTION("undecodable payloads become tombstones and are not shipped")
    {
        auto sch = make_scheduler();
        QSignalSpy gpu(sch.get(), &DemandScheduler::gpu_tiles_updated);
        sch->submit_wanted(1, wanted({ { x1, 1000 } }));
        sch->receive_tile(delivery(x1, NetworkInfo::Status::Good, QByteArray("this is not an image")));
        sch->update();
        CHECK(gpu.size() == 0);
        REQUIRE(sch->ram_cache().contains(x1));
        CHECK(sch->ram_cache().peak_at(x1).network_info.status == NetworkInfo::Status::NotFound);
    }

    SECTION("gpu capacity: never exceeded, evictions travel with the new tiles, least recently used goes first")
    {
        auto s = base_settings();
        s.gpu_tile_limit = 2;
        auto sch = make_scheduler(s);
        QSignalSpy gpu(sch.get(), &DemandScheduler::gpu_tiles_updated);

        // x1 > x2 > x3 in priority (CountTimesGap, same gap)
        for (const auto& id : { x1, x2, x3 })
            sch->receive_tile(delivery(id));
        sch->submit_wanted(1, wanted({ { x1, 300 }, { x2, 200 }, { x3, 100 } }));
        sch->update();
        REQUIRE(gpu.size() == 1);
        CHECK(gpu[0][1].value<std::vector<GpuTextureTile>>().size() == 2); // lowest priority (x3) dropped, nothing evicted yet
        CHECK(sch->n_gpu_resident() == 2);
        CHECK(sch->is_gpu_resident(x1));
        CHECK(sch->is_gpu_resident(x2));
        CHECK(!sch->is_gpu_resident(x3));

        // x3 becomes the only demand. x1 is the least recently used.
        sch->submit_wanted(1, wanted({ { x3, 1000 } }));
        sch->update();
        REQUIRE(gpu.size() == 2);
        const auto deleted = gpu[1][0].value<std::vector<Id>>();
        const auto added = gpu[1][1].value<std::vector<GpuTextureTile>>();
        REQUIRE(deleted.size() == 1);
        CHECK(deleted[0] == x1);
        REQUIRE(added.size() == 1);
        CHECK(added[0].id == x3);
        CHECK(sch->n_gpu_resident() == 2);
    }

    SECTION("gpu capacity: tiles the shader still uses as fallback are never evicted")
    {
        auto s = base_settings();
        s.gpu_tile_limit = 2;
        auto sch = make_scheduler(s);
        QSignalSpy gpu(sch.get(), &DemandScheduler::gpu_tiles_updated);

        sch->receive_tile(delivery(x1));
        sch->receive_tile(delivery(x2));
        sch->submit_wanted(1, wanted({ { x1, 300 }, { x2, 200 } }));
        sch->update();
        REQUIRE(gpu.size() == 1);
        REQUIRE(sch->n_gpu_resident() == 2); // x1 is the older one

        // x3 wants to come in, and a child of x1 is wanted, i.e. x1 is the fallback currently on screen
        sch->receive_tile(delivery(x3));
        sch->submit_wanted(1, wanted({ { x3, 1000 }, { x1.children()[0], 10 } }));
        sch->update();
        REQUIRE(gpu.size() == 2);
        const auto deleted = gpu[1][0].value<std::vector<Id>>();
        REQUIRE(deleted.size() == 1);
        CHECK(deleted[0] == x2);
        CHECK(sch->is_gpu_resident(x1));
        CHECK(sch->is_gpu_resident(x3));
    }

    SECTION("at most max_ship_per_update tiles are shipped per update, the rest on the next one")
    {
        auto s = base_settings();
        s.max_ship_per_update = 2;
        auto sch = make_scheduler(s);
        QSignalSpy gpu(sch.get(), &DemandScheduler::gpu_tiles_updated);
        for (const auto& id : { x1, x2, x3 })
            sch->receive_tile(delivery(id));
        sch->submit_wanted(1, wanted({ { x1, 300 }, { x2, 200 }, { x3, 100 } }));

        sch->update();
        REQUIRE(gpu.size() == 1);
        CHECK(gpu[0][1].value<std::vector<GpuTextureTile>>().size() == 2);
        sch->update();
        REQUIRE(gpu.size() == 2);
        const auto second = gpu[1][1].value<std::vector<GpuTextureTile>>();
        REQUIRE(second.size() == 1);
        CHECK(second[0].id == x3);
    }

    SECTION("the feeds of several owners are summed, and clearing feeds removes their demand")
    {
        auto sch = make_scheduler();
        QSignalSpy requested(sch.get(), &DemandScheduler::tile_requested);
        QSignalSpy aborted(sch.get(), &DemandScheduler::tile_aborted);

        // alone, b (150) would outrank a (100); summed, a (200) wins
        sch->submit_wanted(1, wanted({ { x1, 100 } }));
        sch->submit_wanted(2, wanted({ { x1, 100 }, { x2, 150 } }));
        sch->update();
        REQUIRE(requested.size() == 2);
        CHECK(requested[0][0].value<Id>() == x1);
        CHECK(requested[1][0].value<Id>() == x2);

        sch->submit_wanted(2, {}); // only owner 1 (x1) is left
        sch->update();
        REQUIRE(aborted.size() == 1);
        CHECK(aborted[0][0].value<Id>() == x2);

        sch->submit_wanted(1, {}); // nothing wanted at all
        sch->update();
        REQUIRE(aborted.size() == 2);
        CHECK(aborted[1][0].value<Id>() == x1);
        CHECK(sch->request_queue().in_flight() == 0);
    }

    SECTION("persisting and reading back keeps tiles and tombstones, old entries count as missing")
    {
        auto s = base_settings();
        const auto make_named = [](const DemandScheduler::Settings& settings) {
            auto sch = make_scheduler(settings);
            sch->set_name("test_fetch_persist");
            return sch;
        };
        {
            auto sch = make_named(s);
            std::filesystem::remove_all(sch->disk_cache_path());
            sch->receive_tile(delivery(x1));
            sch->receive_tile(delivery(x2, NetworkInfo::Status::NotFound));
            REQUIRE(sch->persist_tiles().has_value());
        }
        {
            auto sch = make_named(s);
            REQUIRE(sch->read_disk_cache().has_value());
            CHECK(sch->ram_cache().n_cached_objects() == 2);
            CHECK(sch->ram_cache().peak_at(x2).network_info.status == NetworkInfo::Status::NotFound);

            QSignalSpy requested(sch.get(), &DemandScheduler::tile_requested);
            QSignalSpy gpu(sch.get(), &DemandScheduler::gpu_tiles_updated);
            sch->submit_wanted(1, wanted({ { x1, 1000 }, { x2, 1000 } }));
            sch->update();
            CHECK(gpu.size() == 1); // x1 comes straight from the disk cache
            REQUIRE(requested.size() == 1); // x2 is a tombstone -> its parent instead
            CHECK(requested[0][0].value<Id>() == x2.parent());
        }
        {
            s.retirement_age_ms = 1;
            auto sch = make_named(s);
            REQUIRE(sch->read_disk_cache().has_value());
            QThread::msleep(10);
            QSignalSpy requested(sch.get(), &DemandScheduler::tile_requested);
            QSignalSpy gpu(sch.get(), &DemandScheduler::gpu_tiles_updated);
            sch->submit_wanted(1, wanted({ { x1, 1000 }, { x2, 1000 } }));
            sch->update();
            CHECK(gpu.size() == 0);
            CHECK(requested.size() == 2); // both are requested again
            std::filesystem::remove_all(sch->disk_cache_path());
        }
    }

    SECTION("persisting and reading is refused for unnamed schedulers")
    {
        auto sch = make_scheduler();
        CHECK(!sch->persist_tiles().has_value());
        CHECK(!sch->read_disk_cache().has_value());
    }

    SECTION("clear_full_cache drops ram tiles, tombstones, gpu residency and the disk cache")
    {
        auto sch = make_scheduler();
        sch->set_name("test_fetch_clear");
        std::filesystem::remove_all(sch->disk_cache_path());
        QSignalSpy gpu(sch.get(), &DemandScheduler::gpu_tiles_updated);

        sch->receive_tile(delivery(x1));
        sch->receive_tile(delivery(x2, NetworkInfo::Status::NotFound));
        sch->submit_wanted(1, wanted({ { x1, 1000 } }));
        sch->update();
        REQUIRE(sch->is_gpu_resident(x1));
        REQUIRE(sch->persist_tiles().has_value());
        REQUIRE(gpu.size() == 1);

        sch->clear_full_cache();
        CHECK(sch->ram_cache().n_cached_objects() == 0);
        CHECK(sch->n_gpu_resident() == 0);
        REQUIRE(gpu.size() == 2);
        const auto deleted = gpu[1][0].value<std::vector<Id>>();
        REQUIRE(deleted.size() == 1);
        CHECK(deleted[0] == x1);
        CHECK(gpu[1][1].value<std::vector<GpuTextureTile>>().empty());

        auto reloaded = make_scheduler();
        reloaded->set_name("test_fetch_clear");
        CHECK(reloaded->read_disk_cache().has_value());
        CHECK(reloaded->ram_cache().n_cached_objects() == 0);
        std::filesystem::remove_all(sch->disk_cache_path());
    }

    SECTION("setup wires up a scheduler and a tile service")
    {
        auto holder = setup::demand_scheduler(std::make_unique<TileLoadService>("http://localhost:1/", TileLoadService::UrlPattern::ZXY, ".jpeg"));
        CHECK(holder.scheduler);
        CHECK(holder.tile_service);
    }
}
