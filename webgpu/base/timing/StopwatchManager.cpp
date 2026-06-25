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

#include "StopwatchManager.h"

#include "../webgpu_interface.hpp"

namespace webgpu::timing {

StopwatchManager::StopwatchManager(QObject* parent)
    : QObject(parent)
{
}

void StopwatchManager::init(WGPUDevice device) { m_device = device; }

void StopwatchManager::begin_frame(uint64_t frame) { m_current_frame = frame; }

void StopwatchManager::start_gpu(StringId id, WGPUCommandEncoder encoder)
{
    if (!webgpu::isTimingSupported())
        return;
    get_or_create_gpu(id)->start(encoder);
}

void StopwatchManager::stop_gpu(StringId id, WGPUCommandEncoder encoder)
{
    if (!webgpu::isTimingSupported())
        return;
    auto it = m_gpu.find(id.hash);
    if (it != m_gpu.end())
        it->second->stop(encoder, m_current_frame);
}

void StopwatchManager::resolve_all()
{
    for (auto& [hash, sw] : m_gpu)
        sw->resolve();
}

void StopwatchManager::start_cpu(StringId id) { get_or_create_cpu(id)->start(); }

void StopwatchManager::stop_cpu(StringId id)
{
    auto it = m_cpu.find(id.hash);
    if (it != m_cpu.end())
        emit measured(m_ids.at(id.hash), m_current_frame, it->second->stop());
}

GpuStopwatch* StopwatchManager::get_or_create_gpu(StringId id)
{
    auto it = m_gpu.find(id.hash);
    if (it == m_gpu.end()) {
        m_ids.emplace(id.hash, id);
        GpuStopwatch::Callback cb = [this, id](uint64_t frame, float seconds) { emit measured(id, frame, seconds); };
        it = m_gpu.emplace(id.hash, std::make_unique<GpuStopwatch>(m_device, std::move(cb))).first;
    }
    return it->second.get();
}

CpuStopwatch* StopwatchManager::get_or_create_cpu(StringId id)
{
    auto it = m_cpu.find(id.hash);
    if (it == m_cpu.end()) {
        m_ids.emplace(id.hash, id);
        it = m_cpu.emplace(id.hash, std::make_unique<CpuStopwatch>()).first;
    }
    return it->second.get();
}

} // namespace webgpu::timing
