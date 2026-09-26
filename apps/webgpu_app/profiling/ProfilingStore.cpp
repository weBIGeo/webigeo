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

#include "ProfilingStore.h"

namespace webgpu_app {

void TimingSeries::add(uint64_t frame, float value)
{
    if (count == CAPACITY) {
        const Sample& evicted = samples[head];
        sum -= evicted.value;
        sum_sq -= evicted.value * evicted.value;
        count--;
    }
    samples[head] = { frame, value };
    head = (head + 1) % CAPACITY;
    count++;
    last_frame = frame;
    sum += value;
    sum_sq += value * value;
    if (value < min)
        min = value;
    if (value > max)
        max = value;
}

void ThreadLoadSeries::add(float value)
{
    if (count == CAPACITY) {
        sum -= samples[head];
        count--;
    }
    samples[head] = value;
    head = (head + 1) % CAPACITY;
    count++;
    sum += value;
}

ProfilingStore::ProfilingStore(QObject* parent)
    : QObject(parent)
{
}

void ProfilingStore::on_measurement(webgpu::timing::StringId id, uint64_t frame, float seconds)
{
    auto& series = m_data[id.hash];
    if (!series.name) {
        series.name = id.name;
        series.group = id.group;
    }
    series.add(frame, seconds);
}

void ProfilingStore::on_thread_load(const std::vector<nucleus::timing::ThreadLoadSample>& samples)
{
    for (const auto& sample : samples) {
        auto& series = m_thread_loads[sample.label];
        if (series.label.empty())
            series.label = sample.label;
        series.add(sample.busy_fraction);
    }
}

void ProfilingStore::reset_all()
{
    for (auto& [hash, series] : m_data) {
        series.head = 0;
        series.count = 0;
        series.sum = 0.0f;
        series.sum_sq = 0.0f;
        series.min = FLT_MAX;
        series.max = 0.0f;
        series.last_frame = 0;
    }
    for (auto& [label, series] : m_thread_loads) {
        series.head = 0;
        series.count = 0;
        series.sum = 0.0f;
    }
}

} // namespace webgpu_app
