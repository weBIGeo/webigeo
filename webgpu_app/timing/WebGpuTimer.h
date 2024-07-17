/*****************************************************************************
 * Alpine Terrain Renderer
 * Copyright (C) 2023 Gerald Kimmersdorfer
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

#include "timing/TimerInterface.h"
#include <QDebug>
#include <QObject>
#include <memory>
#include <vector>
#include <webgpu/raii/RawBuffer.h>
#include <webgpu/webgpu.h>

namespace webgpu_app::timing {

class WebGpuTimer : public TimerInterface {

public:
    WebGpuTimer(WGPUDevice device, uint32_t ring_buffer_size, size_t capacity)
        : TimerInterface(capacity)
        , m_device(device)
    {
        m_timestamp_query_desc = {
            .nextInChain = nullptr,
            .label = ("T" + std::to_string(get_id()) + " Queries").c_str(), // does this work memory-wise?
            .type = WGPUQueryType_Timestamp,
            .count = 2, // start and end
        };
        m_timestamp_queries = wgpuDeviceCreateQuerySet(m_device, &m_timestamp_query_desc);
        m_timestamp_writes = { .querySet = m_timestamp_queries, .beginningOfPassWriteIndex = 0, .endOfPassWriteIndex = 1 };
        m_timestamp_resolve
            = std::make_unique<webgpu::raii::RawBuffer<uint64_t>>(device, WGPUBufferUsage_QueryResolve | WGPUBufferUsage_CopySrc, 2, "Timestamp GPU Buffer");

        for (int i = 0; i < ring_buffer_size; ++i) {
            m_timestamp_readback_buffer.push_back(
                std::make_unique<webgpu::raii::RawBuffer<uint64_t>>(device, WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead, 2, "Timestamp Readback"));
        }
    }

    void start(WGPUCommandEncoder encoder)
    {
        // Query the GPU for the start timestamp
        wgpuCommandEncoderWriteTimestamp(encoder, m_timestamp_queries, 0);
    }

    void stop(WGPUCommandEncoder encoder)
    {
        const uint32_t size_2_uint64 = static_cast<uint32_t>(m_timestamp_resolve->size_in_byte());
        // Query the GPU for the stop timestamp
        wgpuCommandEncoderWriteTimestamp(encoder, m_timestamp_queries, 1);
        // Resolve the query set into the resolve buffer
        wgpuCommandEncoderResolveQuerySet(encoder, m_timestamp_queries, 0, 2, m_timestamp_resolve->handle(), 0);
        // Copy the resolve buffer to the result buffer
        m_timestamp_resolve->copy_to_buffer(encoder, 0, *m_timestamp_readback_buffer[m_ringbuffer_index_write], 0, size_2_uint64);
        m_ringbuffer_index_write = (m_ringbuffer_index_write + 1) % m_timestamp_readback_buffer.size();
    }

    void resolve(WGPUQueue queue)
    {
        wgpuQueueOnSubmittedWorkDone(
            queue,
            []([[maybe_unused]] WGPUQueueWorkDoneStatus status, void* pUserData) {
                WebGpuTimer* _this = reinterpret_cast<WebGpuTimer*>(pUserData);
                if (status != WGPUQueueWorkDoneStatus_Success)
                    return;

                _this->m_timestamp_readback_buffer[_this->m_ringbuffer_index_read]->read_back_async(_this->m_device, [_this](std::vector<uint64_t> data) {
                    const float result_in_s = (data[1] - data[0]) / 1e9;
                    _this->add_result(result_in_s);
                });
                _this->m_ringbuffer_index_read = (_this->m_ringbuffer_index_read + 1) % _this->m_timestamp_readback_buffer.size();
            },
            this);
    }

private:
    WGPUQuerySetDescriptor m_timestamp_query_desc;
    WGPUQuerySet m_timestamp_queries;
    WGPURenderPassTimestampWrites m_timestamp_writes;
    WGPUDevice m_device;

    std::unique_ptr<webgpu::raii::RawBuffer<uint64_t>> m_timestamp_resolve;
    std::vector<std::unique_ptr<webgpu::raii::RawBuffer<uint64_t>>> m_timestamp_readback_buffer;

    uint32_t m_ringbuffer_index_write = 0; // next write index
    uint32_t m_ringbuffer_index_read = 0; // next read index
};

} // namespace webgpu_app::timing
