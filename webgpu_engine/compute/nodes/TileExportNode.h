/*****************************************************************************
 * weBIGeo
 * Copyright (C) 2024 Gerald Kimmersdorfer
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

#include "Node.h"

namespace webgpu_engine::compute::nodes {

class TileExportNode : public Node {
    Q_OBJECT

public:
    NODE_TYPE_NAME(TileExportNode)

    struct ExportSettings {
        std::string output_directory = "tile_export";
    };

    TileExportNode(WGPUDevice device, const ExportSettings& settings);

    void set_settings(const ExportSettings& settings);
    const ExportSettings& get_settings() const;

public slots:
    void run_impl() override;

private:
    WGPUDevice m_device;
    ExportSettings m_settings;

    static void write_aabb_file(const QString& file_path, const radix::geometry::Aabb<2, double>& bounds);
    void impl_single_texture();
};

} // namespace webgpu_engine::compute::nodes
