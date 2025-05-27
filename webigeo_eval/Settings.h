/*****************************************************************************
 * weBIGeo
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

namespace webigeo_eval {

struct Settings {

    uint32_t trajectory_resolution_multiplier = 16;
    uint32_t num_steps = 256u;
    uint32_t num_paths_per_release_cell = 1024u;

    float random_contribution = 0.16f;
    float persistence_contribution = 0.9f;
    float runout_flowpy_alpha = 25.0f; // in degrees

    int model_type = 1;

    int friction_model_type = 3;

    float friction_coeff = .155;
    float drag_coeff = 4000.0f;
    float slab_thickness = 0.5f;
    float density = 200.0f;

    std::string aabb_file_path;
    std::string release_points_texture_path;
    std::string heightmap_texture_path;
    std::string output_dir_path;

    static void write_to_json_file(const Settings& settings, const std::filesystem::path& output_path);
    static Settings read_from_json_file(const std::filesystem::path& input_path);
};

} // namespace webigeo_eval
