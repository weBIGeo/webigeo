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

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

class QThread;

namespace nucleus::timing {

// Tracks how much wall-clock time a single QThread's event loop spends processing events
// (as opposed to waiting idle for the next one). One instance per registered thread, kept
// alive for the process lifetime by ThreadLoadRegistry.
class ThreadLoadTracker {
public:
    explicit ThreadLoadTracker(std::string label)
        : m_label(std::move(label))
    {
    }

    const char* label() const { return m_label.c_str(); }

    void add_busy_ns(int64_t ns) { m_busy_ns.fetch_add(ns, std::memory_order_relaxed); }
    int64_t exchange_busy_ns() { return m_busy_ns.exchange(0, std::memory_order_relaxed); }

    // Only ever written/read on the tracked thread itself (by the awake()/aboutToBlock()
    // handlers), never concurrently, so these do not need to be atomic.
    //
    // awake() is NOT guaranteed to alternate cleanly with aboutToBlock() -- some event
    // dispatchers emit it again while already "awake" (no intervening block). Treating every
    // awake() as a fresh start would then discard whatever was measured before that re-fire,
    // undercounting real work. `busy` + `awake_since` form a small state machine instead:
    // awake() only opens a span if one isn't already open, aboutToBlock() only closes+accumulates
    // if one is.
    bool busy = false;
    std::chrono::steady_clock::time_point awake_since;

private:
    std::string m_label;
    std::atomic<int64_t> m_busy_ns { 0 };
};

struct ThreadLoadSample {
    const char* label; // stable for the process lifetime (owned by the tracker)
    float busy_fraction; // clamped to [0, 1]
};

// Process-wide registry of tracked worker threads, sampled once per render frame.
class ThreadLoadRegistry {
public:
    // Registers `thread` for busy/idle tracking under `label`. Must be called before
    // thread->start(). Hooks QThread::started() to bind QAbstractEventDispatcher::awake()/
    // aboutToBlock() on the thread itself, so no changes are needed at the call sites that
    // actually run on that thread.
    static void register_thread(QThread* thread, std::string label);

    // Call once per render frame from the thread that owns the profiling UI. Exchanges and
    // resets every registered tracker's accumulator and expresses it as a fraction of an
    // assumed 60 fps (1/60 s) frame budget, rather than the actual measured frame duration.
    static std::vector<ThreadLoadSample> sample_all();
};

} // namespace nucleus::timing
