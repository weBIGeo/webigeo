/*****************************************************************************
 * weBIGeo
 * Copyright (C) 2024 Gerald Kimmersdorfer
 * Copyright (C) 2024 Patrick Komon
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

#include "TileExportNode.h"

#include "../GpuTileStorage.h"
#include <QDebug>
#include <QString>
#include <assert.h>
#include <filesystem>
#include <nucleus/utils/image_writer.h>
#include <qdir.h>

namespace webgpu_engine::compute::nodes {

webgpu_engine::compute::nodes::TileExportNode::TileExportNode(WGPUDevice device, const ExportSettings& settings)
    : Node(
          {
              // need to pass EITHER single texture
              InputSocket(*this, "texture", data_type<const webgpu::raii::TextureWithSampler*>()),
              InputSocket(*this, "region aabb", data_type<const radix::geometry::Aabb<2, double>*>()), // optional, aabb file only written if connected
          },
          {})
    , m_device(device)
    , m_settings(settings)
{
}

void TileExportNode::set_settings(const ExportSettings& settings) { m_settings = settings; }

const TileExportNode::ExportSettings& TileExportNode::get_settings() const { return m_settings; }

void TileExportNode::run_impl()
{
    qDebug() << "running TileExportNode ...";

    impl_single_texture();
}

void TileExportNode::write_aabb_file(const QString& file_path, const radix::geometry::Aabb<2, double>& bounds)
{
    QFile file(file_path);
    if (file.open(QIODevice::WriteOnly)) {
        QTextStream stream(&file);
        stream.setRealNumberPrecision(30);
        stream << bounds.min.x << "\n";
        stream << bounds.min.y << "\n";
        stream << bounds.max.x << "\n";
        stream << bounds.max.y << "\n";
        file.close();
    }
}

static void copy_buffer_to_raster(
    const QByteArray& src, nucleus::Raster<glm::u8vec4>& dest, uint32_t byte_per_pixel, glm::uvec2 source_size, glm::uvec2 dest_size)
{
    auto& dest_buffer = dest.buffer();
    for (uint32_t y = 0; y < dest_size.y; y++) {
        for (uint32_t x = 0; x < dest_size.x; x++) {
            const uint32_t index_src = (y * source_size.x + x) * byte_per_pixel;
            const uint8_t r = src.at(index_src + 0);
            const uint8_t g = byte_per_pixel > 1 ? src.at(index_src + 1) : 0;
            const uint8_t b = byte_per_pixel > 2 ? src.at(index_src + 2) : 0;
            const uint8_t a = byte_per_pixel > 3 ? src.at(index_src + 3) : 255;

            const uint32_t index_dest = y * dest_size.x + x;
            dest_buffer[index_dest] = glm::u8vec4(r, g, b, a);
        }
    }
}

void TileExportNode::impl_single_texture()
{
    const auto& texture = *std::get<data_type<const webgpu::raii::TextureWithSampler*>()>(input_socket("texture").get_connected_data());

    glm::uvec2 texture_dimensions { texture.texture().width(), texture.texture().height() };

    texture.texture().read_back_async(m_device, 0, [this, texture_dimensions]([[maybe_unused]] size_t layer_index, std::shared_ptr<QByteArray> data) {
        // Copy raw QByteArray to rgba8 texture
        nucleus::Raster<glm::u8vec4> raster(texture_dimensions);
        uint32_t bpp = data->size() / (texture_dimensions.x * texture_dimensions.y);
        copy_buffer_to_raster(*data, raster, bpp, raster.size(), raster.size());

        // Make sure output directory exists
        const auto parent_directory = std::filesystem::path(m_settings.output_directory);
        std::filesystem::create_directories(parent_directory);
        qDebug() << "Writing file to " << std::filesystem::canonical(parent_directory).string();

        // Write to file
        QString texture_file_path = QString::fromStdString((parent_directory / "texture.png").string());
        nucleus::utils::image_writer::rgba8_as_png(raster, texture_file_path);

        if (input_socket("region aabb").is_socket_connected()) {
            const auto& region_aabb = *std::get<data_type<const radix::geometry::Aabb<2, double>*>()>(input_socket("region aabb").get_connected_data());
            QString region_file_path = QString::fromStdString((parent_directory / "aabb.txt").string());
            write_aabb_file(region_file_path, region_aabb);
        }

        emit this->run_completed(); 
    });
}

} // namespace webgpu_engine::compute::nodes
