/*****************************************************************************
 * weBIGeo
 * Copyright (C) 2026 Gerald Kimmersdorfer
 * Copyright (C) 2025 Patrick Komon
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

#include <filesystem>
#include <radix/geometry.h>
#include <string>
#include <tl/expected.hpp>
#include <vector>

namespace nucleus::utils::geopng {

// Returns candidate AABB .txt file paths for the given geo-PNG image path.
// Tries in order:
//   - <same dir>/<stem>_aabb.txt
//   - <same dir>/<stem before first _>_aabb.txt (only when differs from candidate 1)
//   - <same dir>/aabb.txt
std::vector<std::filesystem::path> possible_aabb_paths(const std::filesystem::path& image_path);

// Parses a sidecar AABB .txt file describing the world-space extent of a geo-PNG.
// The file contains exactly four lines, one floating point number each (. as separator):
//   min_x, min_y, max_x, max_y
// Returns the parsed AABB, or an error message on failure
tl::expected<radix::geometry::Aabb<2, double>, std::string> load_aabb_from_file(const std::filesystem::path& file_path);

} // namespace nucleus::utils::geopng
