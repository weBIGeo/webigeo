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
#include <filesystem>
#include <limits>
#include <nucleus/srs.h>
#include <nucleus/utils/image_writer.h>
#include <qdir.h>

namespace webgpu_engine::compute::nodes {

webgpu_engine::compute::nodes::TileExportNode::TileExportNode(WGPUDevice device, ExportSettings settings)
    : Node(
          {
              InputSocket(*this, "tile ids", data_type<const std::vector<tile::Id>*>()),
              InputSocket(*this, "hash map", data_type<GpuHashMap<tile::Id, uint32_t, GpuTileId>*>()),
              InputSocket(*this, "textures", data_type<TileStorageTexture*>()),
          },
          {})
    , m_device(device)
    , m_settings(settings)
    , m_should_output_files { true }
{
}

void TileExportNode::run_impl()
{
    qDebug() << "running TileExportNode ...";

    m_exported_tile_count = 0;

    // get tile ids to process
    const auto& tile_ids = *std::get<data_type<const std::vector<tile::Id>*>()>(input_socket("tile ids").get_connected_data());
    const auto& hash_map = *std::get<data_type<GpuHashMap<tile::Id, uint32_t, GpuTileId>*>()>(input_socket("hash map").get_connected_data());
    auto& textures = *std::get<data_type<TileStorageTexture*>()>(input_socket("textures").get_connected_data());

    m_total_tile_count = tile_ids.size();
    m_tile_size = glm::uvec2(textures.texture().texture().width(), textures.texture().texture().height());

    for (size_t i = 0; i < tile_ids.size(); i++) {
        textures.texture().texture().read_back_async(m_device, i, [this, &hash_map](size_t layer_index, std::shared_ptr<QByteArray> data) {
            // const auto& tile_id = tile_ids[layer_index];
            const auto& tile_id = hash_map.key_with_value(layer_index);
            m_tile_data[tile_id] = data;

            m_exported_tile_count++;
            if (m_exported_tile_count == m_total_tile_count) {
                readback_done();
            }
        });
    }
}

void TileExportNode::readback_done()
{

    // check how many byte per pixel
    uint32_t bpp = 0;
    {
        const auto bytes_per_image = m_tile_data.begin()->second->size();
        bpp = bytes_per_image / (m_tile_size.x * m_tile_size.y);
    }

    glm::uvec2 effective_tile_size = m_tile_size;
    if (m_settings.remove_overlap) {
        effective_tile_size = m_tile_size - glm::uvec2(1);
    }

    // Read all tiles and convert them to u8vec4 raster (this step cuts off the overlap)
    std::map<tile::Id, nucleus::Raster<glm::u8vec4>> rasters;
    for (const auto& tile : m_tile_data) {
        const auto& tile_id = tile.first;
        rasters[tile_id] = nucleus::Raster<glm::u8vec4>(effective_tile_size);
        for (uint32_t y = 0; y < effective_tile_size.y; y++) {
            for (uint32_t x = 0; x < effective_tile_size.x; x++) {
                const auto& src = tile.second;
                std::vector<glm::u8vec4>& dest = rasters[tile_id].buffer();

                const uint32_t index_src = (y * m_tile_size.x + x) * bpp;
                const uint8_t r = src->at(index_src + 0);
                const uint8_t g = bpp > 1 ? src->at(index_src + 1) : 0;
                const uint8_t b = bpp > 2 ? src->at(index_src + 2) : 0;
                const uint8_t a = bpp > 3 ? src->at(index_src + 3) : 255;

                const uint32_t index_dest = y * effective_tile_size.x + x;
                dest[index_dest] = glm::u8vec4(r, g, b, a);
            }
        }
    }

    // Make sure output directory exists
    const auto parent_directory = std::filesystem::path(m_settings.output_directory);
    std::filesystem::create_directory(parent_directory);
    qDebug() << "Writing files to " << std::filesystem::canonical(parent_directory).string();

    if (m_settings.stitch_tiles) {
        // iterate through all tiles and save the bounds for each zoom level
        std::map<uint32_t, glm::uvec4> bounds; // 0...x_min, 1...y_min, 2...x_max, 3...y_max
        std::map<uint32_t, glm::dvec4> bounds_srs; // 0...x_min, 1...y_min, 2...x_max, 3...y_max
        for (const auto& tile : m_tile_data) {
            const auto& tile_id = tile.first;
            // Make sure that a bound uvec4 for this zoom_level already exists and that its initialized
            // with proper values
            if (bounds.find(tile_id.zoom_level) == bounds.end()) {
                bounds[tile_id.zoom_level] = glm::uvec4(std::numeric_limits<uint32_t>::max(), std::numeric_limits<uint32_t>::max(),
                    std::numeric_limits<uint32_t>::min(), std::numeric_limits<uint32_t>::min());
                bounds_srs[tile_id.zoom_level] = glm::dvec4(std::numeric_limits<double>::max(), std::numeric_limits<double>::max(),
                    std::numeric_limits<double>::min(), std::numeric_limits<double>::min());
            }

            bounds[tile_id.zoom_level].x = std::min(bounds[tile_id.zoom_level].x, tile_id.coords.x);
            bounds[tile_id.zoom_level].y = std::min(bounds[tile_id.zoom_level].y, tile_id.coords.y);
            bounds[tile_id.zoom_level].z = std::max(bounds[tile_id.zoom_level].z, tile_id.coords.x);
            bounds[tile_id.zoom_level].w = std::max(bounds[tile_id.zoom_level].w, tile_id.coords.y);

            tile::SrsBounds srs = nucleus::srs::tile_bounds(tile_id);
            bounds_srs[tile_id.zoom_level].x = std::min(bounds_srs[tile_id.zoom_level].x, srs.min.x);
            bounds_srs[tile_id.zoom_level].y = std::min(bounds_srs[tile_id.zoom_level].y, srs.min.y);
            bounds_srs[tile_id.zoom_level].z = std::max(bounds_srs[tile_id.zoom_level].z, srs.max.x);
            bounds_srs[tile_id.zoom_level].w = std::max(bounds_srs[tile_id.zoom_level].w, srs.max.y);
        }

        // Calculate the size in x,y (in tiles and pixels) for each zoom level
        std::map<uint32_t, glm::uvec2> size_tiles;
        std::map<uint32_t, glm::uvec2> size_pixels;
        for (const auto& bound : bounds) {
            const auto& zoom_level = bound.first;
            auto& size_tile = size_tiles[zoom_level];
            size_tile = glm::uvec2(bound.second.z - bound.second.x + 1, bound.second.w - bound.second.y + 1);
            size_pixels[zoom_level] = size_tile * effective_tile_size;
        }

        // Check for max image size
        for (const auto& size_pixel : size_pixels) {
            if (size_pixel.second.x > MAX_STITCHED_IMAGE_SIZE || size_pixel.second.y > MAX_STITCHED_IMAGE_SIZE) {
                emit run_failed(NodeRunFailureInfo(*this,
                    std::format("Stitched image size would exceeds maximum size of {}x{} pixel for zoom level {}", MAX_STITCHED_IMAGE_SIZE,
                        MAX_STITCHED_IMAGE_SIZE, size_pixel.first)));
                return;
            }
        }

        // Create an output raster image for each zoom level
        std::map<uint32_t, nucleus::Raster<glm::u8vec4>> stitched_rasters;
        for (const auto& size_pixel : size_pixels) {
            stitched_rasters[size_pixel.first] = nucleus::Raster<glm::u8vec4>(size_pixel.second);
            // Go through all of the raster tiles (rasters) with appropriate zoom level and copy the data into the stitched raster
            for (const auto& tile : rasters) {
                const auto& tile_id = tile.first;
                const auto& raster = tile.second;

                if (tile_id.zoom_level != size_pixel.first) {
                    continue; // Skip tiles of a different zoom level
                }

                glm::uvec2 offset_tile(tile_id.coords.x - bounds[tile_id.zoom_level].x, tile_id.coords.y - bounds[tile_id.zoom_level].y);
                if (m_settings.stitch_inverted_y) {
                    offset_tile.y = (size_tiles[tile_id.zoom_level].y - 1) - offset_tile.y;
                }
                const glm::uvec2 offset_pixel = offset_tile * effective_tile_size;

                for (uint32_t y = 0; y < effective_tile_size.y; y++) {
                    for (uint32_t x = 0; x < effective_tile_size.x; x++) {
                        const uint32_t src_index = y * effective_tile_size.x + x;
                        const uint32_t dest_index = (y + offset_pixel.y) * size_pixel.second.x + (x + offset_pixel.x);
                        stitched_rasters[size_pixel.first].buffer()[dest_index] = raster.buffer()[src_index];
                    }
                }
            }
        }

        // Save the stitched raster images
        for (const auto& stitched_raster : stitched_rasters) {
            uint32_t zoom_level = stitched_raster.first;
            const auto& raster = stitched_raster.second;

            QString file_path = QString::fromStdString((parent_directory / (std::to_string(zoom_level) + ".png")).string());
            nucleus::utils::image_writer::rgba8_as_png(raster, file_path);

            if (m_settings.stitch_export_aabb_text_files) {
                // Write out text file with the bounding box in SRS coordinates as saved in bounds_srs
                QString file_path = QString::fromStdString((parent_directory / (std::to_string(zoom_level) + "_aabb.txt")).string());
                QFile file(file_path);
                if (file.open(QIODevice::WriteOnly)) {
                    QTextStream stream(&file);
                    stream.setRealNumberPrecision(30);
                    stream << bounds_srs[zoom_level].x << "\n";
                    stream << bounds_srs[zoom_level].y << "\n";
                    stream << bounds_srs[zoom_level].z << "\n";
                    stream << bounds_srs[zoom_level].w << "\n";
                    file.close();
                }
            }
        }

    } else {

        // Go through all of the raster tiles (rasters) and save them as PNGs
        for (const auto& raster : rasters) {
            const auto& tile_id = raster.first;

            std::filesystem::path tile_directory = parent_directory / std::to_string(tile_id.zoom_level) / std::to_string(tile_id.coords.x);
            std::filesystem::create_directories(tile_directory);

            QString file_path = QString::fromStdString((tile_directory / (std::to_string(tile_id.coords.y) + ".png")).string());
            nucleus::utils::image_writer::rgba8_as_png(raster.second, file_path);
        }
    }

    emit this->run_completed();
}

} // namespace webgpu_engine::compute::nodes
