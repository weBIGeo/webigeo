#############################################################################
# weBIGeo
# Copyright (C) 2024 Gerald Kimmersdorfer
# Copyright (C) 2025 Patrick Komon
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#############################################################################

include_guard(GLOBAL)

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.h
    ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
    ${imgui_SOURCE_DIR}/imconfig.h
    ${imgui_SOURCE_DIR}/imgui.h
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_internal.h
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imstb_rectpack.h
    ${imgui_SOURCE_DIR}/imstb_textedit.h
    ${imgui_SOURCE_DIR}/imstb_truetype.h
)
target_include_directories(imgui SYSTEM PUBLIC ${imgui_SOURCE_DIR})

add_library(imgui_sdl2_wgpu STATIC
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.h
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_wgpu.h
    ${imgui_SOURCE_DIR}/backends/imgui_impl_wgpu.cpp
)
target_link_libraries(imgui_sdl2_wgpu PUBLIC
    imgui
    alp_webgpu_api
    alp_sdl2_platform
)

target_compile_definitions(imgui_sdl2_wgpu PRIVATE IMGUI_IMPL_WEBGPU_BACKEND_DAWN)
