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

#include <iomanip>
#include <sstream>
#include <string>

namespace webgpu_app {

inline std::string format_time(float time, int precision = 2)
{
    std::ostringstream oss;
    if (time > 0.5f) {
        oss << std::setprecision(precision) << time << " s";
    } else if (time > 0.0005f) {
        oss << std::fixed << std::setprecision(precision) << time * 1000.0f << " ms";
    } else if (time > 0.0000005f) {
        oss << std::fixed << std::setprecision(precision) << time * 1000000.0f << " us";
    } else {
        oss << std::fixed << std::setprecision(precision) << time * 1000000000.0f << " ns";
    }
    return oss.str();
}

} // namespace webgpu_app
