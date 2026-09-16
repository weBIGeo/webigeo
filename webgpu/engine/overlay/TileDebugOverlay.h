/*****************************************************************************
 * weBIGeo
 * Copyright (C) 2026 Gerald Kimmersdorfer
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

#include "Overlay.h"
#include <memory>
#include <webgpu/base/Buffer.h>
#include <webgpu/base/raii/CombinedComputePipeline.h>
#include <webgpu/base/raii/TextureWithSampler.h>

namespace webgpu_engine {

// Displays the per-tile debug data that render_tiles.wgsl packs into GBuffer slot 3.
class TileDebugOverlay : public Overlay {
public:
    // Values must match the overlay_mode branches in render_tiles.wgsl.
    enum class Mode : int {
        Normals = 1,
        Tiles = 2,
        Zoomlevel = 3,
        VertexId = 4,
        PositionBuffer = 5,
        CameraDistancePosW = 6,
        CameraDistanceCalc = 7,
        GeometricDepth = 8,
        LinearDepth = 9,
        DepthDistance = 10,
        DepthPosition = 11,
        PositionDiff = 12,
        ShadingNormals = 13,
    };

    enum class Region {
        Full,
        LeftHalf,
        RightHalf,
        LeftThird,
        MiddleThird,
        RightThird,
    };

    struct Settings {
        int mode = static_cast<int>(Mode::Normals); // consumed CPU-side (forwarded to shared_config)
        float strength = 1.0f;
        float scale = 10000.0f;
        Region region = Region::Full;
    };

    TileDebugOverlay();
    ~TileDebugOverlay() override;

    void init(Context& ctx) override;
    // Pushes settings to the GPU and the selected debug mode into shared_config (consumed by the tile pass).
    // Call from the frontend whenever settings change.
    void update_settings();
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
        float strength = 1.0f;
        float scale = 10000.0f;
        uint32_t mode = 1;
        uint32_t _pad = 0;
        glm::vec2 x_region = { 0.0f, 1.0f };
        glm::vec2 _pad2 = { 0.0f, 0.0f };
    };

    webgpu::Context* m_ctx = nullptr;
    Context* m_engine_ctx = nullptr; // for shared_config access (overlay_mode)
    std::unique_ptr<webgpu::raii::CombinedComputePipeline> m_pipeline;
    std::unique_ptr<webgpu::Buffer<GpuSettings>> m_settings_uniform;
};

} // namespace webgpu_engine
