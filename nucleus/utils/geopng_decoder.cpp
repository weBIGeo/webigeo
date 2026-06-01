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

#include "geopng_decoder.h"

namespace nucleus::utils::geopng {

std::vector<std::filesystem::path> possible_aabb_paths(const std::filesystem::path& image_path)
{
    const auto dir = image_path.parent_path();
    const std::string stem = image_path.stem().string();

    std::vector<std::filesystem::path> candidates;
    candidates.push_back(dir / (stem + "_aabb.txt"));

    const size_t us = stem.find('_');
    if (us != std::string::npos) {
        auto track_candidate = dir / (stem.substr(0, us) + "_aabb.txt");
        if (track_candidate != candidates.front())
            candidates.push_back(std::move(track_candidate));
    }

    candidates.push_back(dir / "aabb.txt");
    return candidates;
}

} // namespace nucleus::utils::geopng
