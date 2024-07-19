/*****************************************************************************
 * Alpine Renderer
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

#include "UnittestWebgpuContext.h"
#include "webgpu/webgpu_interface.hpp"
#include <cassert>
#include <iostream>
#include <limits>

WGPURequiredLimits UnittestWebgpuContext::default_limits()
{
    WGPURequiredLimits required_limits {};
    // irrelevant for us, but needs to be set
    required_limits.limits.minStorageBufferOffsetAlignment = std::numeric_limits<uint32_t>::max();
    required_limits.limits.minUniformBufferOffsetAlignment = std::numeric_limits<uint32_t>::max();
    return required_limits;
}

UnittestWebgpuContext::UnittestWebgpuContext(WGPURequiredLimits required_limits)
{
    webgpu::platformInit();

    instance = wgpuCreateInstance(nullptr);
    assert(instance);

    WGPURequestAdapterOptions adapter_opts {};
    adapter_opts.powerPreference = WGPUPowerPreference_HighPerformance;
    adapter_opts.compatibleSurface = nullptr;
    adapter = webgpu::requestAdapterSync(instance, adapter_opts);
    assert(adapter);

    WGPUDeviceDescriptor device_desc {};
    device_desc.label = "webgpu device for unittests";
    device_desc.requiredFeatureCount = 0;
    device_desc.requiredLimits = &required_limits;
    device_desc.defaultQueue.label = "default queue for unittests";
    device = webgpu::requestDeviceSync(adapter, device_desc);
    assert(device);

    auto onDeviceError = [](WGPUErrorType type, char const* message, void* /* pUserData */) {
        std::cout << "Uncaptured device error: type " << type;
        if (message)
            std::cout << " (" << message << ")";
        std::cout << std::endl;
    };
    wgpuDeviceSetUncapturedErrorCallback(device, onDeviceError, nullptr /* pUserData */);

    queue = wgpuDeviceGetQueue(device);
    assert(queue);

    shader_module_manager = std::make_unique<ShaderModuleManager>(device);
    assert(shader_module_manager);
}
