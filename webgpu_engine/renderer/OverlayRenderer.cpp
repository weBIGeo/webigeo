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

#include "OverlayRenderer.h"

#include <QFile>
#include <QString>
#include <QTextStream>

namespace webgpu_engine {

OverlayRenderer::OverlayRenderer()
    : QObject { nullptr }
{
}

void OverlayRenderer::init(webgpu::Context& ctx) { m_ctx = &ctx; }

void OverlayRenderer::resize(int /*w*/, int /*h*/) { /* TODO: implement */ }

void OverlayRenderer::draw(const WGPUCommandEncoder& /*command_encoder*/) { /* TODO: implement */ }

tl::expected<radix::geometry::Aabb<2, double>, std::string> OverlayRenderer::load_aabb_from_file(const std::string& file_path)
{
    QFile aabb_file(QString::fromStdString(file_path));
    if (!aabb_file.open(QIODevice::ReadOnly)) {
        return tl::make_unexpected(std::format("Failed to open file {}", file_path));
    }
    QTextStream file_contents(&aabb_file);

    // parse extent file (very barebones rn, in the future we want to use geotiff anyway)
    // extent file contains the aabb of the aabb region (in world coordinates) the image overlay texture is associated with
    // each line contains exactly one floating point number (. as separator) with the following meaning:
    //   min_x
    //   min_y
    //   max_x
    //   max_y
    std::array<float, 4> contents;
    bool float_conversion_ok = false;
    for (size_t i = 0; i < contents.size(); i++) {
        QString line = file_contents.readLine();
        contents[i] = line.toFloat(&float_conversion_ok);
        if (!float_conversion_ok) {
            return tl::make_unexpected(std::format("Failed to parse file {}: Could not convert \"{}\" to float", file_path, line.toStdString()));
        }
    }

    if (contents[0] >= contents[2]) {
        return tl::make_unexpected(std::format("Failed to parse file {}: x_min ({}) must not be >= x_max ({})", file_path, contents[0], contents[2]));
    }

    if (contents[1] >= contents[3]) {
        return tl::make_unexpected(std::format("Failed to parse file {}: y_min ({}) must not be >= y_max ({})", file_path, contents[1], contents[3]));
    }

    return radix::geometry::Aabb<2, double> { { contents[0], contents[1] }, { contents[2], contents[3] } };
}

} // namespace webgpu_engine
