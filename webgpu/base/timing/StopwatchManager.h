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

#include "CpuStopwatch.h"
#include "GpuStopwatch.h"
#include "StringId.h"
#include <QObject>
#include <memory>
#include <unordered_map>
#include <webgpu/webgpu.h>

namespace webgpu::timing {

// Lazily creates GpuStopwatch / CpuStopwatch on first use per StringId.
// Emits measured(id, frame, seconds) for every completed measurement.
class StopwatchManager : public QObject {
    Q_OBJECT

public:
    explicit StopwatchManager(QObject* parent = nullptr);

    // Must be called once after WGPUDevice is available.
    void init(WGPUDevice device);

    // Call once per frame before any start calls.
    void begin_frame(uint64_t frame);

    void start_gpu(StringId id, WGPUCommandEncoder encoder);
    void stop_gpu(StringId id, WGPUCommandEncoder encoder);
    // Initiates async readback for all active GPU stopwatches. 
    // Must be called after wgpuQueueSubmit.
    void resolve_all();

    void start_cpu(StringId id);
    void stop_cpu(StringId id);

signals:
    void measured(webgpu::timing::StringId id, uint64_t frame, float seconds);

private:
    WGPUDevice m_device = nullptr;
    uint64_t m_current_frame = 0;

    std::unordered_map<uint32_t, std::unique_ptr<GpuStopwatch>> m_gpu;
    std::unordered_map<uint32_t, std::unique_ptr<CpuStopwatch>> m_cpu;
    std::unordered_map<uint32_t, StringId> m_ids; // metadata cache for signal emission

    GpuStopwatch* get_or_create_gpu(StringId id);
    CpuStopwatch* get_or_create_cpu(StringId id);
};

} // namespace webgpu::timing

Q_DECLARE_METATYPE(webgpu::timing::StringId)
