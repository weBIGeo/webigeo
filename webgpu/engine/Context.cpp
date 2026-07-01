/*****************************************************************************
 * weBIGeo
 * Copyright (C) 2024 Adam Celarek
 * Copyright (C) 2025 Patrick Komon
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

#include "Context.h"

#include <nucleus/utils/thread.h>
#include <webgpu/base/raii/BindGroupLayout.h>

namespace {
webgpu_engine::TileSource::Config ortho_config()
{
    using nucleus::tile::TileLoadService;
    webgpu_engine::TileSource::Config config;
    config.name = "ortho";
    config.url = "https://gataki.cg.tuwien.ac.at/raw/basemap/tiles/";
    config.pattern = TileLoadService::UrlPattern::ZYX_yPointingSouth;
    config.file_ending = ".jpeg";
    config.resolution = 512;
    config.gpu_quad_limit = 256;
    config.tile_limit = 1024;
    return config;
}
} // namespace

namespace webgpu_engine {

Context::Context(QObject* parent)
    : nucleus::EngineContext(parent)
    , m_scheduler_director(std::make_unique<nucleus::tile::SchedulerDirector>())
{
#ifdef ALP_ENABLE_THREADING
    m_scheduler_thread = std::make_unique<QThread>();
    m_scheduler_thread->setObjectName("engine_scheduler_thread");
#endif
}

Context::~Context() = default;

void Context::internal_initialise()
{
    assert(m_webgpu_ctx_ptr != nullptr);

    auto& reg = webgpu_ctx().resource_registry();
    reg.set_local_shader_path("webgpu", ALP_SHADER_DIR_WEBGPU);
    reg.set_local_shader_path("webgpu_engine", ALP_SHADER_DIR_WEBGPU_ENGINE);

    reg.register_bind_group_layout("shared_config", [](WGPUDevice device) {
        WGPUBindGroupLayoutEntry entry {};
        entry.binding = 0;
        entry.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment | WGPUShaderStage_Compute;
        entry.buffer.type = WGPUBufferBindingType_Uniform;
        entry.buffer.minBindingSize = 0;
        return std::make_unique<webgpu::raii::BindGroupLayout>(device, std::vector<WGPUBindGroupLayoutEntry> { entry }, "shared config bind group layout");
    });

    reg.register_bind_group_layout("camera", [](WGPUDevice device) {
        WGPUBindGroupLayoutEntry entry {};
        entry.binding = 0;
        entry.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment | WGPUShaderStage_Compute;
        entry.buffer.type = WGPUBufferBindingType_Uniform;
        entry.buffer.minBindingSize = 0;
        return std::make_unique<webgpu::raii::BindGroupLayout>(device, std::vector<WGPUBindGroupLayoutEntry> { entry }, "camera bind group layout");
    });

    reg.register_bind_group_layout("depth_texture", [](WGPUDevice device) {
        WGPUBindGroupLayoutEntry entry {};
        entry.binding = 0;
        entry.visibility = WGPUShaderStage_Fragment | WGPUShaderStage_Compute;
        entry.texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;
        entry.texture.viewDimension = WGPUTextureViewDimension_2D;
        return std::make_unique<webgpu::raii::BindGroupLayout>(device, std::vector<WGPUBindGroupLayoutEntry> { entry }, "depth texture bind group layout");
    });

    reg.register_bind_group_layout("compose", [](WGPUDevice device) {
        WGPUBindGroupLayoutEntry albedo_entry {};
        albedo_entry.binding = 0;
        albedo_entry.visibility = WGPUShaderStage_Compute;
        albedo_entry.texture.sampleType = WGPUTextureSampleType_Uint;
        albedo_entry.texture.viewDimension = WGPUTextureViewDimension_2D;

        WGPUBindGroupLayoutEntry position_entry {};
        position_entry.binding = 1;
        position_entry.visibility = WGPUShaderStage_Compute;
        position_entry.texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;
        position_entry.texture.viewDimension = WGPUTextureViewDimension_2D;

        WGPUBindGroupLayoutEntry normal_entry {};
        normal_entry.binding = 2;
        normal_entry.visibility = WGPUShaderStage_Compute;
        normal_entry.texture.sampleType = WGPUTextureSampleType_Uint;
        normal_entry.texture.viewDimension = WGPUTextureViewDimension_2D;

        WGPUBindGroupLayoutEntry overlay_entry {};
        overlay_entry.binding = 3;
        overlay_entry.visibility = WGPUShaderStage_Compute;
        overlay_entry.texture.sampleType = WGPUTextureSampleType_Uint;
        overlay_entry.texture.viewDimension = WGPUTextureViewDimension_2D;

        WGPUBindGroupLayoutEntry shadow_texture_entry {};
        shadow_texture_entry.binding = 4;
        shadow_texture_entry.visibility = WGPUShaderStage_Compute;
        shadow_texture_entry.texture.sampleType = WGPUTextureSampleType_Float;
        shadow_texture_entry.texture.viewDimension = WGPUTextureViewDimension_2D;

        WGPUBindGroupLayoutEntry shadow_sampler_entry {};
        shadow_sampler_entry.binding = 5;
        shadow_sampler_entry.visibility = WGPUShaderStage_Compute;
        shadow_sampler_entry.sampler.type = WGPUSamplerBindingType_Filtering;

        WGPUBindGroupLayoutEntry depth_texture_entry {};
        depth_texture_entry.binding = 6;
        depth_texture_entry.visibility = WGPUShaderStage_Compute;
        depth_texture_entry.texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;
        depth_texture_entry.texture.viewDimension = WGPUTextureViewDimension_2D;

        WGPUBindGroupLayoutEntry overlay_renderer_post_entry {};
        overlay_renderer_post_entry.binding = 7;
        overlay_renderer_post_entry.visibility = WGPUShaderStage_Compute;
        overlay_renderer_post_entry.texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;
        overlay_renderer_post_entry.texture.viewDimension = WGPUTextureViewDimension_2D;

        WGPUBindGroupLayoutEntry overlay_renderer_pre_entry {};
        overlay_renderer_pre_entry.binding = 8;
        overlay_renderer_pre_entry.visibility = WGPUShaderStage_Compute;
        overlay_renderer_pre_entry.texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;
        overlay_renderer_pre_entry.texture.viewDimension = WGPUTextureViewDimension_2D;

        return std::make_unique<webgpu::raii::BindGroupLayout>(device,
            std::vector<WGPUBindGroupLayoutEntry> {
                albedo_entry,
                position_entry,
                normal_entry,
                overlay_entry,
                shadow_texture_entry,
                shadow_sampler_entry,
                depth_texture_entry,
                overlay_renderer_post_entry,
                overlay_renderer_pre_entry,
            },
            "compose bind group layout");
    });

    reg.register_bind_group_layout("compose_output", [](WGPUDevice device) {
        WGPUBindGroupLayoutEntry output_entry {};
        output_entry.binding = 0;
        output_entry.visibility = WGPUShaderStage_Compute;
        output_entry.storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
        output_entry.storageTexture.format = WGPUTextureFormat_RGBA16Float;
        output_entry.storageTexture.viewDimension = WGPUTextureViewDimension_2D;

        WGPUBindGroupLayoutEntry atm_buffer_entry {};
        atm_buffer_entry.binding = 1;
        atm_buffer_entry.visibility = WGPUShaderStage_Compute;
        atm_buffer_entry.buffer.type = WGPUBufferBindingType_Uniform;

        WGPUBindGroupLayoutEntry transmittance_lut_entry {};
        transmittance_lut_entry.binding = 2;
        transmittance_lut_entry.visibility = WGPUShaderStage_Compute;
        transmittance_lut_entry.texture.sampleType = WGPUTextureSampleType_Float;
        transmittance_lut_entry.texture.viewDimension = WGPUTextureViewDimension_2D;

        WGPUBindGroupLayoutEntry transmittance_sampler_entry {};
        transmittance_sampler_entry.binding = 3;
        transmittance_sampler_entry.visibility = WGPUShaderStage_Compute;
        transmittance_sampler_entry.sampler.type = WGPUSamplerBindingType_Filtering;

        return std::make_unique<webgpu::raii::BindGroupLayout>(
            device,
            std::vector<WGPUBindGroupLayoutEntry> { output_entry, atm_buffer_entry, transmittance_lut_entry, transmittance_sampler_entry },
            "compose output bind group layout");
    });

    reg.register_bind_group_layout("present", [](WGPUDevice device) {
        WGPUBindGroupLayoutEntry source_entry {};
        source_entry.binding = 0;
        source_entry.visibility = WGPUShaderStage_Fragment;
        source_entry.texture.sampleType = WGPUTextureSampleType_Float;
        source_entry.texture.viewDimension = WGPUTextureViewDimension_2D;
        return std::make_unique<webgpu::raii::BindGroupLayout>(device, std::vector<WGPUBindGroupLayoutEntry> { source_entry }, "present bind group layout");
    });

    reg.register_bind_group_layout("cloud_composite", [](WGPUDevice device) {
        WGPUBindGroupLayoutEntry sky_entry {};
        sky_entry.binding = 0;
        sky_entry.visibility = WGPUShaderStage_Compute;
        sky_entry.texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;
        sky_entry.texture.viewDimension = WGPUTextureViewDimension_2D;

        WGPUBindGroupLayoutEntry cloud_entry {};
        cloud_entry.binding = 1;
        cloud_entry.visibility = WGPUShaderStage_Compute;
        cloud_entry.texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;
        cloud_entry.texture.viewDimension = WGPUTextureViewDimension_2D;

        WGPUBindGroupLayoutEntry out_entry {};
        out_entry.binding = 2;
        out_entry.visibility = WGPUShaderStage_Compute;
        out_entry.storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
        out_entry.storageTexture.format = WGPUTextureFormat_RGBA16Float;
        out_entry.storageTexture.viewDimension = WGPUTextureViewDimension_2D;

        return std::make_unique<webgpu::raii::BindGroupLayout>(device,
            std::vector<WGPUBindGroupLayoutEntry> { sky_entry, cloud_entry, out_entry },
            "cloud composite bind group layout");
    });

    // Create the default imagery source (ortho) and upload its GPU array. is_alive() is still false
    // here, so add_tile_source() does not init the array itself; we do it explicitly below. The source
    // is consumed by the pre-shading SlippyTileOverlay (added app-side), not by the mesh.
    m_ortho_tile_source = add_tile_source(ortho_config());
    for (auto& source : m_tile_sources)
        source->init(webgpu_ctx());

    if (m_tile_mesh_renderer)
        m_tile_mesh_renderer->init(webgpu_ctx());
    if (m_sky_renderer)
        m_sky_renderer->init(webgpu_ctx());
    if (m_cloud_renderer)
        m_cloud_renderer->init(webgpu_ctx());
    if (m_overlay_renderer)
        m_overlay_renderer->init(*this);
    if (m_track_renderer)
        m_track_renderer->init(webgpu_ctx());
    // if (m_ortho_layer)
    //     m_ortho_layer->init();

#ifdef ALP_ENABLE_THREADING
    if (m_scheduler_thread)
        m_scheduler_thread->start();
#endif
    for (auto& source : m_tile_sources)
        source->enable();
}

void Context::internal_destroy()
{
    // Reset renderers first: the SlippyTileOverlay holds a non-owning TileSource*, so the tile sources
    // must outlive the overlay renderer.
    // this is necessary for a clean shutdown (and we want a clean shutdown for the ci integration test).
    // m_ortho_layer.reset();
    m_track_renderer.reset();
    m_overlay_renderer.reset();
    m_cloud_renderer.reset();
    m_sky_renderer.reset();
    m_tile_mesh_renderer.reset();

    // Then tear down engine-owned tile loading: reset each source's scheduler/load service on the
    // scheduler thread, then stop the thread.
    for (auto& source : m_tile_sources)
        source->teardown();
#ifdef ALP_ENABLE_THREADING
    if (m_scheduler_thread) {
        m_scheduler_thread->quit();
        m_scheduler_thread->wait(500); // msec
        m_scheduler_thread.reset();
    }
#endif
    m_ortho_tile_source = nullptr;
    m_tile_sources.clear();
}

TileSource* Context::add_tile_source(const TileSource::Config& config)
{
    auto source = std::make_shared<TileSource>(config, aabb_decorator(), m_scheduler_thread.get());
    m_scheduler_director->check_in(config.name, source->scheduler());
    if (is_alive())
        source->init(webgpu_ctx());
    m_tile_sources.push_back(source);
    return m_tile_sources.back().get();
}

TileMeshRenderer* Context::tile_mesh_renderer() const { return m_tile_mesh_renderer.get(); }

void Context::set_tile_mesh_renderer(std::shared_ptr<TileMeshRenderer> new_tile_mesh_renderer)
{
    assert(!is_alive()); // only set before init is called.
    m_tile_mesh_renderer = std::move(new_tile_mesh_renderer);
}

CloudRenderer* Context::cloud_renderer() const { return m_cloud_renderer.get(); }

void Context::set_cloud_renderer(std::shared_ptr<CloudRenderer> new_cloud_renderer)
{
    assert(!is_alive()); // only set before init is called.
    m_cloud_renderer = std::move(new_cloud_renderer);
}

sky::SkyRenderer* Context::sky_renderer() const { return m_sky_renderer.get(); }

void Context::set_sky_renderer(std::shared_ptr<sky::SkyRenderer> new_sky_renderer)
{
    assert(!is_alive()); // only set before init is called.
    m_sky_renderer = std::move(new_sky_renderer);
}

OverlayRenderer* Context::overlay_renderer() const { return m_overlay_renderer.get(); }

void Context::set_overlay_renderer(std::shared_ptr<OverlayRenderer> new_overlay_renderer)
{
    assert(!is_alive()); // only set before init is called.
    m_overlay_renderer = std::move(new_overlay_renderer);
}

TrackRenderer* Context::track_renderer() const { return m_track_renderer.get(); }

void Context::set_track_renderer(std::shared_ptr<TrackRenderer> new_track_renderer)
{
    assert(!is_alive()); // only set before init is called.
    m_track_renderer = std::move(new_track_renderer);
}

void Context::set_webgpu_ctx(webgpu::Context& ctx) { m_webgpu_ctx_ptr = &ctx; }

nucleus::track::Manager* Context::track_manager() { return nullptr; }

uboSharedConfig& Context::shared_config() { return m_shared_config; }

void Context::request_redraw() { emit redraw_requested(); }

/*TextureLayer* Context::ortho_layer() const { return m_ortho_layer.get(); }

void Context::set_ortho_layer(std::shared_ptr<TextureLayer> new_ortho_layer)
{
    assert(!is_alive()); // only set before init is called.
    m_ortho_layer = std::move(new_ortho_layer);
}*/

} // namespace webgpu_engine
