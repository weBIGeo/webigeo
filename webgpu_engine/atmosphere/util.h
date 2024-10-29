/*
 * Copyright (c) 2024 Lukas Herzberger
 * SPDX-License-Identifier: MIT
 */

// TODO copyright notice?

#pragma once

#include <memory>
#include <vector>
#include <webgpu/raii/BindGroup.h>
#include <webgpu/raii/Sampler.h>
#include <webgpu/raii/Texture.h>
#include <webgpu/webgpu.h>

namespace webgpu_engine::atmosphere::util {

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

} // namespace webgpu_engine::atmosphere::util
