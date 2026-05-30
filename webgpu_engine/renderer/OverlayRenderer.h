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

#include <QObject>
#include <memory>
#include <radix/geometry.h>
#include <string>
#include <tl/expected.hpp>
#include <webgpu/Context.h>
#include <webgpu/raii/CombinedComputePipeline.h>
#include <webgpu/raii/TextureWithSampler.h>
#include <webgpu/raii/TextureView.h>
#include <webgpu/webgpu.h>

namespace webgpu_engine {

class OverlayRenderer : public QObject {
    Q_OBJECT
public:
    explicit OverlayRenderer();

    void init(webgpu::Context& ctx);
    void resize(int w, int h);

    void draw(const WGPUCommandEncoder& command_encoder,
        const webgpu::raii::TextureView& position_view,
        const webgpu::raii::TextureView& normal_view,
        const WGPUBindGroup& shared_config_bg,
        const WGPUBindGroup& camera_bg);

    [[nodiscard]] const webgpu::raii::TextureView* result_view() const;

    static tl::expected<radix::geometry::Aabb<2, double>, std::string> load_aabb_from_file(const std::string& file_path);

private:
    webgpu::Context* m_ctx = nullptr;
    std::unique_ptr<webgpu::raii::CombinedComputePipeline> m_pipeline;
    std::unique_ptr<webgpu::raii::TextureWithSampler> m_output_texture;
};

} // namespace webgpu_engine
