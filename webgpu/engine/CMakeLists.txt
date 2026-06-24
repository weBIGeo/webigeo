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

option(ALP_ENABLE_WGSL_MINIFICATION "Minify WGSL shaders to reduce size" OFF)

project(alpine-renderer-webgpu_engine LANGUAGES C CXX)


set(SOURCES
    Window.h Window.cpp
    UniformBufferObjects.h
    tile_mesh/TileMeshRenderer.h tile_mesh/TileMeshRenderer.cpp
    cloud/CloudRenderer.h cloud/CloudRenderer.cpp
    track/TrackRenderer.h track/TrackRenderer.cpp
    atmosphere/AtmosphereRenderer.h atmosphere/AtmosphereRenderer.cpp
    sky/params.h sky/params.cpp
    sky/uniforms.h sky/uniforms.cpp
    sky/config.h sky/config.cpp
    sky/util.h sky/util.cpp
    sky/resources.h sky/resources.cpp
    sky/pipelines.h sky/pipelines.cpp
    sky/lut_renderer.h sky/lut_renderer.cpp
    sky/sky_renderer.h sky/sky_renderer.cpp
    sky/SkyRenderer.h sky/SkyRenderer.cpp
    sky/atmosphere.h
    overlay/OverlayRenderer.h overlay/OverlayRenderer.cpp
    overlay/Overlay.h
    overlay/HeightLinesOverlay.h overlay/HeightLinesOverlay.cpp
    overlay/ScreenSpaceSnowOverlay.h overlay/ScreenSpaceSnowOverlay.cpp
    overlay/TextureOverlay.h overlay/TextureOverlay.cpp
    overlay/TileDebugOverlay.h overlay/TileDebugOverlay.cpp
    Context.h Context.cpp
)

qt_add_library(webgpu_engine STATIC ${SOURCES})

if (ALP_ENABLE_WGSL_MINIFICATION)
    set(SHADER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
    set(SHADER_MINIFIED_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders_minified")
    set(MINIFY_SCRIPT "${CMAKE_SOURCE_DIR}/misc/scripts/minify_shaders.py")

    find_package(Python3 COMPONENTS Interpreter REQUIRED)

    execute_process(
        COMMAND ${Python3_EXECUTABLE} "${MINIFY_SCRIPT}" "${SHADER_SOURCE_DIR}" "${SHADER_MINIFIED_DIR}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        RESULT_VARIABLE MINIFY_RESULT
    )
    if(NOT MINIFY_RESULT EQUAL 0)
        message(FATAL_ERROR "Shader minification failed")
    endif()

    file(GLOB_RECURSE SHADER_SOURCES "${SHADER_SOURCE_DIR}/*.wgsl")
    add_custom_target(minify_shaders
        COMMAND ${Python3_EXECUTABLE} "${MINIFY_SCRIPT}" "${SHADER_SOURCE_DIR}" "${SHADER_MINIFIED_DIR}"
        DEPENDS ${SHADER_SOURCES} ${MINIFY_SCRIPT}
        COMMENT "Minifying shaders"
        VERBATIM
    )
    add_dependencies(webgpu_engine minify_shaders)

    set(SHADER_BASE_DIR "${SHADER_MINIFIED_DIR}")
    target_compile_definitions(webgpu_engine PUBLIC ALP_ENABLE_WGSL_MINIFICATION)
else()
    set(SHADER_BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
endif()

qt_add_resources(webgpu_engine "shaders"
    PREFIX "/shaders/webgpu_engine"
    BASE "${SHADER_BASE_DIR}"
    FILES
        "${SHADER_BASE_DIR}/render_tiles.wgsl"
        "${SHADER_BASE_DIR}/render_atmosphere.wgsl"
        "${SHADER_BASE_DIR}/render_lines.wgsl"
        "${SHADER_BASE_DIR}/compose_pass.wgsl"
        "${SHADER_BASE_DIR}/present_pass.wgsl"
        "${SHADER_BASE_DIR}/cloud_composite_pass.wgsl"
        "${SHADER_BASE_DIR}/screen_pass_vert.wgsl"
        "${SHADER_BASE_DIR}/render_clouds.wgsl"
        "${SHADER_BASE_DIR}/upscale_clouds.wgsl"

        "${SHADER_BASE_DIR}/overlays/height_lines.wgsl"
        "${SHADER_BASE_DIR}/overlays/texture_overlay.wgsl"
        "${SHADER_BASE_DIR}/overlays/gbuffer_debug.wgsl"
        "${SHADER_BASE_DIR}/overlays/screen_space_snow.wgsl"

        "${SHADER_BASE_DIR}/util/atmosphere.wgsl"
        "${SHADER_BASE_DIR}/util/camera_config.wgsl"
        "${SHADER_BASE_DIR}/util/shared_config.wgsl"

        "${SHADER_BASE_DIR}/sky/render_aerial_perspective_lut.wgsl"
        "${SHADER_BASE_DIR}/sky/render_multi_scattering_lut.wgsl"
        "${SHADER_BASE_DIR}/sky/render_sky_luts_and_raymarch.wgsl"
        "${SHADER_BASE_DIR}/sky/render_sky_raymarching.wgsl"
        "${SHADER_BASE_DIR}/sky/render_sky_view_lut.wgsl"
        "${SHADER_BASE_DIR}/sky/render_sky_with_luts.wgsl"
        "${SHADER_BASE_DIR}/sky/render_transmittance_lut.wgsl"
        "${SHADER_BASE_DIR}/sky/common/aerial_perspective.wgsl"
        "${SHADER_BASE_DIR}/sky/common/blend.wgsl"
        "${SHADER_BASE_DIR}/sky/common/constants.wgsl"
        "${SHADER_BASE_DIR}/sky/common/coordinate_system.wgsl"
        "${SHADER_BASE_DIR}/sky/common/custom_uniforms.wgsl"
        "${SHADER_BASE_DIR}/sky/common/hg_draine_large.wgsl"
        "${SHADER_BASE_DIR}/sky/common/hg_draine_mid1.wgsl"
        "${SHADER_BASE_DIR}/sky/common/hg_draine_mid2.wgsl"
        "${SHADER_BASE_DIR}/sky/common/hg_draine_phase_const.wgsl"
        "${SHADER_BASE_DIR}/sky/common/hg_draine_small.wgsl"
        "${SHADER_BASE_DIR}/sky/common/intersection.wgsl"
        "${SHADER_BASE_DIR}/sky/common/medium.wgsl"
        "${SHADER_BASE_DIR}/sky/common/multiple_scattering.wgsl"
        "${SHADER_BASE_DIR}/sky/common/phase.wgsl"
        "${SHADER_BASE_DIR}/sky/common/sample_sagment_t.wgsl"
        "${SHADER_BASE_DIR}/sky/common/shadow.wgsl"
        "${SHADER_BASE_DIR}/sky/common/shadow_base.wgsl"
        "${SHADER_BASE_DIR}/sky/common/sky_view.wgsl"
        "${SHADER_BASE_DIR}/sky/common/sun_disk.wgsl"
        "${SHADER_BASE_DIR}/sky/common/uniforms.wgsl"
        "${SHADER_BASE_DIR}/sky/common/uv.wgsl"
        "${SHADER_BASE_DIR}/sky/common/vertex_full_screen.wgsl"
    )

target_link_libraries(webgpu_engine PUBLIC nucleus Qt::Core webgpu)
target_include_directories(webgpu_engine PRIVATE .)

target_compile_definitions(webgpu_engine PUBLIC ALP_SHADER_DIR_WEBGPU_ENGINE="${CMAKE_CURRENT_SOURCE_DIR}/shaders/")
