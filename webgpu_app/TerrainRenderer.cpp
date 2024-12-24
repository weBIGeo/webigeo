/*****************************************************************************
 * weBIGeo
 * Copyright (C) 2024 Patrick Komon
 * Copyright (C) 2024 Gerald Kimmersdorfer
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

#include "TerrainRenderer.h"

#include "webgpu_engine/Window.h"
#include <QFile>
#include <webgpu/webgpu_interface.hpp>

#ifdef __EMSCRIPTEN__
#include "WebInterop.h"
#include <emscripten/emscripten.h>
#else
#include "nucleus/utils/image_loader.h"
#endif

#ifdef ALP_WEBGPU_APP_ENABLE_IMGUI
#include "imgui.h"
#endif
#include "util/error_logging.h"

#include <nucleus/camera/PositionStorage.h>
#include <nucleus/timing/CpuTimer.h>

namespace webgpu_app {

TerrainRenderer::TerrainRenderer() {
#ifdef __EMSCRIPTEN__
    // execute on window resize when canvas size changes
    QObject::connect(&WebInterop::instance(), &WebInterop::body_size_changed, this, &TerrainRenderer::set_window_size);
#endif
}

void TerrainRenderer::init_window() {
    // Initializes SDL2 video subsystem
    SDL_SetMainReady();
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
        qFatal("Could not initialize SDL2 video subsystem! SDL_Error: %s", SDL_GetError());
    }

#ifdef __EMSCRIPTEN__
    // Fetch size of the webpage
    m_viewport_size = WebInterop::instance().get_body_size();
#endif
    m_sdl_window = SDL_CreateWindow("weBIGeo - Geospatial Visualization Tool", // Window title
        SDL_WINDOWPOS_CENTERED, // Window position x
        SDL_WINDOWPOS_CENTERED, // Window position y
        m_viewport_size.x, // Window width
        m_viewport_size.y, // Window height
        SDL_WINDOW_RESIZABLE); // SDL_WINDOW_VULKAN

    if (!m_sdl_window) {
        SDL_Quit();
        qFatal("Could not create SDL window! SDL_Error: %s", SDL_GetError());
    }

#ifndef __EMSCRIPTEN__
    // Load icon using the existing image loader
    auto icon = nucleus::utils::image_loader::rgba8(":/icons/logo32.png");
    // Create SDL_Surface from the raw image data
    SDL_Surface* iconSurface = SDL_CreateRGBSurfaceFrom((void*)icon.bytes(), // Pixel data
        icon.width(), // Image width
        icon.height(), // Image height
        32, // Bits per pixel (RGBA = 32 bits)
        icon.width() * 4, // Pitch (width * 4 bytes per pixel)
        0x000000ff, // Red mask
        0x0000ff00, // Green mask
        0x00ff0000, // Blue mask
        0xff000000 // Alpha mask
    );

    if (iconSurface) {
        SDL_SetWindowIcon(m_sdl_window, iconSurface); // Set the window icon
        SDL_FreeSurface(iconSurface); // Free the surface after setting the icon
    } else {
        qWarning("Could not create SDL surface for window icon. SDL_Error: %s", SDL_GetError());
    }
#endif
}

void TerrainRenderer::render_gui()
{
#ifdef ALP_WEBGPU_APP_ENABLE_IMGUI
    static bool vsync_enabled = (m_swapchain_presentmode == WGPUPresentMode::WGPUPresentMode_Fifo);
    if (ImGui::Checkbox("VSync", &vsync_enabled)) {
        m_swapchain_presentmode = vsync_enabled ? WGPUPresentMode::WGPUPresentMode_Fifo : WGPUPresentMode::WGPUPresentMode_Immediate;
        // Recreate swapchain
        m_force_repaint_once = true;
        this->on_window_resize(m_viewport_size.x, m_viewport_size.y);
    }
    ImGui::Checkbox("Repaint each frame", &m_force_repaint);
    ImGui::Text("Repaint-Counter: %d", m_repaint_count);

    if (ImGui::Button("Reload shaders [F5]", ImVec2(350, 20))) {
        m_webgpu_window->reload_shaders();
    }
#endif
}

void TerrainRenderer::poll_events()
{
    // Poll events and handle them.
    // (contrary to GLFW, close event is not automatically managed, and there
    // is no callback mechanism by default.)

    static SDL_Event events[15]; // Only allocate memory once (11 is the max events at once i witnessed)
    bool events_contain_touch = false;
    int event_count = 0;
    static SDL_Event event;
    while (SDL_PollEvent(&event)) {
        m_gui_manager->on_sdl_event(event);
        if (event.type == SDL_QUIT) {
            m_window_open = false;
        } else if (event.type == SDL_WINDOWEVENT) {
            if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
                on_window_resize(event.window.data1, event.window.data2);
            }
        } else {
            events[event_count++] = event;
            if (event.type == SDL_FINGERDOWN || event.type == SDL_FINGERUP || event.type == SDL_FINGERMOTION) {
                events_contain_touch = true;
            }
        }
    }

    // IMPORTANT: SDL seems to emulate touch events as mouse events aswell. In order to avoid this
    // we need to filter out the mouse events if there are touch events. Meaning we priortize touch over mouse.
    for (int i = 0; i < event_count; i++) {
        if (events_contain_touch && (events[i].type == SDL_MOUSEMOTION || events[i].type == SDL_MOUSEBUTTONDOWN || events[i].type == SDL_MOUSEBUTTONUP)) {
            continue;
        }
        m_input_mapper->on_sdl_event(events[i]);
    }
}

void TerrainRenderer::render() {
    // Do nothing, this checks for ongoing asynchronous operations and call their callbacks
    m_cputimer->start();

    WGPUTextureView swapchain_texture = wgpuSwapChainGetCurrentTextureView(m_swapchain);
    if (!swapchain_texture) {
        qFatal("Cannot acquire next swap chain texture");
    }

    WGPUCommandEncoderDescriptor command_encoder_desc {};
    command_encoder_desc.label = "Command Encoder";
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(m_device, &command_encoder_desc);

    if (webgpu::isTimingSupported())
        m_gputimer->start(encoder);

    m_frame_count++;
    if (m_webgpu_window->needs_redraw() || m_force_repaint || m_force_repaint_once) {
        m_webgpu_window->paint(m_framebuffer.get(), encoder);
        m_repaint_count++;
        m_force_repaint_once = false;
    }

    {
        webgpu::raii::RenderPassEncoder render_pass(encoder, swapchain_texture, nullptr);
        wgpuRenderPassEncoderSetPipeline(render_pass.handle(), m_gui_pipeline.get()->pipeline().handle());
        wgpuRenderPassEncoderSetBindGroup(render_pass.handle(), 0, m_gui_bind_group->handle(), 0, nullptr);
        wgpuRenderPassEncoderDraw(render_pass.handle(), 3, 1, 0, 0);

#ifdef ALP_WEBGPU_APP_ENABLE_IMGUI
        // We add the GUI drawing commands to the render pass
        m_gui_manager->render(render_pass.handle());
#endif
    }

    if (webgpu::isTimingSupported())
        m_gputimer->stop(encoder);

    wgpuTextureViewRelease(swapchain_texture);

    WGPUCommandBufferDescriptor cmd_buffer_descriptor {};
    cmd_buffer_descriptor.label = "Command buffer";
    WGPUCommandBuffer command = wgpuCommandEncoderFinish(encoder, &cmd_buffer_descriptor);
    wgpuCommandEncoderRelease(encoder);
    wgpuQueueSubmit(m_queue, 1, &command);
    wgpuCommandBufferRelease(command);

    if (webgpu::isTimingSupported())
        m_gputimer->resolve();

#ifndef __EMSCRIPTEN__
    // Swapchain in the WEB is handled by the browser!
    wgpuSwapChainPresent(m_swapchain);
    wgpuInstanceProcessEvents(m_instance);
    wgpuDeviceTick(m_device);
#endif

    m_cputimer->stop();
}

void TerrainRenderer::start() {
    init_window();

    webgpu::platformInit();

    webgpu_create_context();

    // TODO: THIS TAKES FOREVER ON FIRST LOAD. LETS CHECK OUT WHY!
    m_controller = std::make_unique<nucleus::Controller>(m_webgpu_window.get());

#ifdef ALP_WEBGPU_APP_ENABLE_IMGUI
    m_gui_manager = std::make_unique<GuiManager>(this);
#endif

    nucleus::camera::Controller* camera_controller = m_controller->camera_controller();
    m_input_mapper = std::make_unique<InputMapper>(this, camera_controller, m_gui_manager.get(), [this]() { return m_viewport_size; });

    connect(this, &TerrainRenderer::update_camera_requested, camera_controller, &nucleus::camera::Controller::update_camera_request);
    connect(m_webgpu_window.get(), &webgpu_engine::Window::set_camera_definition_requested, camera_controller, &nucleus::camera::Controller::set_definition);

#ifdef __EMSCRIPTEN__
    // connect(&WebInterop::instance(), &WebInterop::mouse_button_event, m_input_mapper.get(), &InputMapper::on_mouse_button_callback);
    // connect(&WebInterop::instance(), &WebInterop::mouse_position_event, m_input_mapper.get(), &InputMapper::on_cursor_position_callback);
#endif

    connect(m_input_mapper.get(), &InputMapper::key_pressed, this, &TerrainRenderer::handle_shortcuts);

    m_webgpu_window->set_wgpu_context(m_instance, m_device, m_adapter, m_surface, m_queue);
    m_webgpu_window->initialise_gpu();

    // Creates the swapchain
    this->on_window_resize(m_viewport_size.x, m_viewport_size.y);

    { // load first camera definition without changing preset in nucleus
        auto new_definition = nucleus::camera::stored_positions::heiligenblut_popping();
        new_definition.set_viewport_size(m_viewport_size);
        camera_controller->set_definition(new_definition);
    }

    qDebug() << "Create GUI Pipeline...";
    m_gui_ubo
        = std::make_unique<webgpu::raii::RawBuffer<TerrainRenderer::GuiPipelineUBO>>(m_device, WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst, 1, "gui ubo");
    m_gui_ubo->write(m_queue, &m_gui_ubo_data);

    webgpu::FramebufferFormat format {};
    format.color_formats.emplace_back(m_swapchain_format);

    WGPUBindGroupLayoutEntry backbuffer_texture_entry {};
    backbuffer_texture_entry.binding = 0;
    backbuffer_texture_entry.visibility = WGPUShaderStage_Fragment;
    backbuffer_texture_entry.texture.sampleType = WGPUTextureSampleType_Float;
    backbuffer_texture_entry.texture.viewDimension = WGPUTextureViewDimension_2D;

    WGPUBindGroupLayoutEntry gui_ubo_entry = {};
    gui_ubo_entry.binding = 1;
    gui_ubo_entry.visibility = WGPUShaderStage_Fragment;
    gui_ubo_entry.buffer.type = WGPUBufferBindingType_Uniform;
    gui_ubo_entry.buffer.minBindingSize = sizeof(TerrainRenderer::GuiPipelineUBO);

    m_gui_bind_group_layout = std::make_unique<webgpu::raii::BindGroupLayout>(
        m_device, std::vector<WGPUBindGroupLayoutEntry> { backbuffer_texture_entry, gui_ubo_entry }, "gui bind group layout");

    const std::string preprocessed_code = R"(
    @group(0) @binding(0) var backbuffer_texture : texture_2d<f32>;
    @group(0) @binding(1) var<uniform> gui_ubo : vec2f;

    struct VertexOut {
        @builtin(position) position : vec4f,
        @location(0) texcoords : vec2f
    }

    @vertex
    fn vertexMain(@builtin(vertex_index) vertex_index : u32) -> VertexOut {
        const VERTICES = array(vec2f(-1.0, -1.0), vec2f(3.0, -1.0), vec2f(-1.0, 3.0));
        var vertex_out : VertexOut;
        vertex_out.position = vec4(VERTICES[vertex_index], 0.0, 1.0);
        vertex_out.texcoords = vec2(0.5, -0.5) * vertex_out.position.xy + vec2(0.5);
        return vertex_out;
    }

    @fragment
    fn fragmentMain(vertex_out : VertexOut) -> @location(0) vec4f {
        let tci : vec2<u32> = vec2u(vertex_out.texcoords * gui_ubo);
        var backbuffer_color = textureLoad(backbuffer_texture, tci, 0);
        return backbuffer_color;
    }
    )";

    WGPUShaderModuleDescriptor shader_module_desc {};
    WGPUShaderModuleWGSLDescriptor wgsl_desc {};
    wgsl_desc.chain.next = nullptr;
    wgsl_desc.chain.sType = WGPUSType::WGPUSType_ShaderModuleWGSLDescriptor;
    wgsl_desc.code = preprocessed_code.data();
    shader_module_desc.label = "Gui Shader Module";
    shader_module_desc.nextInChain = &wgsl_desc.chain;
    auto shader_module = std::make_unique<webgpu::raii::ShaderModule>(m_device, shader_module_desc);

    m_gui_pipeline = std::make_unique<webgpu::raii::GenericRenderPipeline>(m_device, *shader_module, *shader_module,
        std::vector<webgpu::util::SingleVertexBufferInfo> {}, format, std::vector<const webgpu::raii::BindGroupLayout*> { m_gui_bind_group_layout.get() });

    m_gui_bind_group = std::make_unique<webgpu::raii::BindGroup>(m_device, *m_gui_bind_group_layout.get(),
        std::initializer_list<WGPUBindGroupEntry> { m_framebuffer->color_texture_view(0).create_bind_group_entry(0), m_gui_ubo->create_bind_group_entry(1) });

    m_timer_manager = std::make_unique<webgpu::timing::GuiTimerManager>();
#ifdef ALP_WEBGPU_APP_ENABLE_IMGUI
    m_gui_manager->init(m_sdl_window, m_device, m_swapchain_format, WGPUTextureFormat_Undefined);
#endif

    m_cputimer = std::make_shared<webgpu::timing::CpuTimer>(120);
    m_timer_manager->add_timer(m_cputimer, "CPU Timer", "Renderer");
    if (webgpu::isTimingSupported()) {
        m_gputimer = std::make_shared<webgpu::timing::WebGpuTimer>(m_device, 3, 120);
        m_timer_manager->add_timer(m_gputimer, "GPU Timer", "Renderer");
    }

    this->on_window_resize(m_viewport_size.x, m_viewport_size.y);
    m_initialized = true;

#if defined(__EMSCRIPTEN__)
    emscripten_set_main_loop_arg(
        [](void* userData) {
            TerrainRenderer& renderer = *reinterpret_cast<TerrainRenderer*>(userData);
            renderer.poll_events();
            renderer.render();
        },
        (void*)this, 0, true);
#else
    while (m_window_open) {
        poll_events();
        render();
    }
#endif

    // NOTE: Ressources are freed by the browser when the page is closed. Also keep in mind
    // that this part of code will be executed immediately since the main loop is not blocking.
#ifndef __EMSCRIPTEN__
#ifdef ALP_WEBGPU_APP_ENABLE_IMGUI
    m_gui_manager->shutdown();
#endif
    webgpu_release_context();
    m_webgpu_window->destroy();

    SDL_DestroyWindow(m_sdl_window);
    SDL_Quit();
    m_initialized = false;
#endif
}

void TerrainRenderer::set_window_size(glm::uvec2 size)
{
    if (m_viewport_size == size)
        return;
    m_viewport_size = size;
    if (m_initialized) {
        SDL_SetWindowSize(m_sdl_window, size.x, size.y);
        on_window_resize(size.x, size.y);
    }
}

void TerrainRenderer::handle_shortcuts(QKeyCombination key)
{
    if (key.key() == Qt::Key_F5) {
        m_webgpu_window->reload_shaders();
    }
}

void TerrainRenderer::create_framebuffer(uint32_t width, uint32_t height)
{
    qDebug() << "creating framebuffer textures for size " << width << "x" << height;

    webgpu::FramebufferFormat format {
        .size = { width, height },
        .depth_format = m_depth_texture_format,
        .color_formats = { m_swapchain_format },
        .usages = { WGPUTextureUsage_TextureBinding | WGPUTextureUsage_StorageBinding },
    };
    m_framebuffer = std::make_unique<webgpu::Framebuffer>(m_device, format);

    if (m_gui_bind_group) {
        m_gui_bind_group = std::make_unique<webgpu::raii::BindGroup>(m_device, *m_gui_bind_group_layout.get(),
            std::initializer_list<WGPUBindGroupEntry> {
                m_framebuffer->color_texture_view(0).create_bind_group_entry(0), m_gui_ubo->create_bind_group_entry(1) });
    }

    if (m_gui_ubo) {
        m_gui_ubo_data.resolution = glm::vec2(m_viewport_size);
        m_gui_ubo->write(m_queue, &m_gui_ubo_data);
    }
}

void TerrainRenderer::create_swapchain(uint32_t width, uint32_t height)
{
    qDebug() << "creating swapchain device...";

    // from Learn WebGPU C++ tutorial
#ifdef WEBGPU_BACKEND_WGPU
    m_swapchain_format = surface.getPreferredFormat(m_adapter);
#else
    m_swapchain_format = WGPUTextureFormat::WGPUTextureFormat_BGRA8Unorm;
#endif
    WGPUSwapChainDescriptor swapchain_desc = {};
    swapchain_desc.width = width;
    swapchain_desc.height = height;
    swapchain_desc.usage = WGPUTextureUsage_RenderAttachment;
    swapchain_desc.format = m_swapchain_format;
    swapchain_desc.presentMode = m_swapchain_presentmode;
    m_swapchain = wgpuDeviceCreateSwapChain(m_device, m_surface, &swapchain_desc);
    qInfo() << "Got swapchain: " << m_swapchain;
}

void TerrainRenderer::on_window_resize(int width, int height) {
    m_viewport_size = { width, height };
    // TODO check if we can do it without completely recreating swapchain
    if (m_swapchain != nullptr) {
        wgpuSwapChainRelease(m_swapchain);
    }

    create_swapchain(width, height);
    create_framebuffer(width, height);

    m_webgpu_window->resize_framebuffer(m_viewport_size.x, m_viewport_size.y);
    m_controller->camera_controller()->set_viewport(m_viewport_size);
}

void TerrainRenderer::webgpu_create_context()
{
    qDebug() << "Creating WebGPU instance...";
#ifndef __EMSCRIPTEN__
    m_instance_desc = {};
    m_instance_desc.nextInChain = nullptr;
    WGPUDawnTogglesDescriptor dawnToggles;
    dawnToggles.chain.next = nullptr;
    dawnToggles.chain.sType = WGPUSType_DawnTogglesDescriptor;

    std::vector<const char*> enabledToggles = {
        "allow_unsafe_apis",
    };
    dawnToggles.enabledToggles = enabledToggles.data();
    dawnToggles.enabledToggleCount = enabledToggles.size();
    dawnToggles.disabledToggleCount = 0;

    m_instance_desc.nextInChain = &dawnToggles.chain;
    m_instance = wgpuCreateInstance(&m_instance_desc);
#else
    m_instance = wgpuCreateInstance(nullptr);
#endif

    if (!m_instance) {
        qFatal("Could not initialize WebGPU Instance!");
    }
    qInfo() << "Got instance: " << m_instance;

    qDebug() << "Requesting surface...";
    m_surface = SDL_GetWGPUSurface(m_instance, m_sdl_window);
    if (!m_surface) {
        qFatal("Could not create surface!");
    }
    qInfo() << "Got surface: " << m_surface;

    qDebug() << "Requesting adapter...";
    WGPURequestAdapterOptions adapter_opts {};
    adapter_opts.powerPreference = WGPUPowerPreference_HighPerformance;
    adapter_opts.compatibleSurface = m_surface;
    m_adapter = webgpu::requestAdapterSync(m_instance, adapter_opts);
    if (!m_adapter) {
        qFatal("Could not get adapter!");
    }
    qInfo() << "Got adapter: " << m_adapter;

    m_webgpu_window = std::make_unique<webgpu_engine::Window>();

    qDebug() << "Requesting device...";
    WGPURequiredLimits required_limits {};
    WGPUSupportedLimits supported_limits {};
    wgpuAdapterGetLimits(m_adapter, &supported_limits);

    // irrelevant for us, but needs to be set
    required_limits.limits.minStorageBufferOffsetAlignment = supported_limits.limits.minStorageBufferOffsetAlignment;
    required_limits.limits.minUniformBufferOffsetAlignment = supported_limits.limits.minUniformBufferOffsetAlignment;

    // Let the engine change the required limits
    m_webgpu_window->update_required_gpu_limits(required_limits.limits, supported_limits.limits);

    std::vector<WGPUFeatureName> requiredFeatures;
    requiredFeatures.push_back(WGPUFeatureName_TimestampQuery);
    requiredFeatures.push_back(WGPUFeatureName_BGRA8UnormStorage);

    WGPUDeviceDescriptor device_desc {};
    device_desc.label = "webigeo device";
    device_desc.requiredFeatures = requiredFeatures.data();
    device_desc.requiredFeatureCount = (uint32_t)requiredFeatures.size();
    device_desc.requiredLimits = &required_limits;
    device_desc.defaultQueue.label = "webigeo queue";
    m_device = webgpu::requestDeviceSync(m_adapter, device_desc);
    if (!m_device) {
        qFatal("Could not get device!");
    }
    qInfo() << "Got device: " << m_device;

    webgpu::checkForTimingSupport(m_adapter, m_device);

    // Set error callback
    wgpuDeviceSetUncapturedErrorCallback(m_device, webgpu_device_error_callback, nullptr /* pUserData */);

    qDebug() << "Requesting queue...";
    m_queue = wgpuDeviceGetQueue(m_device);
    if (!m_queue) {
        qFatal("Could not get queue!");
    }
    qInfo() << "Got queue: " << m_queue;
}

void TerrainRenderer::webgpu_release_context()
{
    qDebug() << "Releasing WebGPU context...";
    // Set the device lost callback to null otherwise we'll get a warning
#ifndef __EMSCRIPTEN__
    wgpuDeviceSetDeviceLostCallback(m_device, nullptr, nullptr);
#endif
    wgpuSwapChainRelease(m_swapchain);
    wgpuQueueRelease(m_queue);
    wgpuSurfaceRelease(m_surface);
    wgpuDeviceRelease(m_device);
    wgpuAdapterRelease(m_adapter);
    wgpuInstanceRelease(m_instance);
}

} // namespace webgpu_app
