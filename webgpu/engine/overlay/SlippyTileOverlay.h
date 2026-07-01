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
#include <memory>
#include <webgpu/base/Buffer.h>
#include <webgpu/base/raii/CombinedComputePipeline.h>

namespace webgpu_engine {

class TileSource;

/// Screen-space compute overlay that paints a tile source (e.g. ortho imagery) onto the terrain.
/// Per pixel it reads the gbuffer world position + render-tile zoom (position.w), resolves the tile's
/// array layer via the source's GPU dictionary (walking up to a resident ancestor), samples the tile
/// array, and blends premultiplied over the ping-pong background. Default z_index < 0 (pre-shading), so
/// the compose pass folds it into albedo before lighting.
class SlippyTileOverlay : public Overlay {
public:
    struct Settings {
        float opacity = 1.0f;
        uint32_t max_zoom = 20; // clamps the walk-up start zoom (render zoom already caps it in practice)
    };

    explicit SlippyTileOverlay(TileSource* source);

    void init(Context& ctx) override;
    void update_settings();

    [[nodiscard]] TileSource* source() const { return m_source; }
    void set_source(TileSource* source);
    void draw(const WGPUCommandEncoder& command_encoder,
        const webgpu::raii::TextureView& position_view,
        const webgpu::raii::TextureView& normal_view,
        const webgpu::raii::TextureView& overlay_view,
        const webgpu::raii::TextureView& depth_view,
        const WGPUBindGroup& shared_config_bg,
        const WGPUBindGroup& camera_bg,
        const webgpu::raii::TextureWithSampler& current_input,
        webgpu::raii::TextureWithSampler& target_output,
        glm::uvec2 output_size) override;

    Settings settings;

private:
    struct GpuSettings {
        float opacity = 1.0f;
        uint32_t max_zoom = 20;
        glm::vec2 _pad = { 0.0f, 0.0f };
    };

    webgpu::Context* m_ctx = nullptr;
    TileSource* m_source = nullptr;
    std::unique_ptr<webgpu::raii::CombinedComputePipeline> m_pipeline;
    std::unique_ptr<webgpu::Buffer<GpuSettings>> m_settings_uniform;
};

} // namespace webgpu_engine
