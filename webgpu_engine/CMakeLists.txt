#############################################################################
# Alpine Terrain Renderer
# Copyright (C) 2024 Adam Celarek <family name at cg tuwien ac at>
# Copyright (C) 2024 Gerald Kimmersdorfer
# Copyright (C) 2024 Patrick Komon
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

project(alpine-renderer-webgpu_engine LANGUAGES C CXX)


set(SOURCES
    Window.h Window.cpp
    ShaderModuleManager.h ShaderModuleManager.cpp
    PipelineManager.h PipelineManager.cpp
    UniformBufferObjects.h UniformBufferObjects.cpp
    TileSet.h
    TileManager.h TileManager.cpp
    Buffer.h Buffer.cpp

    compute/GpuHashMap.h compute/GpuHashMap.cpp
    compute/GpuTileStorage.h compute/GpuTileStorage.cpp
    compute/GpuTileId.h compute/GpuTileId.cpp
    compute/RectangularTileRegion.h compute/RectangularTileRegion.cpp
    compute/nodes/Node.h compute/nodes/Node.cpp
    compute/nodes/TileSelectNode.h compute/nodes/TileSelectNode.cpp
    compute/nodes/TileRequestNode.h compute/nodes/TileRequestNode.cpp
    compute/nodes/CreateHashMapNode.h compute/nodes/CreateHashMapNode.cpp
    compute/nodes/NormalComputeNode.h compute/nodes/NormalComputeNode.cpp
    compute/nodes/DownsampleComputeNode.h compute/nodes/DownsampleComputeNode.cpp
    compute/nodes/NodeGraph.h compute/nodes/NodeGraph.cpp
)

qt_add_library(webgpu_engine STATIC ${SOURCES})

qt_add_resources(webgpu_engine "shaders"
    PREFIX "/wgsl_shaders"
    BASE "wgsl_shaders/"
    FILES
        "wgsl_shaders/shared_config.wgsl"
        "wgsl_shaders/hashing.wgsl"
        "wgsl_shaders/camera_config.wgsl"
        "wgsl_shaders/Tile.wgsl"
        "wgsl_shaders/screen_pass_shared.wgsl"
        "wgsl_shaders/screen_pass_vert.wgsl"
        "wgsl_shaders/compose_frag.wgsl"
        "wgsl_shaders/atmosphere_frag.wgsl"
        "wgsl_shaders/atmosphere_implementation.wgsl"
        "wgsl_shaders/encoder.wgsl"
        "wgsl_shaders/tile_util.wgsl"
        "wgsl_shaders/tile_id.wgsl"
        "wgsl_shaders/downsample_compute.wgsl"
        "wgsl_shaders/normals_compute.wgsl"
)

target_link_libraries(webgpu_engine PUBLIC nucleus Qt::Core webgpu)
target_include_directories(webgpu_engine PRIVATE .)

if (ALP_WEBGPU_APP_ENABLE_IMGUI)
    alp_add_git_repository(imgui URL https://github.com/AlpineMapsOrgDependencies/imgui_slim.git COMMITISH f738c335f73956ed3715434987e6f82bac442e87 )

    set(IMNODES_IMGUI_TARGET_NAME imgui)
    alp_add_git_repository(imnodes URL https://github.com/AlpineMapsOrgDependencies/imnodes_slim.git COMMITISH 2300279d51bb74ecec9b4e96ba08b1482972051d)
    target_link_libraries(webgpu_engine PUBLIC imgui imnodes)
    target_compile_definitions(webgpu_engine PUBLIC "ALP_WEBGPU_APP_ENABLE_IMGUI")
endif()

target_compile_definitions(webgpu_engine PUBLIC ALP_RESOURCES_PREFIX="${CMAKE_CURRENT_SOURCE_DIR}/shaders/")
