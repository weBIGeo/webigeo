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
#include <webgpu/base/raii/RawBuffer.h>
#include <webgpu/base/raii/TextureWithSampler.h>

namespace webgpu_engine {

// Visualizes depth/normal-derived debug data (see gbuffer_debug.wgsl) in a compute pass.
class TileDebugOverlay : public Overlay {
public:
    // Values must match the mode branches in gbuffer_debug.wgsl.
    enum class Mode : int {
        GeometricDepth = 1,
        LinearDepth = 2,
        DepthDistance = 3,
        DepthPosition = 4,
        ShadingNormals = 5,
        RenderTileId = 6, // decodes the render-tile id straight from the tile_ref gbuffer target
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
        int mode = static_cast<int>(Mode::GeometricDepth);
        float strength = 1.0f;
        float scale = 10000.0f;
        Region region = Region::Full;
    };

    TileDebugOverlay();
    ~TileDebugOverlay() override;

    void init(Context& ctx) override;
    // Pushes settings to the GPU. Call from the frontend whenever settings change.
    void update_settings();
    void draw(const WGPUCommandEncoder& command_encoder,
        const OverlayContext& octx,
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
    std::unique_ptr<webgpu::raii::CombinedComputePipeline> m_pipeline;
    std::unique_ptr<webgpu::Buffer<GpuSettings>> m_settings_uniform;
    // frame_local_id -> packed tile id, rebuilt every draw() call from frame_tile_ids -- lets
    // Mode::RenderTileId recover the render tile's actual id from the tile_ref gbuffer target
    // (which only carries the 16-bit frame_local_id). Same format as SlippyTileOverlay's buffer.
    std::unique_ptr<webgpu::raii::RawBuffer<glm::u32vec2>> m_frame_tile_ids_buffer;
};

} // namespace webgpu_engine
