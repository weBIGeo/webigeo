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
#include <glm/glm.hpp>
#include <webgpu/base/Buffer.h>
#include <webgpu/base/Context.h>
#include <webgpu/base/raii/CombinedComputePipeline.h>

namespace webgpu_compute::nodes {

class TextureToRgba8Node : public Node {
    Q_OBJECT

public:
    NODE_TYPE_NAME(TextureToRgba8Node)

    struct Settings {
        glm::vec2 value_range = { 0.0f, 1.0f };
    };

    TextureToRgba8Node(webgpu::Context& ctx);
    TextureToRgba8Node(webgpu::Context& ctx, const Settings& settings);

    void set_settings(const Settings& s);
    const Settings& get_settings() const { return m_settings; }

    void serialize_settings(QJsonObject& out) const override;
    void deserialize_settings(const QJsonObject& in) override;

public slots:
    void run_impl() override;

private:
    struct Uniform {
        float value_min;
        float value_max;
        uint32_t num_channels;
        uint32_t _pad = 0;
    };

    webgpu::Context* m_ctx;
    Settings m_settings;
    webgpu::Buffer<Uniform> m_uniform;

    std::unique_ptr<webgpu::raii::CombinedComputePipeline> m_pipeline_f32;
    std::unique_ptr<webgpu::raii::CombinedComputePipeline> m_pipeline_u32;
    std::unique_ptr<webgpu::raii::CombinedComputePipeline> m_pipeline_i32;

    std::unique_ptr<webgpu::raii::TextureWithSampler> m_output_texture;
};

} // namespace webgpu_compute::nodes
