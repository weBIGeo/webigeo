/*****************************************************************************
 * weBIGeo
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

#pragma once

#include "PipelineManager.h"
#include "webgpu/raii/BindGroup.h"
#include "webgpu/raii/RawBuffer.h"
#include "webgpu/raii/TextureWithSampler.h"
#include <glm/glm.hpp>
#include <vector>

namespace webgpu_engine {

using Coordinates = glm::dvec3;
using Track = std::vector<Coordinates>;

class TrackRenderer {
public:
    TrackRenderer(WGPUDevice device, const PipelineManager& pipeline_manager);

    void add_track(Track track);

    void render(WGPUCommandEncoder command_encoder, const webgpu::raii::BindGroup& shared_config, const webgpu::raii::BindGroup& camera_config,
        const webgpu::raii::TextureView& depth_texture);

    void resize_render_target_texture(int width, int height);

    const webgpu::raii::TextureWithSampler& render_target_texture() const;

private:
    WGPUDevice m_device;
    WGPUQueue m_queue;
    const PipelineManager* m_pipeline_manager;

    std::vector<std::unique_ptr<webgpu::raii::RawBuffer<glm::fvec4>>> m_position_buffers;
    std::vector<std::unique_ptr<webgpu::raii::BindGroup>> m_bind_groups;

    std::unique_ptr<webgpu::raii::TextureWithSampler> m_render_target_texture;
};

} // namespace webgpu_engine
