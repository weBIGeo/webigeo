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

#include <QObject>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <webgpu/base/timing/StringId.h>

namespace webgpu_app {

struct Sample {
    uint64_t frame = 0;
    float value = 0.0f;
};

struct TimingSeries {
    const char* name = nullptr;
    const char* group = nullptr;
    static constexpr size_t CAPACITY = 120;
    std::array<Sample, CAPACITY> samples = {};
    size_t head = 0;
    size_t count = 0;
    float sum = 0.0f;
    float sum_sq = 0.0f;
    float min = FLT_MAX;
    float max = 0.0f;

    void add(uint64_t frame, float value);
};

inline float average(const TimingSeries& s) { return s.count > 0 ? s.sum / static_cast<float>(s.count) : 0.0f; }

inline float stddev(const TimingSeries& s)
{
    if (s.count == 0)
        return 0.0f;
    const float avg = average(s);
    const float variance = s.sum_sq / static_cast<float>(s.count) - avg * avg;
    return variance > 0.0f ? std::sqrt(variance) : 0.0f;
}

class ProfilingStore : public QObject {
    Q_OBJECT
public:
    explicit ProfilingStore(QObject* parent = nullptr);

    [[nodiscard]] const std::unordered_map<uint32_t, TimingSeries>& data() const { return m_data; }
    void reset_all();

public slots:
    void on_measurement(webgpu::timing::StringId id, uint64_t frame, float seconds);

private:
    std::unordered_map<uint32_t, TimingSeries> m_data;
};

} // namespace webgpu_app
