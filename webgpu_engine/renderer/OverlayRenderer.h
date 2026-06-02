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

#include "overlays/Overlay.h"
#include <QObject>
#include <memory>
#include <vector>
#include <webgpu/Context.h>
#include <webgpu/raii/TextureView.h>
#include <webgpu/raii/TextureWithSampler.h>
#include <webgpu/webgpu.h>

namespace webgpu_engine {

class OverlayRenderer : public QObject {
    Q_OBJECT
public:
    explicit OverlayRenderer();

    void add_overlay(std::shared_ptr<Overlay> overlay);
    void remove_overlay(size_t index);
    // Re-sort m_overlays by z_index after the GUI has assigned new z_indices.
    void sort_overlays();

    [[nodiscard]] const std::vector<std::shared_ptr<Overlay>>& overlays() const;

    void init(webgpu::Context& ctx);
    // Called once after all GPU resources are created (and the initial setup is done).
    void ready(webgpu::Context& ctx);
    void resize(int w, int h);

    void draw(const WGPUCommandEncoder& command_encoder,
        const webgpu::raii::TextureView& position_view,
        const webgpu::raii::TextureView& normal_view,
        const WGPUBindGroup& shared_config_bg,
        const WGPUBindGroup& camera_bg);

    [[nodiscard]] const webgpu::raii::TextureView* result_pre_view() const;
    [[nodiscard]] const webgpu::raii::TextureView* result_post_view() const;

private:
    std::unique_ptr<webgpu::raii::TextureWithSampler> create_output_texture(int w, int h, const char* label) const;

    webgpu::Context* m_ctx = nullptr;
    bool m_is_ready = false;
    std::vector<std::shared_ptr<Overlay>> m_overlays; // stable-sorted by z_index ascending
    std::unique_ptr<webgpu::raii::TextureWithSampler> m_pre_output_texture;
    std::unique_ptr<webgpu::raii::TextureWithSampler> m_post_output_texture;
};

} // namespace webgpu_engine
