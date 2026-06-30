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

#include "Node.h"
#include <optional>
#include <webgpu/base/Context.h>

namespace webgpu_compute::nodes {

class GeoTiffNode : public Node {
    Q_OBJECT

public:
    NODE_TYPE_NAME(GeoTiffNode)

    struct GeoTiffNodeSettings {
        std::string file_path;
        std::string target_crs = "EPSG:3857";
    };

    GeoTiffNode(webgpu::Context& ctx);
    GeoTiffNode(webgpu::Context& ctx, const GeoTiffNodeSettings& settings);

    struct LoadInfo {
        // Source file (before reprojection / resampling)
        int src_width = 0;
        int src_height = 0;
        int src_bands = 0;
        std::string src_crs;       // e.g. "WGS 84 (EPSG:4326)"
        std::string src_data_type; // e.g. "Float32"
        // Output GPU texture
        uint32_t out_width = 0;
        uint32_t out_height = 0;
        std::string out_format;    // e.g. "RGBA32Float"
        bool loaded = false;
    };

    void set_settings(const GeoTiffNodeSettings& settings);
    const GeoTiffNodeSettings& get_settings() const { return m_settings; }
    const LoadInfo& load_info() const { return m_load_info; }

    void serialize_settings(QJsonObject& out) const override;
    void deserialize_settings(const QJsonObject& in) override;

public slots:
    void run_impl() override;

private:
    static constexpr uint32_t MAX_TEXTURE_RESOLUTION = 8192;

    webgpu::Context* m_ctx;
    GeoTiffNodeSettings m_settings;
    std::unique_ptr<webgpu::raii::TextureWithSampler> m_output_texture;
    LoadInfo m_load_info;

    std::optional<radix::geometry::Aabb<3, double>> m_cached_region;
};

} // namespace webgpu_compute::nodes
