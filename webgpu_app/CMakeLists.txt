#############################################################################
# weBIGeo
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

project(alpine-renderer-webgpu_app LANGUAGES C CXX)

# option to enable/disable imgui
option(ALP_WEBGPU_APP_ENABLE_IMGUI "Enable imgui" ON)

set(SOURCES
    main.cpp
    TerrainRenderer.h TerrainRenderer.cpp
    InputMapper.h InputMapper.cpp

    util/error_logging.h util/error_logging.cpp
    util/url_tools.h util/url_tools.cpp
    util/dark_mode.h util/dark_mode.cpp

    GuiManager.h GuiManager.cpp
)

if (EMSCRIPTEN)
    list(APPEND SOURCES WebInterop.h WebInterop.cpp)
endif()

qt_add_executable(webgpu_app
    resources.qrc
    ${SOURCES}
)


set_target_properties(webgpu_app PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
)

target_link_libraries(webgpu_app PUBLIC webgpu webgpu_engine Qt::Core)
target_include_directories(webgpu_app PRIVATE .)

if (EMSCRIPTEN)
    # Define the exported functions
    set(WEBGPU_EXPORTED_FUNCTIONS
        _main
        _global_file_uploaded
        _malloc
        _free
    )

    # Conditionally add threading-related functions
    if (ALP_ENABLE_THREADING)
        list(APPEND WEBGPU_EXPORTED_FUNCTIONS
            __emscripten_thread_crashed
            __embind_initialize_bindings
        )
    endif()

    string(REPLACE ";" "," WEBGPU_EXPORTED_FUNCTIONS_STRING "${WEBGPU_EXPORTED_FUNCTIONS}")

    # because of a bug with emscripten and -pthreads, we have to add __emscripten_thread_crashed,__embind_initialize_bindings
    # to EXPORTED_FUNCTIONS; for details see https://github.com/emscripten-core/emscripten/issues/21844
    target_link_options(webgpu_app PRIVATE
            -sUSE_SDL=2 # Use emscripten SDL2 implementation
            -sUSE_WEBGPU # Handle WebGPU symbols
            -sASYNCIFY # Required by WebGPU-C++
            -sEXPORTED_FUNCTIONS=${WEBGPU_EXPORTED_FUNCTIONS_STRING} # _main needs to be specifically exported if sEXPORTED_FUNCTIONS is set
            #-sEXPORTED_RUNTIME_METHODS=ccall,cwrap,UTF16ToString,stringToUTF16,JSEvents,specialHTMLTargets,FS # will be overwritten by Qt
            #--preload-file ${CMAKE_CURRENT_SOURCE_DIR}/resources # resources pls via qt resource system
            #--shell-file "${CMAKE_CURRENT_SOURCE_DIR}/shell/${SHELL_FILE}" # setting custom shell here makes no sense as qt will overwrite it anyway
            -msimd128   #enables auto-vectorization and simd support
            -lembind #enable embind
            #-gsource-map #generate source maps

            # FOLLOWING IS SET BY Qt6.6.1 (Single thread)
            #-s INITIAL_MEMORY=50MB
            #-s EXPORTED_RUNTIME_METHODS=UTF16ToString,stringToUTF16,JSEvents,specialHTMLTargets,FS
            #-s MAX_WEBGL_VERSION=2
            #-s FETCH=1  # necessary for Network-Requests
            #-s WASM_BIGINT=1
            #-s STACK_SIZE=5MB
            #-s MODULARIZE=1
            #-s EXPORT_NAME=createQtAppInstance
            #-s DISABLE_EXCEPTION_CATCHING=1
            #-s ALLOW_MEMORY_GROWTH
            #-s DEMANGLE_SUPPORT=1
            #--profiling-funcs
            #-sASYNCIFY_IMPORTS=qt_asyncify_suspend_js,qt_asyncify_resume_js
            #-s ERROR_ON_UNDEFINED_SYMBOLS=1
    )

    # enables to append to EXPORTED_RUNTIME_METHODS which is overridden by Qt
    # see issue (https://bugreports.qt.io/browse/QTBUG-104882) and fix (https://codereview.qt-project.org/c/qt/qtbase/+/421733)
    set_target_properties(webgpu_app PROPERTIES QT_WASM_EXTRA_EXPORTED_METHODS "ccall,cwrap")

    # Copy custom shell (name of file == name of target)
    # IMPORTANT: THIS WILL ONLY BE EXECUTED WHEN THE TARGET IS ACTUALLY REBUILT
    # SO THERE HAVE TO BE CHANGES IN THE C++ CODE.
    file(GLOB_RECURSE SHELL_FILES "${CMAKE_CURRENT_SOURCE_DIR}/shell/*")
    message(STATUS "SHELL_FILES: ${SHELL_FILES}")
    # now create a custom command to move all those files to the build directory
    foreach(SHELL_FILE ${SHELL_FILES})
        get_filename_component(SHELL_FILE_NAME ${SHELL_FILE} NAME)
        add_custom_command(TARGET webgpu_app POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy
            ${SHELL_FILE}
            ${CMAKE_CURRENT_BINARY_DIR}/${SHELL_FILE_NAME}
        )
    endforeach()

    #add_custom_command(TARGET webgpu_app POST_BUILD
    #    COMMAND ${CMAKE_COMMAND} -E copy
    #            ${CMAKE_CURRENT_SOURCE_DIR}/shell/fullscreen_qt_webgpu_shell.html
    #            ${CMAKE_CURRENT_BINARY_DIR}/webgpu_app.html
    #)


endif()
