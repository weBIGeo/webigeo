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
#include "nucleus/stb/stb_image_loader.h"
#endif

#ifdef ALP_WEBGPU_APP_ENABLE_IMGUI
#include "imgui.h"
#endif
#include "util/error_logging.h"

#include <nucleus/timing/CpuTimer.h>

namespace webgpu_app {

static void windowResizeCallback(GLFWwindow* window, int width, int height) {
    auto terrainRenderer = static_cast<TerrainRenderer*>(glfwGetWindowUserPointer(window));
    terrainRenderer->on_window_resize(width, height);
}

static void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    auto renderer = static_cast<TerrainRenderer*>(glfwGetWindowUserPointer(window));
    if (renderer->get_gui_manager()->want_capture_keyboard())
        return;
    renderer->get_input_mapper()->on_key_callback(key, scancode, action, mods);
}

static void cursor_position_callback(GLFWwindow* window, double xpos, double ypos) {
    auto renderer = static_cast<TerrainRenderer*>(glfwGetWindowUserPointer(window));
    if (renderer->get_gui_manager()->want_capture_mouse())
        return;
    renderer->get_input_mapper()->on_cursor_position_callback(xpos, ypos);
}

static void mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
    auto renderer = static_cast<TerrainRenderer*>(glfwGetWindowUserPointer(window));
    if (renderer->get_gui_manager()->want_capture_mouse())
        return;
    double xpos, ypos;
    glfwGetCursorPos(window, &xpos, &ypos);
    renderer->get_input_mapper()->on_mouse_button_callback(button, action, mods, xpos, ypos);
}

static void scroll_callback(GLFWwindow* window, double xoffset, double yoffset)
{
    auto renderer = static_cast<TerrainRenderer*>(glfwGetWindowUserPointer(window));
    if (renderer->get_gui_manager()->want_capture_mouse())
        return;
    renderer->get_input_mapper()->on_scroll_callback(xoffset, yoffset);
}

TerrainRenderer::TerrainRenderer() {
#ifdef __EMSCRIPTEN__
    // execute on window resize when canvas size changes
    QObject::connect(&WebInterop::instance(), &WebInterop::canvas_size_changed, this, &TerrainRenderer::set_glfw_window_size);
#endif
}

void TerrainRenderer::init_window() {
    if (!glfwInit())
        qFatal("Could not initialize GLFW!");

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    //glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    m_window = glfwCreateWindow(m_viewport_size.x, m_viewport_size.y, "weBIGeo - Geospatial Visualization Tool", NULL, NULL);
    if (!m_window) {
        glfwTerminate();
        qFatal("Could not open GLFW window");
    }
    glfwSetWindowUserPointer(m_window, this);
    glfwSetWindowSizeCallback(m_window, windowResizeCallback);
    glfwSetKeyCallback(m_window, key_callback);
    glfwSetCursorPosCallback(m_window, cursor_position_callback);
    glfwSetMouseButtonCallback(m_window, mouse_button_callback);
    glfwSetScrollCallback(m_window, scroll_callback);

#ifndef __EMSCRIPTEN__
    // Load Icon for Window
    auto icon = nucleus::stb::load_8bit_rgba_image_from_file(":/icons/logo32.png");
    GLFWimage image = { int(icon.width()), int(icon.height()), icon.bytes() };
    glfwSetWindowIcon(m_window, 1, &image);
#endif
}

void TerrainRenderer::render_gui()
{
#ifdef ALP_WEBGPU_APP_ENABLE_IMGUI
    static bool vsync_enabled = (prop_swapchain_presentmode == WGPUPresentMode::WGPUPresentMode_Fifo);
    if (ImGui::Checkbox("VSync", &vsync_enabled)) {
        prop_swapchain_presentmode = vsync_enabled ? WGPUPresentMode::WGPUPresentMode_Fifo : WGPUPresentMode::WGPUPresentMode_Immediate;
        // Recreate swapchain
        prop_force_repaint_once = true;
        this->on_window_resize(m_viewport_size.x, m_viewport_size.y);
    }
    ImGui::Checkbox("Repaint each frame", &prop_force_repaint);
    ImGui::Text("Repaint-Counter: %d", prop_repaint_count);

    // ImGui::Text("%s", m_gputimer->to_string().c_str());

#endif
}

void TerrainRenderer::render() {
    // Do nothing, this checks for ongoing asynchronous operations and call their callbacks
    m_cputimer->start();
    glfwPollEvents();

    WGPUTextureView swapchain_texture = wgpuSwapChainGetCurrentTextureView(m_swapchain);
    if (!swapchain_texture) {
        qFatal("Cannot acquire next swap chain texture");
    }

    WGPUCommandEncoderDescriptor command_encoder_desc {};
    command_encoder_desc.label = "Command Encoder";
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(m_device, &command_encoder_desc);

    m_gputimer->start(encoder);

    prop_frame_count++;
    if (m_webgpu_window->needs_redraw() || prop_force_repaint || prop_force_repaint_once) {
        m_webgpu_window->paint(m_framebuffer.get(), encoder);
        prop_repaint_count++;
        prop_force_repaint_once = false;
    }

    {
        webgpu::raii::RenderPassEncoder render_pass(encoder, swapchain_texture, nullptr);
        // webgpu::raii::RenderPassEncoder render_pass(encoder, swapchain_texture, nullptr, &m_timestamp_writes);
        wgpuRenderPassEncoderSetPipeline(render_pass.handle(), m_gui_pipeline.get()->pipeline().handle());
        wgpuRenderPassEncoderSetBindGroup(render_pass.handle(), 0, m_gui_bind_group->handle(), 0, nullptr);
        wgpuRenderPassEncoderDraw(render_pass.handle(), 3, 1, 0, 0);

#ifdef ALP_WEBGPU_APP_ENABLE_IMGUI
        // We add the GUI drawing commands to the render pass
        m_gui_manager->render(render_pass.handle());
#endif
    }

    m_gputimer->stop(encoder);

    wgpuTextureViewRelease(swapchain_texture);

    WGPUCommandBufferDescriptor cmd_buffer_descriptor {};
    cmd_buffer_descriptor.label = "Command buffer";
    WGPUCommandBuffer command = wgpuCommandEncoderFinish(encoder, &cmd_buffer_descriptor);
    wgpuCommandEncoderRelease(encoder);
    wgpuQueueSubmit(m_queue, 1, &command);
    wgpuCommandBufferRelease(command);

    m_gputimer->resolve(m_queue);

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

    webgpuPlatformInit();

    webgpu_create_context();

    // TODO: THIS TAKES FOREVER ON FIRST LOAD. LETS CHECK OUT WHY!
    m_controller = std::make_unique<nucleus::Controller>(m_webgpu_window.get());

    nucleus::camera::Controller* camera_controller = m_controller->camera_controller();
    m_inputMapper = std::make_unique<InputMapper>(this, camera_controller);

    connect(this, &TerrainRenderer::update_camera_requested, camera_controller, &nucleus::camera::Controller::update_camera_request);

    m_webgpu_window->set_wgpu_context(m_instance, m_device, m_adapter, m_surface, m_queue);
    m_webgpu_window->initialise_gpu();
    // Creates the swapchain
    this->on_window_resize(m_viewport_size.x, m_viewport_size.y);

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

    glfwSetWindowSize(m_window, m_viewport_size.x, m_viewport_size.y);

    m_timer_manager = std::make_unique<timing::GuiTimerManager>();
#ifdef ALP_WEBGPU_APP_ENABLE_IMGUI
    m_gui_manager = std::make_unique<GuiManager>(this);
    m_gui_manager->init(m_window, m_device, m_swapchain_format, WGPUTextureFormat_Undefined);
#endif

    m_cputimer = std::make_shared<timing::CpuTimer>(120);
    m_timer_manager->add_timer(m_cputimer, "CPU Timer", "Renderer");
    m_gputimer = std::make_shared<timing::WebGpuTimer>(m_device, 4, 120);
    m_timer_manager->add_timer(m_gputimer, "GPU Timer", "Renderer");

    m_initialized = true;

#if defined(__EMSCRIPTEN__)
    emscripten_set_main_loop_arg(
        [](void *userData) {
            TerrainRenderer& renderer = *reinterpret_cast<TerrainRenderer*>(userData);
            renderer.render();
        },
        (void*)this,
        0, true
    );
#else
    while (!glfwWindowShouldClose(m_window)) {
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
    m_webgpu_window->deinit_gpu();

    glfwDestroyWindow(m_window);
    glfwTerminate();
    m_initialized = false;
#endif
}

void TerrainRenderer::set_glfw_window_size(int width, int height) {
    m_viewport_size = { width, height };
    if (m_initialized) {
        glfwSetWindowSize(m_window, width, height);
    }
}

void TerrainRenderer::create_framebuffer(uint32_t width, uint32_t height)
{
    qDebug() << "creating framebuffer textures for size " << width << "x" << height;

    webgpu::FramebufferFormat format { .size = { width, height }, .depth_format = m_depth_texture_format, .color_formats = { m_swapchain_format } };
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
    swapchain_desc.usage = WGPUTextureUsage::WGPUTextureUsage_RenderAttachment;
    swapchain_desc.format = m_swapchain_format;
    swapchain_desc.presentMode = prop_swapchain_presentmode;
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

    m_instance_desc = {
        .nextInChain = nullptr,
    };

#ifndef __EMSCRIPTEN__
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
#endif

    m_instance = wgpuCreateInstance(&m_instance_desc);
    if (!m_instance) {
        qFatal("Could not initialize WebGPU!");
    }
    qInfo() << "Got instance: " << m_instance;

    qDebug() << "Requesting surface...";
    m_surface = glfwGetWGPUSurface(m_instance, m_window);
    if (!m_surface) {
        qFatal("Could not create surface!");
    }
    qInfo() << "Got surface: " << m_surface;

    qDebug() << "Requesting adapter...";
    WGPURequestAdapterOptions adapter_opts {};
    adapter_opts.powerPreference = WGPUPowerPreference_HighPerformance;
    adapter_opts.compatibleSurface = m_surface;
    m_adapter = requestAdapterSync(m_instance, adapter_opts);
    if (!m_adapter) {
        qFatal("Could not get adapter!");
    }
    qInfo() << "Got adapter: " << m_adapter;

    m_webgpu_window = std::make_unique<webgpu_engine::Window>();

    qDebug() << "Requesting device...";
    WGPURequiredLimits required_limits {};
    WGPUSupportedLimits supported_limits {};
#ifndef __EMSCRIPTEN__
    wgpuAdapterGetLimits(m_adapter, &supported_limits);
#else
    // TODO: Update emscripten and hope wgpuAdapterGetLimits is supported
    // or alternatively setup some custom js interop (https://developer.mozilla.org/en-US/docs/Web/API/GPUSupportedLimits)
    supported_limits.limits.maxTextureDimension1D = 8192;
    supported_limits.limits.maxTextureDimension2D = 8192;
    supported_limits.limits.maxTextureDimension3D = 2048;
    supported_limits.limits.maxTextureArrayLayers = 256;
    supported_limits.limits.maxBindGroups = 4;
    supported_limits.limits.maxBindingsPerBindGroup = 640;
    supported_limits.limits.maxDynamicUniformBuffersPerPipelineLayout = 8;
    supported_limits.limits.maxDynamicStorageBuffersPerPipelineLayout = 4;
    supported_limits.limits.maxSampledTexturesPerShaderStage = 16;
    supported_limits.limits.maxSamplersPerShaderStage = 16;
    supported_limits.limits.maxStorageBuffersPerShaderStage = 8;
    supported_limits.limits.maxStorageTexturesPerShaderStage = 4;
    supported_limits.limits.maxUniformBuffersPerShaderStage = 12;
    supported_limits.limits.maxUniformBufferBindingSize = 65536; // 64 KB
    supported_limits.limits.maxStorageBufferBindingSize = 134217728; // 128 MB
    supported_limits.limits.minUniformBufferOffsetAlignment = 256;
    supported_limits.limits.minStorageBufferOffsetAlignment = 256;
    supported_limits.limits.maxVertexBuffers = 8;
    supported_limits.limits.maxBufferSize = 268435456; // 256 MB
    supported_limits.limits.maxVertexAttributes = 16;
    supported_limits.limits.maxVertexBufferArrayStride = 2048;
    supported_limits.limits.maxInterStageShaderComponents = 60;
    supported_limits.limits.maxInterStageShaderVariables = 16;
    supported_limits.limits.maxColorAttachments = 8;
    supported_limits.limits.maxColorAttachmentBytesPerSample = 32;
    supported_limits.limits.maxComputeWorkgroupStorageSize = 16384; // 16 KB
    supported_limits.limits.maxComputeInvocationsPerWorkgroup = 256;
    supported_limits.limits.maxComputeWorkgroupSizeX = 256;
    supported_limits.limits.maxComputeWorkgroupSizeY = 256;
    supported_limits.limits.maxComputeWorkgroupSizeZ = 64;
    supported_limits.limits.maxComputeWorkgroupsPerDimension = 65535;
#endif

    // irrelevant for us, but needs to be set
    required_limits.limits.minStorageBufferOffsetAlignment = supported_limits.limits.minStorageBufferOffsetAlignment;
    required_limits.limits.minUniformBufferOffsetAlignment = supported_limits.limits.minUniformBufferOffsetAlignment;

    // Let the engine change the required limits
    m_webgpu_window->update_required_gpu_limits(required_limits.limits, supported_limits.limits);

    std::vector<WGPUFeatureName> requiredFeatures;
    if (wgpuAdapterHasFeature(m_adapter, WGPUFeatureName_TimestampQuery)) {
        requiredFeatures.push_back(WGPUFeatureName_TimestampQuery);
    }

    WGPUDeviceDescriptor device_desc {};
    device_desc.label = "webigeo device";
    device_desc.requiredFeatures = requiredFeatures.data();
    device_desc.requiredFeatureCount = (uint32_t)requiredFeatures.size();
    device_desc.requiredLimits = &required_limits;
    device_desc.defaultQueue.label = "webigeo queue";
    m_device = requestDeviceSync(m_adapter, device_desc);
    if (!m_device) {
        qFatal("Could not get device!");
    }
    qInfo() << "Got device: " << m_device;

    if (!wgpuDeviceHasFeature(m_device, WGPUFeatureName_TimestampQuery)) {
        qWarning() << "Timestamp queries are not supported!";
    }

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
