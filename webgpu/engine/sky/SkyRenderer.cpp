/*****************************************************************************
 * weBIGeo
 * Copyright (C) 2026 Gerald Kimmersdorfer
 * Copyright (C) 2025 Patrick Komon
 * Copyright (C) 2024 Lukas Herzberger
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
#include <glm/common.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <nucleus/srs.h>
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

    // Auto mode crossfades linearly between Lut (below) and Hybrid (above) across this altitude band.
    constexpr float AUTO_BLEND_ALTITUDE_MIN_M = 10000.0f;
    constexpr float AUTO_BLEND_ALTITUDE_MAX_M = 20000.0f;

    std::unique_ptr<webgpu::raii::Texture> make_sky_target_texture(WGPUDevice device, uint32_t width, uint32_t height, const char* label)
    {
        WGPUTextureDescriptor desc {};
        desc.label = WGPUStringView { label, WGPU_STRLEN };
        desc.dimension = WGPUTextureDimension_2D;
        desc.format = WGPUTextureFormat_RGBA16Float;
        desc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_StorageBinding;
        desc.mipLevelCount = 1;
        desc.sampleCount = 1;
        desc.size = { width, height, 1 };
        return std::make_unique<webgpu::raii::Texture>(device, desc);
    }
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

    m_depth_texture = &depth_texture;
    m_depth_view = &depth_view;
    m_back_buffer_texture = &back_buffer_texture;
    m_back_buffer_view = &back_buffer_view;
    m_width = width;
    m_height = height;

    m_render_target = make_sky_target_texture(m_device, width, height, "sky render target texture");
    m_render_target_view = m_render_target->create_view();

    rebuild_renderer();
}

void SkyRenderer::set_mode(Mode mode)
{
    if (m_mode == mode) {
        return;
    }
    m_mode = mode;
    if (m_render_target) { // only rebuild once resize() has already run at least once
        rebuild_renderer();
    }
}

void SkyRenderer::rebuild_renderer()
{
    if (m_mode == Mode::Auto) {
        m_renderer.reset();
        rebuild_auto_renderers();
    } else {
        // Leaving Auto (or first build in a non-Auto mode): drop its scratch targets/renderers/bind group.
        m_auto_hybrid_renderer.reset();
        m_auto_lut_renderer.reset();
        m_auto_hybrid_target_view.reset();
        m_auto_hybrid_target.reset();
        m_auto_lut_target_view.reset();
        m_auto_lut_target.reset();
        m_blend_bind_group.reset();

        compute::SkyPassVariant variant = compute::SkyPassVariant::LutOnly;
        if (m_mode == Mode::RayMarch) {
            variant = compute::SkyPassVariant::PureRayMarch;
        } else if (m_mode == Mode::Hybrid) {
            variant = compute::SkyPassVariant::HybridRayMarch;
        }
        rebuild_single_renderer(variant);
    }
    m_resource_generation++;
}

void SkyRenderer::rebuild_single_renderer(compute::SkyPassVariant variant)
{
    config::SkyAtmosphereRendererConfig config;
    config.label = "sky";
    config.atmosphere = m_atmosphere;
    config.fromKilometersScale = FROM_KM_SCALE;
    config.initializeConstantLuts = true;
    config.skyRenderer.defaultToPerPixelRayMarch = variant != compute::SkyPassVariant::LutOnly;
    config.skyRenderer.rayMarch.rayMarchDistantSky = variant == compute::SkyPassVariant::PureRayMarch;
    config.skyRenderer.depthBuffer.texture = m_depth_texture;
    config.skyRenderer.depthBuffer.view = m_depth_view;
    config.skyRenderer.depthBuffer.reverseZ = true; // weBIGeo uses reverse-Z (sky depth = 0)
    config.skyRenderer.backBuffer.texture = m_back_buffer_texture;
    config.skyRenderer.backBuffer.view = m_back_buffer_view;
    config.skyRenderer.renderTarget.texture = m_render_target.get();
    config.skyRenderer.renderTarget.view = m_render_target_view.get();

    m_renderer = compute::SkyWithLutsComputeRenderer::create(m_device, *m_registry, config);
    m_atmosphere_dirty = false; // create() already rendered the constant LUTs
}

void SkyRenderer::rebuild_auto_renderers()
{
    // Freshly created renderers always start out on their own scratch target (matching hybrid_config /
    // lut_config below); render_auto() rebinds them to m_render_target itself as needed.
    m_auto_hybrid_targets_render_target = false;
    m_auto_lut_targets_render_target = false;

    m_auto_hybrid_target = make_sky_target_texture(m_device, m_width, m_height, "sky auto hybrid target texture");
    m_auto_hybrid_target_view = m_auto_hybrid_target->create_view();
    m_auto_lut_target = make_sky_target_texture(m_device, m_width, m_height, "sky auto lut target texture");
    m_auto_lut_target_view = m_auto_lut_target->create_view();

    config::SkyAtmosphereRendererConfig hybrid_config;
    hybrid_config.label = "sky auto hybrid";
    hybrid_config.atmosphere = m_atmosphere;
    hybrid_config.fromKilometersScale = FROM_KM_SCALE;
    hybrid_config.initializeConstantLuts = true;
    hybrid_config.skyRenderer.defaultToPerPixelRayMarch = true;
    hybrid_config.skyRenderer.rayMarch.rayMarchDistantSky = false;
    hybrid_config.skyRenderer.depthBuffer.texture = m_depth_texture;
    hybrid_config.skyRenderer.depthBuffer.view = m_depth_view;
    hybrid_config.skyRenderer.depthBuffer.reverseZ = true;
    hybrid_config.skyRenderer.backBuffer.texture = m_back_buffer_texture;
    hybrid_config.skyRenderer.backBuffer.view = m_back_buffer_view;
    hybrid_config.skyRenderer.renderTarget.texture = m_auto_hybrid_target.get();
    hybrid_config.skyRenderer.renderTarget.view = m_auto_hybrid_target_view.get();
    m_auto_hybrid_renderer = compute::SkyWithLutsComputeRenderer::create(m_device, *m_registry, hybrid_config);

    config::SkyAtmosphereRendererConfig lut_config = hybrid_config;
    lut_config.label = "sky auto lut";
    lut_config.skyRenderer.defaultToPerPixelRayMarch = false;
    lut_config.skyRenderer.renderTarget.texture = m_auto_lut_target.get();
    lut_config.skyRenderer.renderTarget.view = m_auto_lut_target_view.get();
    m_auto_lut_renderer = compute::SkyWithLutsComputeRenderer::create(m_device, *m_registry, lut_config);

    m_atmosphere_dirty = false;

    rebuild_blend_pipeline();
}

void SkyRenderer::rebuild_blend_pipeline()
{
    if (!m_blend_factor_buffer) {
        m_blend_factor_buffer = std::make_unique<webgpu::Buffer<float>>(m_device, WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst);
    }

    if (!m_blend_pipeline) {
        WGPUBindGroupLayoutEntry factor_entry {};
        factor_entry.binding = 0;
        factor_entry.visibility = WGPUShaderStage_Compute;
        factor_entry.buffer.type = WGPUBufferBindingType_Uniform;

        WGPUBindGroupLayoutEntry tex_a_entry {};
        tex_a_entry.binding = 1;
        tex_a_entry.visibility = WGPUShaderStage_Compute;
        tex_a_entry.texture.sampleType = WGPUTextureSampleType_Float;
        tex_a_entry.texture.viewDimension = WGPUTextureViewDimension_2D;

        WGPUBindGroupLayoutEntry tex_b_entry = tex_a_entry;
        tex_b_entry.binding = 2;

        WGPUBindGroupLayoutEntry out_entry {};
        out_entry.binding = 3;
        out_entry.visibility = WGPUShaderStage_Compute;
        out_entry.storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
        out_entry.storageTexture.format = WGPUTextureFormat_RGBA16Float;
        out_entry.storageTexture.viewDimension = WGPUTextureViewDimension_2D;

        m_blend_bind_group_layout = std::make_unique<webgpu::raii::BindGroupLayout>(m_device,
            std::vector<WGPUBindGroupLayoutEntry> { factor_entry, tex_a_entry, tex_b_entry, out_entry }, "sky auto blend bind group layout");
        m_blend_pipeline_layout = std::make_unique<webgpu::raii::PipelineLayout>(
            m_device, std::vector<WGPUBindGroupLayout> { m_blend_bind_group_layout->handle() }, "sky auto blend pipeline layout");

        m_registry->register_shader("sky_blend_variants", "webgpu_engine::sky/common/blend_variants");
        WGPUComputePipelineDescriptor pipeline_desc {};
        pipeline_desc.label = sv("sky auto blend pipeline");
        pipeline_desc.layout = m_blend_pipeline_layout->handle();
        pipeline_desc.compute = {};
        pipeline_desc.compute.entryPoint = sv("main");
        pipeline_desc.compute.module = m_registry->shader("sky_blend_variants").handle();
        m_blend_pipeline = std::make_unique<webgpu::raii::ComputePipeline>(m_device, pipeline_desc);
    }

    // tex_a = lut (factor 0 -> pure lut), tex_b = hybrid (factor 1 -> pure hybrid).
    m_blend_bind_group = std::make_unique<webgpu::raii::BindGroup>(m_device, m_blend_bind_group_layout->handle(),
        std::vector<WGPUBindGroupEntry> {
            m_blend_factor_buffer->raw_buffer().create_bind_group_entry(0),
            m_auto_lut_target_view->create_bind_group_entry(1),
            m_auto_hybrid_target_view->create_bind_group_entry(2),
            m_render_target_view->create_bind_group_entry(3),
        },
        "sky auto blend bind group");
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

    // Raw world-space z is mercator-scaled by 1/cos(latitude), not true altitude - convert properly
    // (same conversion the date/time panel uses to show the camera's altitude to the user).
    const double true_altitude_m = nucleus::srs::world_to_lat_long_alt(camera.position()).z;
    m_camera_altitude_m = std::max(0.0f, float(true_altitude_m));
}

void SkyRenderer::render(WGPUCommandEncoder command_encoder)
{
    if (!m_sky_enabled) {
        return;
    }

    if (m_mode == Mode::Auto) {
        render_auto(command_encoder);
    } else {
        render_single(command_encoder);
    }

    m_atmosphere_dirty = false;
}

void SkyRenderer::render_single(WGPUCommandEncoder command_encoder)
{
    if (!m_renderer) {
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
    }
    m_renderer->render_dynamic_luts(compute_pass.handle());
    m_renderer->render_sky(compute_pass.handle());
}

void SkyRenderer::render_auto(WGPUCommandEncoder command_encoder)
{
    if (!m_auto_hybrid_renderer || !m_auto_lut_renderer || !m_blend_pipeline) {
        return;
    }

    const float t = auto_blend_factor();
    const bool need_hybrid = t > 0.0f;
    const bool need_lut = t < 1.0f;
    const bool single_side_active = need_hybrid != need_lut; // outside the blend band

    // Outside the band, rebind the sole active side to write straight into m_render_target - no
    // scratch target, no blend/copy pass at all. Inside it, make sure both are back on their own
    // scratch targets so the blend pass below has two separate inputs to read.
    ensure_auto_renderer_target(*m_auto_lut_renderer, false, single_side_active && need_lut, m_auto_lut_targets_render_target);
    ensure_auto_renderer_target(*m_auto_hybrid_renderer, true, single_side_active && need_hybrid, m_auto_hybrid_targets_render_target);

    // Lut is lut_source()'s stable pick (see there), so its uniforms/atmosphere/dynamic LUTs stay
    // current every frame regardless of blend weight; hybrid only needs them while actually visible.
    m_auto_lut_renderer->update_uniforms(m_uniforms);
    m_auto_lut_renderer->update_atmosphere(m_atmosphere);
    if (need_hybrid) {
        m_auto_hybrid_renderer->update_uniforms(m_uniforms);
        m_auto_hybrid_renderer->update_atmosphere(m_atmosphere);
    }

    WGPUComputePassDescriptor compute_pass_desc {};
    compute_pass_desc.label = sv("sky auto luts + render compute pass");
    webgpu::raii::ComputePassEncoder compute_pass(command_encoder, compute_pass_desc);

    if (m_atmosphere_dirty) {
        m_auto_hybrid_renderer->render_constant_luts(compute_pass.handle());
        m_auto_lut_renderer->render_constant_luts(compute_pass.handle());
    }

    m_auto_lut_renderer->render_dynamic_luts(compute_pass.handle());
    if (need_lut) {
        m_auto_lut_renderer->render_sky(compute_pass.handle());
    }
    // Skip hybrid's expensive dynamic LUTs + full-screen ray march entirely when it isn't blended in.
    if (need_hybrid) {
        m_auto_hybrid_renderer->render_dynamic_luts(compute_pass.handle());
        m_auto_hybrid_renderer->render_sky(compute_pass.handle());
    }

    // Only actually crossfading inside the band; outside it the active side's render_sky() above
    // already wrote m_render_target directly, so there's nothing left to do.
    if (!single_side_active) {
        m_blend_factor_buffer->data = t;
        m_blend_factor_buffer->update_gpu_data(wgpuDeviceGetQueue(m_device));

        wgpuComputePassEncoderSetPipeline(compute_pass.handle(), m_blend_pipeline->handle());
        wgpuComputePassEncoderSetBindGroup(compute_pass.handle(), 0, m_blend_bind_group->handle(), 0, nullptr);
        wgpuComputePassEncoderDispatchWorkgroups(compute_pass.handle(), (m_width + 7) / 8, (m_height + 7) / 8, 1);
    }
}

config::SkyRendererComputeConfig SkyRenderer::make_auto_compute_config(bool is_hybrid, bool direct) const
{
    config::SkyRendererComputeConfig compute_config;
    compute_config.defaultToPerPixelRayMarch = is_hybrid;
    compute_config.rayMarch.rayMarchDistantSky = false;
    compute_config.depthBuffer.texture = m_depth_texture;
    compute_config.depthBuffer.view = m_depth_view;
    compute_config.depthBuffer.reverseZ = true;
    compute_config.backBuffer.texture = m_back_buffer_texture;
    compute_config.backBuffer.view = m_back_buffer_view;
    if (direct) {
        compute_config.renderTarget.texture = m_render_target.get();
        compute_config.renderTarget.view = m_render_target_view.get();
    } else if (is_hybrid) {
        compute_config.renderTarget.texture = m_auto_hybrid_target.get();
        compute_config.renderTarget.view = m_auto_hybrid_target_view.get();
    } else {
        compute_config.renderTarget.texture = m_auto_lut_target.get();
        compute_config.renderTarget.view = m_auto_lut_target_view.get();
    }
    return compute_config;
}

void SkyRenderer::ensure_auto_renderer_target(compute::SkyWithLutsComputeRenderer& renderer, bool is_hybrid, bool direct, bool& current_is_direct)
{
    if (direct == current_is_direct) {
        return;
    }
    renderer.rebind_output(make_auto_compute_config(is_hybrid, direct));
    current_is_direct = direct;
}

float SkyRenderer::auto_blend_factor() const
{
    return glm::clamp((m_camera_altitude_m - AUTO_BLEND_ALTITUDE_MIN_M) / (AUTO_BLEND_ALTITUDE_MAX_M - AUTO_BLEND_ALTITUDE_MIN_M), 0.0f, 1.0f);
}

void SkyRenderer::mark_atmosphere_dirty() { m_atmosphere_dirty = true; }

const compute::SkyWithLutsComputeRenderer* SkyRenderer::lut_source() const
{
    if (m_renderer) {
        return m_renderer.get();
    }
    // Always the lut renderer, regardless of blend weight: Window.cpp caches bind groups referencing
    // this renderer's buffers/views across frames (rebuilt only on resource_generation() changes), so
    // the choice must stay stable rather than track the per-frame altitude. render_auto() keeps the
    // lut renderer's uniforms/atmosphere/dynamic LUTs current every frame for exactly this reason.
    return m_auto_lut_renderer ? m_auto_lut_renderer.get() : m_auto_hybrid_renderer.get();
}

const webgpu::raii::TextureView* SkyRenderer::result_view() const { return m_render_target_view.get(); }

const webgpu::raii::TextureView* SkyRenderer::transmittance_lut_view() const
{
    const auto* src = lut_source();
    return src ? &src->resources().transmittance_lut().view() : nullptr;
}
const webgpu::raii::Sampler* SkyRenderer::transmittance_lut_sampler() const
{
    const auto* src = lut_source();
    return src ? &src->resources().lut_sampler() : nullptr;
}
WGPUBuffer SkyRenderer::atmosphere_uniform_buffer() const
{
    const auto* src = lut_source();
    return src ? src->resources().atmosphere_buffer().raw_buffer().handle() : nullptr;
}
const webgpu::raii::TextureView* SkyRenderer::aerial_perspective_lut_view() const
{
    const auto* src = lut_source();
    return src ? &src->resources().aerial_perspective_lut().view() : nullptr;
}
const webgpu::raii::TextureView* SkyRenderer::sky_view_lut_view() const
{
    const auto* src = lut_source();
    return src ? &src->resources().sky_view_lut().view() : nullptr;
}

params::Atmosphere& SkyRenderer::atmosphere() { return m_atmosphere; }
const params::Atmosphere& SkyRenderer::atmosphere() const { return m_atmosphere; }
uniforms::Uniforms& SkyRenderer::uniforms() { return m_uniforms; }
const uniforms::Uniforms& SkyRenderer::uniforms() const { return m_uniforms; }

} // namespace webgpu_engine::sky
