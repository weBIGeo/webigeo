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

#include "Overlay.h"
#include "webgpu_engine/Buffer.h"
#include <QString>
#include <memory>
#include <webgpu/raii/CombinedComputePipeline.h>
#include <webgpu/raii/TextureWithSampler.h>

namespace webgpu_engine {

class TextureOverlay : public Overlay {
public:
    enum class FilterMode { Nearest, Linear };

    // Outward-facing settings (affects sampler on next load_image call)
    struct Settings {
        FilterMode filter_mode = FilterMode::Linear;
        bool use_mipmaps = true;
    };
    Settings settings;

    TextureOverlay();

    // Load an RGBA image from disk. Can be called before or after GPU init;
    // if called after post_recreate_all() the texture is uploaded immediately.
    void load_image(const QString& path);

    // Set the world-space AABB in double precision. Can be called at any time.
    void set_aabb(glm::dvec2 min, glm::dvec2 max);

    void init(webgpu::Context& ctx) override;
    void post_recreate_all(webgpu::Context& ctx) override;
    void draw(const WGPUCommandEncoder& command_encoder,
        const webgpu::raii::TextureView& position_view,
        const webgpu::raii::TextureView& normal_view,
        const WGPUBindGroup& shared_config_bg,
        const WGPUBindGroup& camera_bg,
        const webgpu::raii::TextureView& output_view,
        glm::uvec2 output_size) override;

private:
    // Internal GPU uniform — matches WGSL struct TextureOverlaySettings
    struct GpuSettings {
        glm::vec2 aabb_min  = glm::vec2(0.0f);
        glm::vec2 aabb_size = glm::vec2(1.0f); // precomputed in double on CPU
    };

    void upload_texture(webgpu::Context& ctx);
    void update_gpu_settings();

    webgpu::Context* m_ctx = nullptr;
    bool m_post_recreate_called = false;

    // Pending state — stored before GPU is ready
    glm::dvec2 m_aabb_min_d = glm::dvec2(0.0);
    glm::dvec2 m_aabb_max_d = glm::dvec2(1.0);
    QString m_image_path;

    std::unique_ptr<webgpu::raii::CombinedComputePipeline> m_pipeline;
    std::unique_ptr<webgpu_engine::Buffer<GpuSettings>> m_settings_uniform;
    std::unique_ptr<webgpu::raii::TextureWithSampler> m_overlay_texture;
};

} // namespace webgpu_engine
