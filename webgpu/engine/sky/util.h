/*****************************************************************************
 * weBIGeo
 * Copyright (C) 2026 Gerald Kimmersdorfer
 * Copyright (C) 2025 Patrick Komon
 * Copyright (C) 2024 Lukas Herzberger
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

/*
 * Copyright (c) 2024 Lukas Herzberger
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <glm/vec3.hpp>
#include <memory>
#include <vector>
#include <webgpu/base/raii/BindGroup.h>
#include <webgpu/base/raii/Sampler.h>
#include <webgpu/base/raii/Texture.h>
#include <webgpu/webgpu.h>

namespace webgpu_engine::sky::util {

/**
 * A helper class for textures.
 */
class LookUpTable {
public:
    LookUpTable(std::unique_ptr<webgpu::raii::Texture> texture);

    webgpu::raii::Texture& texture();
    const webgpu::raii::Texture& texture() const;

    webgpu::raii::TextureView& view();
    const webgpu::raii::TextureView& view() const;

private:
    std::unique_ptr<webgpu::raii::Texture> m_texture;
    std::unique_ptr<webgpu::raii::TextureView> m_view;
};

/**
 * A helper class for compute passes
 */
class ComputePass {
public:
    ComputePass(WGPUComputePipeline pipeline, std::vector<std::unique_ptr<webgpu::raii::BindGroup>>& bind_groups, const glm::uvec3& dispatch_dimensions);

    void encode(WGPUComputePassEncoder compute_pass, bool reset_bind_groups = false);

    void replace_bind_group(uint32_t index, std::unique_ptr<webgpu::raii::BindGroup> bind_group);
    void replace_dispatch_dimensions(const glm::uvec3& dispatch_dimensions);

private:
    WGPUComputePipeline m_pipeline;
    std::vector<std::unique_ptr<webgpu::raii::BindGroup>> m_bind_groups;
    glm::uvec3 m_dispatch_dimensions;
};

std::unique_ptr<webgpu::raii::Sampler> makeLutSampler(WGPUDevice device);

} // namespace webgpu_engine::sky::util
