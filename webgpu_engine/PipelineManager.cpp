/*****************************************************************************
 * weBIGeo
 * Copyright (C) 2024 Patrick Komon
 * Copyright (C) 2024 Gerald Kimmersdorfer
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

#include "PipelineManager.h"

#include <webgpu/raii/BindGroupLayout.h>
#include <webgpu/raii/Pipeline.h>
#include <webgpu/util/VertexBufferInfo.h>

namespace webgpu_engine {

PipelineManager::PipelineManager(WGPUDevice device, ShaderModuleManager& shader_manager)
    : m_device(device)
    , m_shader_manager(&shader_manager)
{
}

const webgpu::raii::GenericRenderPipeline& PipelineManager::tile_pipeline() const { return *m_tile_pipeline; }

const webgpu::raii::GenericRenderPipeline& PipelineManager::compose_pipeline() const { return *m_compose_pipeline; }

const webgpu::raii::GenericRenderPipeline& PipelineManager::atmosphere_pipeline() const { return *m_atmosphere_pipeline; }

const webgpu::raii::CombinedComputePipeline& PipelineManager::normals_compute_pipeline() const { return *m_normals_compute_pipeline; }

const webgpu::raii::CombinedComputePipeline& PipelineManager::snow_compute_pipeline() const { return *m_snow_compute_pipeline; }

const webgpu::raii::CombinedComputePipeline& PipelineManager::downsample_compute_pipeline() const { return *m_downsample_compute_pipeline; }

const webgpu::raii::BindGroupLayout& PipelineManager::shared_config_bind_group_layout() const { return *m_shared_config_bind_group_layout; }

const webgpu::raii::BindGroupLayout& PipelineManager::camera_bind_group_layout() const { return *m_camera_bind_group_layout; }

const webgpu::raii::BindGroupLayout& PipelineManager::tile_bind_group_layout() const { return *m_tile_bind_group_layout; }

const webgpu::raii::BindGroupLayout& PipelineManager::compose_bind_group_layout() const { return *m_compose_bind_group_layout; }

const webgpu::raii::BindGroupLayout& PipelineManager::normals_compute_bind_group_layout() const { return *m_normals_compute_bind_group_layout; }

const webgpu::raii::BindGroupLayout& PipelineManager::snow_compute_bind_group_layout() const { return *m_snow_compute_bind_group_layout; }

const webgpu::raii::BindGroupLayout& PipelineManager::overlay_bind_group_layout() const { return *m_overlay_bind_group_layout; }

const webgpu::raii::BindGroupLayout& PipelineManager::downsample_compute_bind_group_layout() const { return *m_downsample_compute_bind_group_layout; }

void PipelineManager::create_pipelines()
{
    create_bind_group_layouts();
    create_tile_pipeline();
    create_compose_pipeline();
    create_atmosphere_pipeline();
    create_normals_compute_pipeline();
    create_snow_compute_pipeline();
    create_downsample_compute_pipeline();
    m_pipelines_created = true;
}

void PipelineManager::create_bind_group_layouts()
{
    create_shared_config_bind_group_layout();
    create_camera_bind_group_layout();
    create_tile_bind_group_layout();
    create_compose_bind_group_layout();
    create_normals_compute_bind_group_layout();
    create_snow_compute_bind_group_layout();
    create_overlay_bind_group_layout();
    create_downsample_compute_bind_group_layout();
}

void PipelineManager::release_pipelines()
{
    m_tile_pipeline.release();
    m_compose_pipeline.release();
    m_atmosphere_pipeline.release();
    m_normals_compute_pipeline.release();
    m_downsample_compute_pipeline.release();
    m_pipelines_created = false;
}

bool PipelineManager::pipelines_created() const { return m_pipelines_created; }

void PipelineManager::create_tile_pipeline()
{
    webgpu::util::SingleVertexBufferInfo bounds_buffer_info(WGPUVertexStepMode_Instance);
    bounds_buffer_info.add_attribute<float, 4>(0);
    webgpu::util::SingleVertexBufferInfo texture_layer_buffer_info(WGPUVertexStepMode_Instance);
    texture_layer_buffer_info.add_attribute<int32_t, 1>(1);
    webgpu::util::SingleVertexBufferInfo tileset_id_buffer_info(WGPUVertexStepMode_Instance);
    tileset_id_buffer_info.add_attribute<int32_t, 1>(2);
    webgpu::util::SingleVertexBufferInfo zoomlevel_buffer_info(WGPUVertexStepMode_Instance);
    zoomlevel_buffer_info.add_attribute<int32_t, 1>(3);
    webgpu::util::SingleVertexBufferInfo tile_id_buffer_info(WGPUVertexStepMode_Instance);
    tile_id_buffer_info.add_attribute<uint32_t, 4>(4);

    webgpu::FramebufferFormat format {};
    format.depth_format = WGPUTextureFormat_Depth24Plus;
    format.color_formats.emplace_back(WGPUTextureFormat_RGBA8Unorm);
    format.color_formats.emplace_back(WGPUTextureFormat_RGBA32Float);
    format.color_formats.emplace_back(WGPUTextureFormat_RG16Uint);

    m_tile_pipeline = std::make_unique<webgpu::raii::GenericRenderPipeline>(m_device, m_shader_manager->tile(), m_shader_manager->tile(),
        std::vector<webgpu::util::SingleVertexBufferInfo> {
            bounds_buffer_info, texture_layer_buffer_info, tileset_id_buffer_info, zoomlevel_buffer_info, tile_id_buffer_info },
        format,
        std::vector<const webgpu::raii::BindGroupLayout*> {
            m_shared_config_bind_group_layout.get(), m_camera_bind_group_layout.get(), m_tile_bind_group_layout.get(), m_overlay_bind_group_layout.get() });
}

void PipelineManager::create_compose_pipeline()
{
    webgpu::FramebufferFormat format {};
    format.depth_format = WGPUTextureFormat_Depth24Plus; // ImGUI needs attached depth buffer
    format.color_formats.emplace_back(WGPUTextureFormat_BGRA8Unorm);

    m_compose_pipeline = std::make_unique<webgpu::raii::GenericRenderPipeline>(m_device, m_shader_manager->screen_pass_vert(), m_shader_manager->compose_frag(),
        std::vector<webgpu::util::SingleVertexBufferInfo> {}, format,
        std::vector<const webgpu::raii::BindGroupLayout*> {
            m_shared_config_bind_group_layout.get(), m_camera_bind_group_layout.get(), m_compose_bind_group_layout.get() });
}

void PipelineManager::create_atmosphere_pipeline()
{
    webgpu::FramebufferFormat format {};
    format.depth_format = WGPUTextureFormat_Undefined;  // no depth buffer needed
    format.color_formats.emplace_back(WGPUTextureFormat_RGBA8Unorm);

    m_atmosphere_pipeline = std::make_unique<webgpu::raii::GenericRenderPipeline>(m_device, m_shader_manager->screen_pass_vert(),
        m_shader_manager->atmosphere_frag(), std::vector<webgpu::util::SingleVertexBufferInfo> {}, format,
        std::vector<const webgpu::raii::BindGroupLayout*> { m_camera_bind_group_layout.get() });
}

void PipelineManager::create_shadow_pipeline() {
    //TODO
}

void PipelineManager::create_normals_compute_pipeline()
{
    m_normals_compute_pipeline = std::make_unique<webgpu::raii::CombinedComputePipeline>(
        m_device, m_shader_manager->normals_compute(), std::vector<const webgpu::raii::BindGroupLayout*> { m_normals_compute_bind_group_layout.get() });
}

void PipelineManager::create_snow_compute_pipeline()
{
    m_snow_compute_pipeline = std::make_unique<webgpu::raii::CombinedComputePipeline>(
        m_device, m_shader_manager->snow_compute(), std::vector<const webgpu::raii::BindGroupLayout*> { m_snow_compute_bind_group_layout.get() });
}

void PipelineManager::create_downsample_compute_pipeline()
{
    m_downsample_compute_pipeline = std::make_unique<webgpu::raii::CombinedComputePipeline>(
        m_device, m_shader_manager->downsample_compute(), std::vector<const webgpu::raii::BindGroupLayout*> { m_downsample_compute_bind_group_layout.get() });
}

void PipelineManager::create_shared_config_bind_group_layout()
{
    WGPUBindGroupLayoutEntry shared_config_entry {};
    shared_config_entry.binding = 0;
    shared_config_entry.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    shared_config_entry.buffer.type = WGPUBufferBindingType_Uniform;
    shared_config_entry.buffer.minBindingSize = 0;
    m_shared_config_bind_group_layout = std::make_unique<webgpu::raii::BindGroupLayout>(
        m_device, std::vector<WGPUBindGroupLayoutEntry> { shared_config_entry }, "shared config bind group layout");
}

void PipelineManager::create_camera_bind_group_layout()
{
    WGPUBindGroupLayoutEntry camera_entry {};
    camera_entry.binding = 0;
    camera_entry.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    camera_entry.buffer.type = WGPUBufferBindingType_Uniform;
    camera_entry.buffer.minBindingSize = 0;
    m_camera_bind_group_layout
        = std::make_unique<webgpu::raii::BindGroupLayout>(m_device, std::vector<WGPUBindGroupLayoutEntry> { camera_entry }, "camera bind group layout");
}

void PipelineManager::create_tile_bind_group_layout()
{
    WGPUBindGroupLayoutEntry n_vertices_entry {};
    n_vertices_entry.binding = 0;
    n_vertices_entry.visibility = WGPUShaderStage_Vertex;
    n_vertices_entry.buffer.type = WGPUBufferBindingType_Uniform;
    n_vertices_entry.buffer.minBindingSize = 0;

    WGPUBindGroupLayoutEntry heightmap_texture_entry {};
    heightmap_texture_entry.binding = 1;
    heightmap_texture_entry.visibility = WGPUShaderStage_Vertex;
    heightmap_texture_entry.texture.sampleType = WGPUTextureSampleType_Uint;
    heightmap_texture_entry.texture.viewDimension = WGPUTextureViewDimension_2DArray;

    WGPUBindGroupLayoutEntry heightmap_texture_sampler {};
    heightmap_texture_sampler.binding = 2;
    heightmap_texture_sampler.visibility = WGPUShaderStage_Vertex;
    heightmap_texture_sampler.sampler.type = WGPUSamplerBindingType_Filtering;

    WGPUBindGroupLayoutEntry ortho_texture_entry {};
    ortho_texture_entry.binding = 3;
    ortho_texture_entry.visibility = WGPUShaderStage_Fragment;
    ortho_texture_entry.texture.sampleType = WGPUTextureSampleType_Float;
    ortho_texture_entry.texture.viewDimension = WGPUTextureViewDimension_2DArray;

    WGPUBindGroupLayoutEntry ortho_texture_sampler {};
    ortho_texture_sampler.binding = 4;
    ortho_texture_sampler.visibility = WGPUShaderStage_Fragment;
    ortho_texture_sampler.sampler.type = WGPUSamplerBindingType_Filtering;

    m_tile_bind_group_layout = std::make_unique<webgpu::raii::BindGroupLayout>(m_device,
        std::vector<WGPUBindGroupLayoutEntry> {
            n_vertices_entry, heightmap_texture_entry, heightmap_texture_sampler, ortho_texture_entry, ortho_texture_sampler },
        "tile bind group");
}

void PipelineManager::create_compose_bind_group_layout()
{
    WGPUBindGroupLayoutEntry albedo_texture_entry {};
    albedo_texture_entry.binding = 0;
    albedo_texture_entry.visibility = WGPUShaderStage_Fragment;
    albedo_texture_entry.texture.sampleType = WGPUTextureSampleType_Float;
    albedo_texture_entry.texture.viewDimension = WGPUTextureViewDimension_2D;

    WGPUBindGroupLayoutEntry position_texture_entry {};
    position_texture_entry.binding = 1;
    position_texture_entry.visibility = WGPUShaderStage_Fragment;
    position_texture_entry.texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;
    position_texture_entry.texture.viewDimension = WGPUTextureViewDimension_2D;

    WGPUBindGroupLayoutEntry normal_texture_entry {};
    normal_texture_entry.binding = 2;
    normal_texture_entry.visibility = WGPUShaderStage_Fragment;
    normal_texture_entry.texture.sampleType = WGPUTextureSampleType_Uint;
    normal_texture_entry.texture.viewDimension = WGPUTextureViewDimension_2D;

    WGPUBindGroupLayoutEntry atmosphere_texture_entry {};
    atmosphere_texture_entry.binding = 3;
    atmosphere_texture_entry.visibility = WGPUShaderStage_Fragment;
    atmosphere_texture_entry.texture.sampleType = WGPUTextureSampleType_Float;
    atmosphere_texture_entry.texture.viewDimension = WGPUTextureViewDimension_2D;

    m_compose_bind_group_layout = std::make_unique<webgpu::raii::BindGroupLayout>(m_device,
        std::vector<WGPUBindGroupLayoutEntry> {
            albedo_texture_entry,
            position_texture_entry,
            normal_texture_entry,
            atmosphere_texture_entry,
        },
        "compose bind group layout");
}

void PipelineManager::create_normals_compute_bind_group_layout()
{
    WGPUBindGroupLayoutEntry compute_input_tile_ids_entry {};
    compute_input_tile_ids_entry.binding = 0;
    compute_input_tile_ids_entry.visibility = WGPUShaderStage_Compute;
    compute_input_tile_ids_entry.buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
    compute_input_tile_ids_entry.buffer.minBindingSize = 0;

    WGPUBindGroupLayoutEntry compute_input_bounds_entry {};
    compute_input_bounds_entry.binding = 1;
    compute_input_bounds_entry.visibility = WGPUShaderStage_Compute;
    compute_input_bounds_entry.buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
    compute_input_bounds_entry.buffer.minBindingSize = 0;

    WGPUBindGroupLayoutEntry compute_key_buffer_entry {};
    compute_key_buffer_entry.binding = 2;
    compute_key_buffer_entry.visibility = WGPUShaderStage_Compute;
    compute_key_buffer_entry.buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
    compute_key_buffer_entry.buffer.minBindingSize = 0;

    WGPUBindGroupLayoutEntry compute_value_buffer_entry {};
    compute_value_buffer_entry.binding = 3;
    compute_value_buffer_entry.visibility = WGPUShaderStage_Compute;
    compute_value_buffer_entry.buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
    compute_value_buffer_entry.buffer.minBindingSize = 0;

    WGPUBindGroupLayoutEntry compute_input_height_textures_entry {};
    compute_input_height_textures_entry.binding = 4;
    compute_input_height_textures_entry.visibility = WGPUShaderStage_Compute;
    compute_input_height_textures_entry.texture.sampleType = WGPUTextureSampleType_Uint;
    compute_input_height_textures_entry.texture.viewDimension = WGPUTextureViewDimension_2DArray;

    WGPUBindGroupLayoutEntry compute_output_tiles_entry {};
    compute_output_tiles_entry.binding = 5;
    compute_output_tiles_entry.visibility = WGPUShaderStage_Compute;
    compute_output_tiles_entry.storageTexture.viewDimension = WGPUTextureViewDimension_2DArray;
    compute_output_tiles_entry.storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
    compute_output_tiles_entry.storageTexture.format = WGPUTextureFormat_RGBA8Unorm;

    m_normals_compute_bind_group_layout = std::make_unique<webgpu::raii::BindGroupLayout>(m_device,
        std::vector<WGPUBindGroupLayoutEntry> { compute_input_tile_ids_entry, compute_input_bounds_entry, compute_key_buffer_entry, compute_value_buffer_entry,
            compute_input_height_textures_entry, compute_output_tiles_entry },
        "normals compute bind group layout");
}

void PipelineManager::create_snow_compute_bind_group_layout()
{
    WGPUBindGroupLayoutEntry input_tile_ids_entry {};
    input_tile_ids_entry.binding = 0;
    input_tile_ids_entry.visibility = WGPUShaderStage_Compute;
    input_tile_ids_entry.buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
    input_tile_ids_entry.buffer.minBindingSize = 0;

    WGPUBindGroupLayoutEntry input_bounds_entry {};
    input_bounds_entry.binding = 1;
    input_bounds_entry.visibility = WGPUShaderStage_Compute;
    input_bounds_entry.buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
    input_bounds_entry.buffer.minBindingSize = 0;

    WGPUBindGroupLayoutEntry input_snow_settings {};
    input_snow_settings.binding = 2;
    input_snow_settings.visibility = WGPUShaderStage_Compute;
    input_snow_settings.buffer.type = WGPUBufferBindingType_Uniform;
    input_snow_settings.buffer.minBindingSize = 0;

    WGPUBindGroupLayoutEntry key_buffer_entry {};
    key_buffer_entry.binding = 3;
    key_buffer_entry.visibility = WGPUShaderStage_Compute;
    key_buffer_entry.buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
    key_buffer_entry.buffer.minBindingSize = 0;

    WGPUBindGroupLayoutEntry value_buffer_entry {};
    value_buffer_entry.binding = 4;
    value_buffer_entry.visibility = WGPUShaderStage_Compute;
    value_buffer_entry.buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
    value_buffer_entry.buffer.minBindingSize = 0;

    WGPUBindGroupLayoutEntry input_height_textures_entry {};
    input_height_textures_entry.binding = 5;
    input_height_textures_entry.visibility = WGPUShaderStage_Compute;
    input_height_textures_entry.texture.sampleType = WGPUTextureSampleType_Uint;
    input_height_textures_entry.texture.viewDimension = WGPUTextureViewDimension_2DArray;

    WGPUBindGroupLayoutEntry output_tiles_entry {};
    output_tiles_entry.binding = 6;
    output_tiles_entry.visibility = WGPUShaderStage_Compute;
    output_tiles_entry.storageTexture.viewDimension = WGPUTextureViewDimension_2DArray;
    output_tiles_entry.storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
    output_tiles_entry.storageTexture.format = WGPUTextureFormat_RGBA8Unorm;

    m_snow_compute_bind_group_layout = std::make_unique<webgpu::raii::BindGroupLayout>(m_device,
        std::vector<WGPUBindGroupLayoutEntry> { input_tile_ids_entry, input_bounds_entry, input_snow_settings, key_buffer_entry, value_buffer_entry,
            input_height_textures_entry, output_tiles_entry },
        "snow compute bind group layout");
}

void PipelineManager::create_overlay_bind_group_layout()
{
    WGPUBindGroupLayoutEntry overlay_key_buffer_entry {};
    overlay_key_buffer_entry.binding = 0;
    overlay_key_buffer_entry.visibility = WGPUShaderStage_Fragment;
    overlay_key_buffer_entry.buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
    overlay_key_buffer_entry.buffer.minBindingSize = 0;

    WGPUBindGroupLayoutEntry overlay_value_buffer_entry {};
    overlay_value_buffer_entry.binding = 1;
    overlay_value_buffer_entry.visibility = WGPUShaderStage_Fragment;
    overlay_value_buffer_entry.buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
    overlay_value_buffer_entry.buffer.minBindingSize = 0;

    WGPUBindGroupLayoutEntry overlay_input_overlay_textures_entry {};
    overlay_input_overlay_textures_entry.binding = 2;
    overlay_input_overlay_textures_entry.visibility = WGPUShaderStage_Fragment;
    overlay_input_overlay_textures_entry.texture.sampleType = WGPUTextureSampleType_Float;
    overlay_input_overlay_textures_entry.texture.viewDimension = WGPUTextureViewDimension_2DArray;

    m_overlay_bind_group_layout = std::make_unique<webgpu::raii::BindGroupLayout>(m_device,
        std::vector<WGPUBindGroupLayoutEntry> { overlay_key_buffer_entry, overlay_value_buffer_entry, overlay_input_overlay_textures_entry },
        "overlay bind group layout");
}

void PipelineManager::create_downsample_compute_bind_group_layout()
{
    WGPUBindGroupLayoutEntry downsample_compute_input_tile_ids_entry {};
    downsample_compute_input_tile_ids_entry.binding = 0;
    downsample_compute_input_tile_ids_entry.visibility = WGPUShaderStage_Compute;
    downsample_compute_input_tile_ids_entry.buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
    downsample_compute_input_tile_ids_entry.buffer.minBindingSize = 0;

    WGPUBindGroupLayoutEntry downsample_compute_key_buffer_entry {};
    downsample_compute_key_buffer_entry.binding = 1;
    downsample_compute_key_buffer_entry.visibility = WGPUShaderStage_Compute;
    downsample_compute_key_buffer_entry.buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
    downsample_compute_key_buffer_entry.buffer.minBindingSize = 0;

    WGPUBindGroupLayoutEntry downsample_compute_value_buffer_entry {};
    downsample_compute_value_buffer_entry.binding = 2;
    downsample_compute_value_buffer_entry.visibility = WGPUShaderStage_Compute;
    downsample_compute_value_buffer_entry.buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
    downsample_compute_value_buffer_entry.buffer.minBindingSize = 0;

    WGPUBindGroupLayoutEntry downsample_compute_input_textures_entry {};
    downsample_compute_input_textures_entry.binding = 3;
    downsample_compute_input_textures_entry.visibility = WGPUShaderStage_Compute;
    downsample_compute_input_textures_entry.texture.sampleType = WGPUTextureSampleType_Float;
    downsample_compute_input_textures_entry.texture.viewDimension = WGPUTextureViewDimension_2DArray;

    WGPUBindGroupLayoutEntry downsample_compute_output_textures_entry {};
    downsample_compute_output_textures_entry.binding = 4;
    downsample_compute_output_textures_entry.visibility = WGPUShaderStage_Compute;
    downsample_compute_output_textures_entry.storageTexture.viewDimension = WGPUTextureViewDimension_2DArray;
    downsample_compute_output_textures_entry.storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
    downsample_compute_output_textures_entry.storageTexture.format = WGPUTextureFormat_RGBA8Unorm;

    m_downsample_compute_bind_group_layout = std::make_unique<webgpu::raii::BindGroupLayout>(m_device,
        std::vector<WGPUBindGroupLayoutEntry> { downsample_compute_input_tile_ids_entry, downsample_compute_key_buffer_entry,
            downsample_compute_value_buffer_entry, downsample_compute_input_textures_entry, downsample_compute_output_textures_entry },
        "compute: downsample bind group layout");
}
}
