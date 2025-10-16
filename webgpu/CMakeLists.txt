#############################################################################
# weBIGeo
# Copyright (C) 2024 Gerald Kimmersdorfer
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#############################################################################

project(alpine-renderer-webgpu LANGUAGES C CXX)

# NOTE: The idea behind this target is to accumulate all the webgpu related things that are platform
# dependent such that we only have the webgpu cmake target for the other projects to link against.

set(ALP_DAWN_DIR "${CMAKE_SOURCE_DIR}/../dawn" CACHE PATH "path to the dawn repo with debug and release builds. (only necessary for native build)")

set(ALP_SDL_INSTALL_DIR "${CMAKE_SOURCE_DIR}/../SDL" CACHE PATH "Path to the SDL installation directory")

set(SOURCES

    raii/base_types.h
    raii/Texture.h raii/Texture.cpp
    raii/TextureWithSampler.h raii/TextureWithSampler.cpp
    raii/RawBuffer.h
    raii/TextureView.h raii/TextureView.cpp
    raii/Sampler.h raii/Sampler.cpp
    raii/BindGroup.h raii/BindGroup.cpp
    raii/BindGroupLayout.h raii/BindGroupLayout.cpp
    raii/Pipeline.h raii/Pipeline.cpp
    raii/PipelineLayout.h raii/PipelineLayout.cpp
    raii/CombinedComputePipeline.h raii/CombinedComputePipeline.cpp
    raii/RenderPassEncoder.h raii/RenderPassEncoder.cpp

    util/VertexBufferInfo.h util/VertexBufferInfo.cpp
    util/VertexFormat.h
    util/string_cast.h util/string_cast.cpp

    timing/GuiTimerManager.h timing/GuiTimerManager.cpp
    timing/TimerInterface.h timing/TimerInterface.cpp
    timing/CpuTimer.h timing/CpuTimer.cpp
    timing/WebGpuTimer.h timing/WebGpuTimer.cpp

    Framebuffer.h Framebuffer.cpp

    webgpu_interface.hpp webgpu_interface.cpp)

add_library(webgpu STATIC ${SOURCES})

if (EMSCRIPTEN)
    target_compile_options(webgpu PUBLIC
        "--use-port=sdl2"
        "--use-port=emdawnwebgpu" # could use this instead of locally compiled emdawnwebgpu
    )

    # using self-compiled version of emdawnwebgpu_cpp seems to introduce compile errors, stick to --use-port for now
    #target_link_libraries(webgpu PUBLIC emdawnwebgpu_cpp)

    target_link_options(webgpu PUBLIC
        "-sASYNCIFY=1"
        "--use-port=sdl2"
        "--use-port=emdawnwebgpu" # could use this instead of locally compiled emdawnwebgpu
    )

else()

    # set configuration variables for Dawn build
    set(DAWN_BUILD_MONOLITHIC_LIBRARY STATIC)
    set(DAWN_FETCH_DEPENDENCIES ON)
    set(DAWN_FORCE_SYSTEM_COMPONENT_LOAD ON) # currently needed, see https://issues.chromium.org/issues/441317244
    set(TINT_BUILD_SPV_READER OFF)
    set(TINT_BUILD_TESTS OFF)
    set(TINT_BUILD_FUZZERS OFF)
    set(TINT_BUILD_BENCHMARKS OFF)
    set(TINT_BUILD_AS_OTHER_OS OFF)
    set(DAWN_BUILD_SAMPLES OFF)
    set(DAWN_ENABLE_D3D11 OFF)
    set(DAWN_ENABLE_D3D12 OFF)
    set(DAWN_ENABLE_METAL OFF)
    set(DAWN_ENABLE_NULL OFF)
    set(DAWN_ENABLE_DESKTOP_GL OFF)
    set(DAWN_ENABLE_OPENGLES OFF)
    set(DAWN_ENABLE_VULKAN ON)
    set(DAWN_USE_WINDOWS_UI OFF)
    #set(DAWN_USE_GLFW OFF) # TODO turning this off leads to a linker error, "cant find glfw.lib" - how/where/why are we trying to link to glfw?

    # manually cloned at ALP_DAWN_DIR
    add_subdirectory("${ALP_DAWN_DIR}" "${ALP_DAWN_DIR}/build/${CMAKE_CXX_COMPILER_ID}_${CMAKE_CXX_COMPILER_VERSION}_${CMAKE_BUILD_TYPE}" EXCLUDE_FROM_ALL)

    # add dawn libraries to webgpu library
    target_link_libraries(webgpu PUBLIC webgpu_dawn)

    # find and add SDL2 (you may install it to the default install folder or ALP_SDL_INSTALL_DIR)
    # Add ALP_SDL_INSTALL_DIR to CMake's search path for find_package
    list(APPEND CMAKE_PREFIX_PATH "${ALP_SDL_INSTALL_DIR}")
    find_package(SDL2 REQUIRED)
    target_link_libraries(webgpu PUBLIC SDL2::SDL2 SDL2::SDL2main)

    # If tint_lang_hlsl_writer.lib within ${ALP_DAWN_DIR}/build/debug/src exists DAWN was compiled with DX-Backends.
    file(GLOB_RECURSE TINT_LIB_PATH "${ALP_DAWN_DIR}/build/debug/src/tint_lang_hlsl_writer.lib")
    if (TINT_LIB_PATH)
        # NOTE: With Direct3D Backends we additionally need to link against dx.
        message(STATUS "Found tint_lang_hlsl_writer.lib, DAWN seems to be compiled with DX-Backends. Will link against dxguid.lib.")
        find_library(DXGUID_LIB dxguid.lib)
        if (DXGUID_LIB)
            target_link_libraries(webgpu PUBLIC ${DXGUID_LIB})
        else()
            message(FATAL_ERROR "dxguid.lib not found.")
        endif()
    endif()
endif()

# set public IMGUI_IMPL_WEBGPU_BACKEND_DAWN flag
target_compile_definitions(webgpu PUBLIC IMGUI_IMPL_WEBGPU_BACKEND_DAWN)

target_link_libraries(webgpu PUBLIC Qt::Core nucleus)
target_include_directories(webgpu PRIVATE .)
