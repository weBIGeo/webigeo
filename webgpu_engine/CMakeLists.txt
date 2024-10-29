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
    TrackRenderer.h TrackRenderer.cpp

    compute/GpuHashMap.h compute/GpuHashMap.cpp
    compute/GpuTileStorage.h compute/GpuTileStorage.cpp
    compute/GpuTileId.h compute/GpuTileId.cpp
    compute/RectangularTileRegion.h compute/RectangularTileRegion.cpp
    compute/nodes/Node.h compute/nodes/Node.cpp
    compute/nodes/SelectTilesNode.h compute/nodes/SelectTilesNode.cpp
    compute/nodes/RequestTilesNode.h compute/nodes/RequestTilesNode.cpp
    compute/nodes/CreateHashMapNode.h compute/nodes/CreateHashMapNode.cpp
    compute/nodes/ComputeNormalsNode.h compute/nodes/ComputeNormalsNode.cpp
    compute/nodes/DownsampleTilesNode.h compute/nodes/DownsampleTilesNode.cpp
    compute/nodes/NodeGraph.h compute/nodes/NodeGraph.cpp
    compute/nodes/ComputeSnowNode.h compute/nodes/ComputeSnowNode.cpp
    compute/nodes/UpsampleTexturesNode.h compute/nodes/UpsampleTexturesNode.cpp
    compute/nodes/ComputeAvalancheTrajectoriesNode.h compute/nodes/ComputeAvalancheTrajectoriesNode.cpp
    compute/nodes/ComputeAvalancheInfluenceAreaNode.h compute/nodes/ComputeAvalancheInfluenceAreaNode.cpp
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
        "wgsl_shaders/tile_hashmap.wgsl"
        "wgsl_shaders/downsample_compute.wgsl"
        "wgsl_shaders/normals_compute.wgsl"
        "wgsl_shaders/noise.wgsl"
        "wgsl_shaders/snow.wgsl"
        "wgsl_shaders/snow_compute.wgsl"
        "wgsl_shaders/upsample_textures_compute.wgsl"
        "wgsl_shaders/line_render.wgsl"
        "wgsl_shaders/avalanche_trajectories_compute.wgsl"
        "wgsl_shaders/avalanche_trajectories_buffer_to_texture_compute.wgsl"
        "wgsl_shaders/avalanche_influence_area_compute.wgsl"
        "wgsl_shaders/filtering.wgsl"
        "wgsl_shaders/normals_util.wgsl"
        "wgsl_shaders/color_mapping.wgsl"
    )

# Note: This has to go into the webgpu_app as soon as we don't use ImGui in the engine anymore!!!
# We can't do it right now as the engine is not dependent on the app. so we dont have acess in this case.
if (ALP_WEBGPU_APP_ENABLE_IMGUI)
    # For Font Awesome Icon Headers:
    alp_add_git_repository(iconfontcppheaders URL https://github.com/juliettef/IconFontCppHeaders.git COMMITISH f30b1e73b2d71eb331d77619c3f1de34199afc38 DO_NOT_ADD_SUBPROJECT)
    target_include_directories(webgpu_engine PUBLIC ../extern/iconfontcppheaders)

    alp_add_git_repository(imgui URL https://github.com/AlpineMapsOrgDependencies/imgui_slim.git COMMITISH 3b8e925ccea214b6b8c733055691f38bbbb46f89 )

    set(IMNODES_IMGUI_TARGET_NAME imgui)
    alp_add_git_repository(imnodes URL https://github.com/AlpineMapsOrgDependencies/imnodes_slim.git COMMITISH 2300279d51bb74ecec9b4e96ba08b1482972051d)

    alp_add_git_repository(ImGuiFileDialog URL https://github.com/AlpineMapsOrgDependencies/ImGuiFileDialog_slim.git COMMITISH 39f98eb131ee00d476f1002336fc7e9e1795ccc5)
    target_link_libraries(ImGuiFileDialog PRIVATE imgui)

    target_link_libraries(webgpu_engine PUBLIC imgui imnodes ImGuiFileDialog)
    target_compile_definitions(webgpu_engine PUBLIC "ALP_WEBGPU_APP_ENABLE_IMGUI")
endif()

target_link_libraries(webgpu_engine PUBLIC nucleus Qt::Core webgpu)
target_include_directories(webgpu_engine PRIVATE .)

target_compile_definitions(webgpu_engine PUBLIC ALP_RESOURCES_PREFIX="${CMAKE_CURRENT_SOURCE_DIR}/wgsl_shaders/")
