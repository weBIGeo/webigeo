/*****************************************************************************
 * weBIGeo
 * Copyright (C) 2026 Wendelin Muth
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

#include <glm/glm.hpp>

namespace webgpu_engine::clouds {
    static constexpr uint32_t ZOOM_MAX = 10;
    static constexpr glm::vec2 BOUNDS_MIN = {46.2, 9.4};
    static constexpr glm::vec2 BOUNDS_MAX = {49.2, 17.4};
    static constexpr glm::uvec2 TILE_COUNTS = {24, 14};
    static constexpr uint32_t TILE_COUNT_TOTAL = TILE_COUNTS.x * TILE_COUNTS.y;
    static constexpr uint32_t TILE_RESOLUTION_XY = 256;
    static constexpr uint32_t TILE_RESOLUTION_Z = 64;
    static constexpr float MAX_ALTITUDE = 14000.0;
    static constexpr uint32_t ATLAS_BITS_XY = 2;
    static constexpr uint32_t ATLAS_SCALE_XY = 1 << ATLAS_BITS_XY;
    static constexpr uint32_t ATLAS_MASK_XY = ATLAS_SCALE_XY - 1;
    static constexpr uint32_t ATLAS_BITS_Z = 5;
    static constexpr uint32_t ATLAS_SCALE_Z = 1 << ATLAS_BITS_Z;
    static constexpr uint32_t ATLAS_MASK_Z = ATLAS_SCALE_Z - 1;
    static constexpr uint32_t LOADED_TILE_LIMIT = ATLAS_SCALE_XY * ATLAS_SCALE_XY * ATLAS_SCALE_Z;
}