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

#include "GpuStopwatch.h"

#include <QDebug>

namespace webgpu::timing {

#ifdef QT_DEBUG
static const char* ring_slot_label(uint32_t i)
{
    switch (i) {
    case 0: return "GpuStopwatch Readback 0";
    case 1: return "GpuStopwatch Readback 1";
    case 2: return "GpuStopwatch Readback 2";
    case 3: return "GpuStopwatch Readback 3";
    default: return "GpuStopwatch Readback X";
    }
}
#else
static inline const char* ring_slot_label(uint32_t) { return "GpuStopwatch Readback"; }
#endif

GpuStopwatch::GpuStopwatch(WGPUDevice device, Callback callback, uint32_t ring_buffer_size)
    : m_device(device)
    , m_callback(std::move(callback))
{
    WGPUQuerySetDescriptor desc = {
        .nextInChain = nullptr,
        .label = WGPUStringView { .data = "GpuStopwatch Query", .length = WGPU_STRLEN },
        .type = WGPUQueryType_Timestamp,
        .count = 2,
    };
    m_timestamp_queries = wgpuDeviceCreateQuerySet(m_device, &desc);
    m_timestamp_resolve = std::make_unique<webgpu::raii::RawBuffer<uint64_t>>(
        device, WGPUBufferUsage_QueryResolve | WGPUBufferUsage_CopySrc, 2, "GpuStopwatch Resolve");

    for (uint32_t i = 0; i < ring_buffer_size; ++i) {
        m_ring.push_back({ std::make_unique<webgpu::raii::RawBuffer<uint64_t>>(
                               device, WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead, 2, ring_slot_label(i)),
            0 });
    }
}

GpuStopwatch::~GpuStopwatch()
{
    if (m_timestamp_queries)
        wgpuQuerySetRelease(m_timestamp_queries);
}

void GpuStopwatch::start(WGPUCommandEncoder encoder)
{
    wgpuCommandEncoderWriteTimestamp(encoder, m_timestamp_queries, 0);
}

void GpuStopwatch::stop(WGPUCommandEncoder encoder, uint64_t frame)
{
    const uint32_t resolve_size = static_cast<uint32_t>(m_timestamp_resolve->size_in_byte());
    wgpuCommandEncoderWriteTimestamp(encoder, m_timestamp_queries, 1);
    wgpuCommandEncoderResolveQuerySet(encoder, m_timestamp_queries, 0, 2, m_timestamp_resolve->handle(), 0);

    const int i = m_write_index;
    if (m_ring[i].buffer->map_state() == WGPUBufferMapState_Unmapped) {
        m_timestamp_resolve->copy_to_buffer(encoder, 0, *m_ring[i].buffer, 0, resolve_size);
        m_ring[i].frame = frame;
        m_read_index = i;
        increment(m_write_index);
    }
#ifdef QT_DEBUG
    else {
        m_dbg_dropped_count++;
        if (m_dbg_dropped_count == 100)
            qWarning() << "GpuStopwatch: dropped 100 measurements. Consider increasing ring_buffer_size.";
    }
#endif
}

void GpuStopwatch::resolve()
{
    if (m_read_index < 0)
        return;

    const int i = m_read_index;
    const uint64_t frame = m_ring[i].frame;
    m_ring[i].buffer->read_back_async(m_device, [this, frame](WGPUMapAsyncStatus status, std::vector<uint64_t> data) {
        if (status == WGPUMapAsyncStatus_Success)
            m_callback(frame, static_cast<float>(data[1] - data[0]) / 1e9f);
    });
    m_read_index = -1;
}

} // namespace webgpu::timing
