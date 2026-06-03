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
#include <nucleus/Raster.h>
#include <radix/geometry.h>
#include <webgpu/Framebuffer.h>
#include <webgpu/raii/Pipeline.h>
#include <webgpu/raii/TextureWithSampler.h>

namespace webgpu_engine {

class TextureOverlay : public Overlay {
public:
    enum class FilterMode { Nearest, Linear };
    enum class Mode { AlphaBlend, EncodedFloat };

    // Outward-facing, user-tweakable settings.
    struct Settings {
        radix::geometry::Aabb<2, double> aabb = { { 0.0, 0.0 }, { 1.0, 1.0 } }; // world-space extent
        float opacity = 1.0f;
        Mode mode = Mode::AlphaBlend;
        glm::vec2 float_decode_range = glm::vec2(0.0f, 20.0f); // [lower, upper] for EncodedFloat mode
        FilterMode filter_mode = FilterMode::Linear; // filter_mode/use_mipmaps take effect on next load_image
        bool use_mipmaps = true;
    };
    Settings settings;

    TextureOverlay();

    // Load an RGBA8 image from disk into the overlay's own texture.
    void load_image(const QString& path);

    // Copy an external RGBA8 GPU texture (e.g. a compute node output) into the overlay's
    // own texture (with mipmaps per settings). The source is only read during this call,
    // so the overlay does not depend on its lifetime afterwards.
    void load_texture(const webgpu::raii::TextureWithSampler& source);

    // Link an external GPU texture directly (non-owning): the overlay samples it through
    // the source's own sampler. The caller must keep it alive while linked; pass nullptr
    // to unlink. While linked, settings.filter_mode/use_mipmaps have no effect.
    void link_texture(const webgpu::raii::TextureWithSampler* texture);

    // True while a linked (external) texture is in use rather than the overlay's own one.
    [[nodiscard]] bool is_linked() const { return m_linked_texture != nullptr; }

    void init(webgpu::Context& ctx) override;
    void ready(webgpu::Context& ctx) override;
    void update_gpu_settings();
    void draw(const WGPUCommandEncoder& command_encoder,
        const webgpu::raii::TextureView& position_view,
        const webgpu::raii::TextureView& normal_view,
        const webgpu::raii::TextureView& overlay_view,
        const WGPUBindGroup& shared_config_bg,
        const WGPUBindGroup& camera_bg,
        const webgpu::raii::TextureWithSampler& current_input,
        webgpu::raii::TextureWithSampler& target_output,
        glm::uvec2 output_size) override;

private:
    // Internal GPU uniform — must match WGSL struct TextureOverlaySettings layout exactly
    struct GpuSettings {
        glm::vec2 aabb_min = glm::vec2(0.0f); // offset  0
        glm::vec2 aabb_size = glm::vec2(1.0f); // offset  8
        float opacity = 1.0f; // offset 16
        uint32_t mode = 0u; // offset 20  (0=AlphaBlend, 1=EncodedFloat)
        glm::vec2 float_decode_range = glm::vec2(0.0f, 20.0f); // offset 24  (user visualization range)
        glm::vec2 encoded_float_range; // offset 32  (encoding format range)
    }; // total  40 bytes

    // (Re)creates m_overlay_texture sized w*h (RGBA8, mip levels + sampler per settings).
    void create_texture(webgpu::Context& ctx, uint32_t width, uint32_t height);

    webgpu::Context* m_ctx = nullptr;
    bool m_is_ready = false;

    std::unique_ptr<webgpu::raii::GenericRenderPipeline> m_pipeline;
    std::unique_ptr<webgpu_engine::Buffer<GpuSettings>> m_settings_uniform;
    std::unique_ptr<webgpu::raii::TextureWithSampler> m_overlay_texture; // owned source (load_image/load_texture)
    const webgpu::raii::TextureWithSampler* m_linked_texture = nullptr; // borrowed source; takes precedence while set
};

} // namespace webgpu_engine
