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

#include "../raii/RawBuffer.h"
#include <functional>
#include <memory>
#include <vector>
#include <webgpu/webgpu.h>

namespace webgpu::timing {

class GpuStopwatch {
public:
    using Callback = std::function<void(uint64_t frame, float seconds)>;

    GpuStopwatch(WGPUDevice device, Callback callback, uint32_t ring_buffer_size = 3);
    ~GpuStopwatch();

    void start(WGPUCommandEncoder encoder);
    // frame is stored per ring slot and passed to the callback in resolve().
    void stop(WGPUCommandEncoder encoder, uint64_t frame);
    // Must be called after wgpuQueueSubmit. Fires the stored callback when data is ready.
    void resolve();

private:
    struct RingSlot {
        std::unique_ptr<webgpu::raii::RawBuffer<uint64_t>> buffer;
        uint64_t frame = 0;
    };

    WGPUDevice m_device;
    WGPUQuerySet m_timestamp_queries = nullptr;
    std::unique_ptr<webgpu::raii::RawBuffer<uint64_t>> m_timestamp_resolve;
    std::vector<RingSlot> m_ring;
    Callback m_callback;

    int m_write_index = 0;
    int m_read_index = -1;

    void increment(int& index) { index = (index + 1) % static_cast<int>(m_ring.size()); }

#ifdef QT_DEBUG
    uint32_t m_dbg_dropped_count = 0;
#endif
};

} // namespace webgpu::timing
