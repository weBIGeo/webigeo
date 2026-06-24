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

#include "SkyRenderer.h"

#include "nucleus/camera/Definition.h"
#include <glm/gtc/matrix_inverse.hpp>
#include <webgpu/base/Context.h>
#include <webgpu/base/RenderResourceRegistry.h>
#include <webgpu/base/raii/base_types.h>

namespace webgpu_engine::sky {

namespace {
    inline WGPUStringView sv(const char* s) { return WGPUStringView { s, WGPU_STRLEN }; }

    // The LUT renderer works in km (1 = 1km). weBIGeo's world is in meters, so scale by 1000.
    // The planet is tuned to sit underneath the (web-mercator, z-up) scene; these match the
    // reference port and are the chief correctness item to tune visually (see plan / memory).
    constexpr float FROM_KM_SCALE = 1000.0f;
} // namespace

void SkyRenderer::init(webgpu::Context& context)
{
    m_device = context.device();
    m_registry = &context.resource_registry();

    // z-up earth atmosphere; Henyey-Greenstein + Draine Mie phase (matches the fixed shader variant).
    // center is set every frame in update() from the camera position; init to origin until first update().
    m_atmosphere = params::makeEarthAtmosphere(false); // bottomRadius=6360km, height=100km
    m_atmosphere.center = { 0.0f, 0.0f, -m_atmosphere.bottomRadius };
}

void SkyRenderer::resize(uint32_t width, uint32_t height, const webgpu::raii::Texture& depth_texture, const webgpu::raii::TextureView& depth_view,
    const webgpu::raii::Texture& back_buffer_texture, const webgpu::raii::TextureView& back_buffer_view)
{
    assert(m_device != nullptr && m_registry != nullptr); // init() must have run

    WGPUTextureDescriptor render_target_desc {};
    render_target_desc.label = sv("sky render target texture");
    render_target_desc.dimension = WGPUTextureDimension_2D;
    render_target_desc.format = WGPUTextureFormat_RGBA16Float;
    render_target_desc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_StorageBinding;
    render_target_desc.mipLevelCount = 1;
    render_target_desc.sampleCount = 1;
    render_target_desc.size = { width, height, 1 };
    m_render_target = std::make_unique<webgpu::raii::Texture>(m_device, render_target_desc);
    m_render_target_view = m_render_target->create_view();

    config::SkyAtmosphereRendererConfig config;
    config.label = "sky";
    config.atmosphere = m_atmosphere;
    config.fromKilometersScale = FROM_KM_SCALE;
    config.initializeConstantLuts = true;
    config.skyRenderer.depthBuffer.texture = &depth_texture;
    config.skyRenderer.depthBuffer.view = &depth_view;
    config.skyRenderer.depthBuffer.reverseZ = true; // weBIGeo uses reverse-Z (sky depth = 0)
    config.skyRenderer.backBuffer.texture = &back_buffer_texture;
    config.skyRenderer.backBuffer.view = &back_buffer_view;
    config.skyRenderer.renderTarget.texture = m_render_target.get();
    config.skyRenderer.renderTarget.view = m_render_target_view.get();

    m_renderer = compute::SkyWithLutsComputeRenderer::create(m_device, *m_registry, config);
    m_atmosphere_dirty = false; // create() already rendered the constant LUTs
}

void SkyRenderer::update(const nucleus::camera::Definition& camera, const glm::vec3& sun_direction)
{
    m_uniforms.screenResolution = glm::vec2(camera.viewport_size());
    m_uniforms.camera.inverseProjection = glm::mat4(glm::inverse(camera.projection_matrix()));
    m_uniforms.camera.inverseView = glm::mat4(glm::inverse(camera.camera_matrix()));
    m_uniforms.camera.position = glm::vec3(camera.position());
    m_uniforms.sun.direction = glm::normalize(sun_direction);

    // Planet center follows the camera in x/y so view_height stays correct at any location.
    // z is always derived from bottomRadius (planet surface sits at z=0 in world space).
    const glm::vec3 cam_km = glm::vec3(camera.position()) / FROM_KM_SCALE;
    m_atmosphere.center = { cam_km.x, cam_km.y, -m_atmosphere.bottomRadius };
}

void SkyRenderer::render(WGPUCommandEncoder command_encoder)
{
    if (!m_renderer || !m_sky_enabled) {
        return;
    }

    m_renderer->update_uniforms(m_uniforms);

    WGPUComputePassDescriptor compute_pass_desc {};
    compute_pass_desc.label = sv("sky luts + render compute pass");
    webgpu::raii::ComputePassEncoder compute_pass(command_encoder, compute_pass_desc);

    // center is updated every frame in update(); push the full struct so the GPU sees the new center.
    m_renderer->update_atmosphere(m_atmosphere);

    if (m_atmosphere_dirty) {
        m_renderer->render_constant_luts(compute_pass.handle());
        m_atmosphere_dirty = false;
    }
    m_renderer->render_dynamic_luts(compute_pass.handle());
    m_renderer->render_sky(compute_pass.handle());
}

void SkyRenderer::mark_atmosphere_dirty() { m_atmosphere_dirty = true; }

const webgpu::raii::TextureView* SkyRenderer::result_view() const { return m_render_target_view.get(); }

const webgpu::raii::TextureView* SkyRenderer::transmittance_lut_view() const {
    return m_renderer ? &m_renderer->resources().transmittance_lut().view() : nullptr;
}
const webgpu::raii::Sampler* SkyRenderer::transmittance_lut_sampler() const {
    return m_renderer ? &m_renderer->resources().lut_sampler() : nullptr;
}
WGPUBuffer SkyRenderer::atmosphere_uniform_buffer() const {
    return m_renderer ? m_renderer->resources().atmosphere_buffer().raw_buffer().handle() : nullptr;
}

params::Atmosphere& SkyRenderer::atmosphere() { return m_atmosphere; }
const params::Atmosphere& SkyRenderer::atmosphere() const { return m_atmosphere; }
uniforms::Uniforms& SkyRenderer::uniforms() { return m_uniforms; }
const uniforms::Uniforms& SkyRenderer::uniforms() const { return m_uniforms; }

} // namespace webgpu_engine::sky
