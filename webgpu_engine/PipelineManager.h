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

#include "ShaderModuleManager.h"

#include <webgpu/raii/BindGroupLayout.h>
#include <webgpu/raii/CombinedComputePipeline.h>
#include <webgpu/raii/Pipeline.h>
#include <webgpu/webgpu.h>

namespace webgpu_engine {

class PipelineManager {
public:
    PipelineManager(WGPUDevice device, ShaderModuleManager& shader_manager);

    const webgpu::raii::GenericRenderPipeline& tile_pipeline() const;
    const webgpu::raii::GenericRenderPipeline& compose_pipeline() const;
    const webgpu::raii::GenericRenderPipeline& atmosphere_pipeline() const;
    const webgpu::raii::RenderPipeline& lines_render_pipeline() const;

    const webgpu::raii::CombinedComputePipeline& normals_compute_pipeline() const;
    const webgpu::raii::CombinedComputePipeline& snow_compute_pipeline() const;
    const webgpu::raii::CombinedComputePipeline& downsample_compute_pipeline() const;
    const webgpu::raii::CombinedComputePipeline& upsample_textures_compute_pipeline() const;
    const webgpu::raii::CombinedComputePipeline& avalanche_trajectories_compute_pipeline() const;
    const webgpu::raii::CombinedComputePipeline& avalanche_trajectories_buffer_to_texture_compute_pipeline() const;
    const webgpu::raii::CombinedComputePipeline& avalanche_influence_area_compute_pipeline() const;

    const webgpu::raii::BindGroupLayout& shared_config_bind_group_layout() const;
    const webgpu::raii::BindGroupLayout& camera_bind_group_layout() const;
    const webgpu::raii::BindGroupLayout& tile_bind_group_layout() const;
    const webgpu::raii::BindGroupLayout& compose_bind_group_layout() const;
    const webgpu::raii::BindGroupLayout& normals_compute_bind_group_layout() const;
    const webgpu::raii::BindGroupLayout& snow_compute_bind_group_layout() const;
    const webgpu::raii::BindGroupLayout& overlay_bind_group_layout() const;
    const webgpu::raii::BindGroupLayout& downsample_compute_bind_group_layout() const;
    const webgpu::raii::BindGroupLayout& upsample_textures_compute_bind_group_layout() const;
    const webgpu::raii::BindGroupLayout& lines_bind_group_layout() const;
    const webgpu::raii::BindGroupLayout& depth_texture_bind_group_layout() const;
    const webgpu::raii::BindGroupLayout& avalanche_trajectories_bind_group_layout() const;
    const webgpu::raii::BindGroupLayout& avalanche_trajectories_buffer_to_texture_bind_group_layout() const;
    const webgpu::raii::BindGroupLayout& avalanche_influence_area_bind_group_layout() const;

    void create_pipelines();
    void create_bind_group_layouts();
    void release_pipelines();
    bool pipelines_created() const;

private:
    void create_tile_pipeline();
    void create_compose_pipeline();
    void create_atmosphere_pipeline();
    void create_shadow_pipeline();
    void create_normals_compute_pipeline();
    void create_snow_compute_pipeline();
    void create_downsample_compute_pipeline();
    void create_upsample_textures_compute_pipeline();
    void create_lines_render_pipeline();
    void create_avalanche_trajectories_compute_pipeline();
    void create_avalanche_trajectories_buffer_to_texture_compute_pipeline();
    void create_avalanche_influence_area_compute_pipeline();

    void create_shared_config_bind_group_layout();
    void create_camera_bind_group_layout();
    void create_tile_bind_group_layout();
    void create_compose_bind_group_layout();
    void create_normals_compute_bind_group_layout();
    void create_snow_compute_bind_group_layout();
    void create_overlay_bind_group_layout();
    void create_downsample_compute_bind_group_layout();
    void create_upsample_textures_compute_bind_group_layout();
    void create_lines_bind_group_layout();
    void create_depth_texture_bind_group_layout();
    void create_avalanche_trajectory_bind_group_layout();
    void create_avalanche_trajectory_buffer_to_texture_bind_group_layout();
    void create_avalanche_influence_area_bind_group_layout();

private:
    WGPUDevice m_device;
    ShaderModuleManager* m_shader_manager;

    std::unique_ptr<webgpu::raii::GenericRenderPipeline> m_tile_pipeline;
    std::unique_ptr<webgpu::raii::GenericRenderPipeline> m_compose_pipeline;
    std::unique_ptr<webgpu::raii::GenericRenderPipeline> m_atmosphere_pipeline;
    std::unique_ptr<webgpu::raii::RenderPipeline> m_lines_render_pipeline;

    std::unique_ptr<webgpu::raii::CombinedComputePipeline> m_normals_compute_pipeline;
    std::unique_ptr<webgpu::raii::CombinedComputePipeline> m_snow_compute_pipeline;
    std::unique_ptr<webgpu::raii::CombinedComputePipeline> m_downsample_compute_pipeline;
    std::unique_ptr<webgpu::raii::CombinedComputePipeline> m_upsample_textures_compute_pipeline;
    std::unique_ptr<webgpu::raii::CombinedComputePipeline> m_avalanche_trajectories_compute_pipeline;
    std::unique_ptr<webgpu::raii::CombinedComputePipeline> m_avalanche_trajectories_buffer_to_texture_compute_pipeline;
    std::unique_ptr<webgpu::raii::CombinedComputePipeline> m_avalanche_influence_area_compute_pipeline;

    std::unique_ptr<webgpu::raii::BindGroupLayout> m_shared_config_bind_group_layout;
    std::unique_ptr<webgpu::raii::BindGroupLayout> m_camera_bind_group_layout;
    std::unique_ptr<webgpu::raii::BindGroupLayout> m_tile_bind_group_layout;
    std::unique_ptr<webgpu::raii::BindGroupLayout> m_compose_bind_group_layout;
    std::unique_ptr<webgpu::raii::BindGroupLayout> m_normals_compute_bind_group_layout;
    std::unique_ptr<webgpu::raii::BindGroupLayout> m_overlay_bind_group_layout;
    std::unique_ptr<webgpu::raii::BindGroupLayout> m_downsample_compute_bind_group_layout;
    std::unique_ptr<webgpu::raii::BindGroupLayout> m_snow_compute_bind_group_layout;
    std::unique_ptr<webgpu::raii::BindGroupLayout> m_upsample_textures_compute_bind_group_layout;
    std::unique_ptr<webgpu::raii::BindGroupLayout> m_lines_bind_group_layout;
    std::unique_ptr<webgpu::raii::BindGroupLayout> m_depth_texture_bind_group_layout;
    std::unique_ptr<webgpu::raii::BindGroupLayout> m_avalanche_trajectories_bind_group_layout;
    std::unique_ptr<webgpu::raii::BindGroupLayout> m_avalanche_trajectories_buffer_to_texture_bind_group_layout;
    std::unique_ptr<webgpu::raii::BindGroupLayout> m_avalanche_influence_area_bind_group_layout;

    bool m_pipelines_created = false;
};
}
