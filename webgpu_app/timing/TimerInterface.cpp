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

#include "TimerInterface.h"
#include <QDebug>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace webgpu_app::timing {

std::string format_time(float time, int precision)
{
    std::ostringstream oss;
    if (time > 0.5) {
        oss << std::setprecision(precision) << time << " s";
    } else if (time > 0.0005) {
        oss << std::fixed << std::setprecision(precision) << time * 1000 << " ms";
    } else if (time > 0.0000005) {
        oss << std::fixed << std::setprecision(precision) << time * 1000000 << " us";
    } else {
        oss << std::fixed << std::setprecision(precision) << time * 1000000000 << " ns";
    }
    return oss.str();
}

TimerInterface::TimerInterface(size_t capacity)
    : m_capacity(capacity)
    , m_id(s_next_id++)
{
    m_results.reserve(capacity);
#ifdef ALP_ENABLE_TRACK_OBJECT_LIFECYCLE
    qDebug() << "nucleus::timing::TimerInterface(name=" << get_name().c_str() << ")";
#endif
}

TimerInterface::~TimerInterface()
{
#ifdef ALP_ENABLE_TRACK_OBJECT_LIFECYCLE
    qDebug() << "nucleus::timing::~TimerInterface(name=" << get_name().c_str() << ")";
#endif
}

uint32_t TimerInterface::get_id() { return m_id; }
float TimerInterface::get_last_measurement() { return this->m_results.back(); }
size_t TimerInterface::get_capacity() { return m_capacity; }

float TimerInterface::get_average() { return m_sum / m_results.size(); }

float TimerInterface::get_standard_deviation()
{
    size_t n = m_results.size();
    if (n == 0)
        return 0.0f;
    float mean = m_sum / n;
    return std::sqrt((m_sum_of_squares / n) - (mean * mean));
}

size_t TimerInterface::get_sample_count() { return m_results.size(); }

void TimerInterface::clear_results()
{
    m_results.clear();
    m_sum = 0.0f;
    m_sum_of_squares = 0.0f;
}

std::string TimerInterface::to_string()
{
    std::ostringstream oss;
    oss << "T" << get_id() << ": " << format_time(get_average()) << " ±" << format_time(get_standard_deviation()) << " [" << get_sample_count() << "]";
    return oss.str();
}

void TimerInterface::add_result(float result)
{
    if (m_results.size() == m_results.capacity()) {
        float oldest = m_results.front();
        m_sum -= oldest;
        m_sum_of_squares -= oldest * oldest;
        m_results.erase(m_results.begin());
    }
    m_results.push_back(result);
    m_sum += result;
    m_sum_of_squares += result * result;
    emit tick(result);
}

uint32_t TimerInterface::s_next_id = 0;

} // namespace webgpu_app::timing
