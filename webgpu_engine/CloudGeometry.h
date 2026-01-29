/*****************************************************************************
 * weBIGeo
 * Copyright (C) 2023 Adam Celerek
 * Copyright (C) 2023 Gerald Kimmersdorfer
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

#include <memory>

#include "Buffer.h"
#include "PipelineManager.h"
#include "webgpu_engine/compute/GpuTileId.h"
#include <QObject>
#include <mutex>
#include <nucleus/tile/GpuArrayHelper.h>
#include <nucleus/tile/types.h>
#include <webgpu/raii/BindGroup.h>
#include <webgpu/raii/BindGroupLayout.h>
#include <webgpu/raii/TextureWithSampler.h>
#include <webgpu/webgpu.h>

namespace nucleus::camera {
class Definition;
}

namespace webgpu_engine {

class CloudGeometry : public QObject {
    Q_OBJECT
public:

    static constexpr uint32_t ZOOM_MIN = 6;
    static constexpr uint32_t ZOOM_MAX = 10;
    static constexpr glm::vec2 BOUNDS_MIN = {46.2, 9.4};
    static constexpr glm::vec2 BOUNDS_MAX = {49.2, 17.4};
    static constexpr glm::uvec2 TILE_COUNTS = {46/2, 26/2};
    static constexpr uint32_t TILE_COUNT_TOTAL = TILE_COUNTS.x * TILE_COUNTS.y;
    static constexpr uint32_t TILE_RESOLUTION_XY = 256;
    static constexpr uint32_t TILE_RESOLUTION_Z = 64;
    static constexpr uint32_t ATLAS_SCALE_XY = 4;
    static constexpr uint32_t ATLAS_SCALE_Z = 4;

    struct alignas(16) CameraConfig {
        glm::mat4 view_matrix;
        glm::mat4 proj_matrix;
        glm::mat4 inv_view_matrix;
        glm::mat4 inv_proj_matrix;
        glm::vec4 position;
    };

    struct alignas(16) ShaderParamsRender {
        CameraConfig camera;
        glm::vec4 bounds_min;
        glm::vec4 bounds_max;
        uint32_t frame_index;
        uint32_t _padding0[3] = {};
        float start_distance;
        float start_step_size;
        float end_distance;
        float end_step_size;
        float extinction_multiplier;
        float detail_strength;
        float _padding1[2] = {};
    };

    struct alignas(16) ShaderParamsUpscale {
        CameraConfig current_camera;
        CameraConfig previous_camera;
        glm::vec2 jitter;
        glm::vec2 prev_jitter;
        glm::vec2 low_res_texel_size;
        glm::vec2 high_res_texel_size;
    };

    struct TileInfo {
        glm::uint32 index;
        glm::uint32 zoom;
    };

    explicit CloudGeometry();

    void init(WGPUDevice device);

    void resize(int w, int h);

    void draw(const WGPUCommandEncoder& command_encoder, const WGPUBindGroup& depth_texture_bind_group, const nucleus::camera::Definition& camera);

    void set_pipeline_manager(const PipelineManager& pipeline_manager);

    void set_tile_limit(unsigned new_limit);

    [[nodiscard]] webgpu::raii::TextureView* result_view() const {
        return m_clouds_hi_color_texture_view.get();
    }

signals:
    void tiles_changed();

public slots:
    void update_gpu_tiles_cloud(const std::vector<nucleus::tile::Id>& deleted_tiles, const std::vector<nucleus::tile::GpuTexture3DTile>& new_tiles);

private:

    // tile coordinates of the bounds min corner at max zoom level
    glm::uvec2 m_tile_coords_offset = {};

    nucleus::tile::GpuArrayHelper m_loaded_cloud_textures;

    WGPUDevice m_device = 0;
    WGPUQueue m_queue = 0;
    const PipelineManager* m_pipeline_manager = nullptr;

    std::unique_ptr<Buffer<ShaderParamsRender>> m_render_shader_params_ubo;
    std::unique_ptr<Buffer<ShaderParamsUpscale>> m_upscale_shader_params_ubo;

    std::unique_ptr<webgpu::raii::RawBuffer<TileInfo>> m_cloud_tile_info_buffer;
    std::vector<TileInfo> m_tile_infos;

    std::unique_ptr<webgpu::raii::TextureWithSampler> m_cloud_texture_atlas;

    glm::uvec2 m_output_lo_resolution = {};
    glm::uvec2 m_output_hi_resolution = {};
    uint32_t m_frame_index = 0;
    std::unique_ptr<webgpu::raii::Texture> m_clouds_lo_color_texture;
    std::unique_ptr<webgpu::raii::TextureView> m_clouds_lo_color_texture_view;
    std::unique_ptr<webgpu::raii::Texture> m_clouds_lo_depth_texture;
    std::unique_ptr<webgpu::raii::TextureView> m_clouds_lo_depth_texture_view;
    std::unique_ptr<webgpu::raii::Texture> m_clouds_hi_color_texture;
    std::unique_ptr<webgpu::raii::TextureView> m_clouds_hi_color_texture_view;
    std::unique_ptr<webgpu::raii::Texture> m_clouds_hi_depth_texture;
    std::unique_ptr<webgpu::raii::TextureView> m_clouds_hi_depth_texture_view;
    std::unique_ptr<webgpu::raii::Sampler> m_linear_sampler;

    std::unique_ptr<webgpu::raii::BindGroup> m_render_clouds_bind_group;
    std::unique_ptr<webgpu::raii::BindGroup> m_upscale_clouds_bind_group;
    std::unique_ptr<webgpu::raii::BindGroup> m_camera_bind_group;


    std::mutex m_mutex = {};
};

} // namespace webgpu_engine
