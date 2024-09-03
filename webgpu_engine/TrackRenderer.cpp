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

#include "TrackRenderer.h"

#include "nucleus/srs.h"

namespace webgpu_engine {

TrackRenderer::TrackRenderer(WGPUDevice device, const PipelineManager& pipeline_manager)
    : m_device { device }
    , m_queue { wgpuDeviceGetQueue(device) }
    , m_pipeline_manager { &pipeline_manager }
{
}

void TrackRenderer::add_track(Track track)
{
    assert(!track.empty());

    m_position_buffers.emplace_back(std::make_unique<webgpu::raii::RawBuffer<glm::fvec4>>(
        m_device, WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst, track.size(), "track renderer, storage buffer for points"));
    std::vector<glm::fvec4> gpu_points;
    gpu_points.reserve(track.size());
    for (const glm::dvec3& coords : track) {
        gpu_points.push_back(glm::fvec4(nucleus::srs::lat_long_alt_to_world(coords), 1));
    }
    m_position_buffers.back()->write(m_queue, gpu_points.data(), gpu_points.size());

    m_bind_groups.emplace_back(std::make_unique<webgpu::raii::BindGroup>(m_device, m_pipeline_manager->lines_bind_group_layout(),
        std::initializer_list<WGPUBindGroupEntry> { m_position_buffers.back()->create_bind_group_entry(0) }));
}

void TrackRenderer::render(WGPUCommandEncoder command_encoder, const webgpu::raii::BindGroup& shared_config, const webgpu::raii::BindGroup& camera_config,
    const webgpu::raii::TextureView& depth_texture_view, const webgpu::raii::TextureView& color_texture)
{
    WGPURenderPassColorAttachment color_attachment {};
    color_attachment.view = color_texture.handle();
    color_attachment.resolveTarget = nullptr;
    color_attachment.loadOp = WGPULoadOp::WGPULoadOp_Load;
    color_attachment.storeOp = WGPUStoreOp::WGPUStoreOp_Store;
    color_attachment.clearValue = WGPUColor { 0.0, 0.0, 0.0, 0.0 };
    // depthSlice field for RenderPassColorAttachment (https://github.com/gpuweb/gpuweb/issues/4251)
    // this field specifies the slice to render to when rendering to a 3d texture (view)
    // passing a valid index but referencing a non-3d texture leads to an error
    // TODO use some constant that represents "undefined" for this value (I couldn't find a constant for this?)
    //     (I just guessed -1 (max unsigned int value) and it worked)
    color_attachment.depthSlice = -1;

    WGPURenderPassDepthStencilAttachment depth_stencil_attachment {};
    depth_stencil_attachment.view = depth_texture_view.handle();
    depth_stencil_attachment.depthLoadOp = WGPULoadOp_Undefined;
    depth_stencil_attachment.depthStoreOp = WGPUStoreOp_Undefined;
    depth_stencil_attachment.depthReadOnly = true;
    depth_stencil_attachment.stencilLoadOp = WGPULoadOp::WGPULoadOp_Undefined;
    depth_stencil_attachment.stencilStoreOp = WGPUStoreOp::WGPUStoreOp_Undefined;
    depth_stencil_attachment.stencilReadOnly = true;

    WGPURenderPassDescriptor render_pass_descriptor {};
    render_pass_descriptor.label = "line render render pass";
    render_pass_descriptor.colorAttachmentCount = 1;
    render_pass_descriptor.colorAttachments = &color_attachment;
    render_pass_descriptor.depthStencilAttachment = &depth_stencil_attachment;
    render_pass_descriptor.timestampWrites = nullptr;

    auto render_pass = webgpu::raii::RenderPassEncoder(command_encoder, render_pass_descriptor);
    wgpuRenderPassEncoderSetPipeline(render_pass.handle(), m_pipeline_manager->lines_render_pipeline().handle());
    wgpuRenderPassEncoderSetBindGroup(render_pass.handle(), 0, shared_config.handle(), 0, nullptr);
    wgpuRenderPassEncoderSetBindGroup(render_pass.handle(), 1, camera_config.handle(), 0, nullptr);
    for (size_t i = 0; i < m_bind_groups.size(); i++) {
        wgpuRenderPassEncoderSetBindGroup(render_pass.handle(), 2, m_bind_groups.at(i)->handle(), 0, nullptr);
        wgpuRenderPassEncoderDraw(render_pass.handle(), uint32_t(m_position_buffers.at(i)->size()), 1, 0, 0);
    }
}

} // namespace webgpu_engine
