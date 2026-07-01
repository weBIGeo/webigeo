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

#include "TileLoadService.h"
#include <QString>
#include <cstdint>
#include <vector>

namespace nucleus::tile {

struct TileSourcePreset {
    QString display_name;
    QString source_name;
    QString url;
    TileLoadService::UrlPattern pattern;
    QString file_ending;
    uint32_t resolution;
    uint32_t max_possible_zoom;
};

namespace tile_source_presets {
    const std::vector<TileSourcePreset>& all();
    // Returns nullptr (and logs a warning) if no preset with this source_name exists.
    const TileSourcePreset* get(const QString& source_name);
} // namespace tile_source_presets

} // namespace nucleus::tile
