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

#include "ThreadLoad.h"

#include <QAbstractEventDispatcher>
#include <QObject>
#include <QThread>
#include <algorithm>
#include <memory>
#include <mutex>

namespace nucleus::timing {

namespace {

    // Process-lifetime storage: trackers are never removed, only ever appended, so the raw
    // pointers handed out to QThread::started()/awake()/aboutToBlock() lambdas stay valid for
    // as long as the process runs (vector reallocation moves the unique_ptrs, not the pointees).
    std::vector<std::unique_ptr<ThreadLoadTracker>>& registered_trackers()
    {
        static std::vector<std::unique_ptr<ThreadLoadTracker>> trackers;
        return trackers;
    }

    std::mutex& registry_mutex()
    {
        static std::mutex mutex;
        return mutex;
    }

    // Assumed frame budget used to turn accumulated busy time into a percentage, rather than
    // measuring the actual frame duration.
    constexpr double ASSUMED_FRAME_BUDGET_NS = 1'000'000'000.0 / 60.0;

} // namespace

void ThreadLoadRegistry::register_thread(QThread* thread, std::string label)
{
    ThreadLoadTracker* tracker = nullptr;
    {
        std::lock_guard<std::mutex> lock(registry_mutex());
        registered_trackers().push_back(std::make_unique<ThreadLoadTracker>(std::move(label)));
        tracker = registered_trackers().back().get();
    }

    // Qt::DirectConnection is required here: QThread::started() would otherwise be queued back
    // to the thread that owns the QThread object (its creator), not run on the thread it
    // manages, and the awake()/aboutToBlock() bindings below would silently attach to the wrong
    // thread's dispatcher (or none at all).
    QObject::connect(
        thread,
        &QThread::started,
        thread,
        [tracker] {
            // Seed in case aboutToBlock() fires before this thread's first awake().
            tracker->busy = true;
            tracker->awake_since = std::chrono::steady_clock::now();

            auto* dispatcher = QAbstractEventDispatcher::instance();
            QObject::connect(
                dispatcher, &QAbstractEventDispatcher::awake, dispatcher,
                [tracker] {
                    if (tracker->busy)
                        return; // already in a busy span -- a re-fire here must not reset it
                    tracker->busy = true;
                    tracker->awake_since = std::chrono::steady_clock::now();
                },
                Qt::DirectConnection);
            QObject::connect(
                dispatcher, &QAbstractEventDispatcher::aboutToBlock, dispatcher,
                [tracker] {
                    if (!tracker->busy)
                        return;
                    tracker->add_busy_ns((std::chrono::steady_clock::now() - tracker->awake_since).count());
                    tracker->busy = false;
                },
                Qt::DirectConnection);
        },
        Qt::DirectConnection);
}

std::vector<ThreadLoadSample> ThreadLoadRegistry::sample_all()
{
    std::vector<ThreadLoadTracker*> trackers;
    {
        std::lock_guard<std::mutex> lock(registry_mutex());
        trackers.reserve(registered_trackers().size());
        for (const auto& tracker : registered_trackers())
            trackers.push_back(tracker.get());
    }

    std::vector<ThreadLoadSample> samples;
    samples.reserve(trackers.size());
    for (auto* tracker : trackers) {
        const double busy_ns = double(tracker->exchange_busy_ns());
        const float fraction = std::clamp(float(busy_ns / ASSUMED_FRAME_BUDGET_NS), 0.0f, 1.0f);
        samples.push_back({ tracker->label(), fraction });
    }
    return samples;
}

} // namespace nucleus::timing
