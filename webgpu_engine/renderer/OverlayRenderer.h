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

#include "../PipelineManager.h"
#include <QObject>
#include <radix/geometry.h>
#include <string>
#include <tl/expected.hpp>
#include <webgpu/webgpu.h>

namespace webgpu_engine {

// TODO: implement overlay rendering
class OverlayRenderer : public QObject {
    Q_OBJECT
public:
    explicit OverlayRenderer();

    void init(WGPUDevice device);

    void resize(int w, int h);

    void draw(const WGPUCommandEncoder& command_encoder);

    void set_pipeline_manager(const PipelineManager& pipeline_manager);

    static tl::expected<radix::geometry::Aabb<2, double>, std::string> load_aabb_from_file(const std::string& file_path);

private:
    WGPUDevice m_device = {};
    const PipelineManager* m_pipeline_manager = nullptr;
};

} // namespace webgpu_engine
