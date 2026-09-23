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

set(ALP_DAWN_VERSION "20260420.230544")
set(ALP_DXC_URL "https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.9.2602/dxc_2026_02_20.zip"
    CACHE INTERNAL "DXC release archive whose dxcompiler.dll/dxil.dll match ALP_DAWN_VERSION")
set(ALP_DAWN_DIR "${CMAKE_SOURCE_DIR}/${ALP_EXTERN_DIR}/dawn")
set(ALP_SDL_INSTALL_DIR "${CMAKE_SOURCE_DIR}/${ALP_EXTERN_DIR}/sdl")
set(ALP_DAWN_PORT_DIR "${CMAKE_SOURCE_DIR}/${ALP_EXTERN_DIR}/emdawnwebgpu_pkg/emdawnwebgpu.port.py")

add_library(alp_webgpu_api INTERFACE)
add_library(alp_sdl2_platform INTERFACE)

find_package(Python3 COMPONENTS Interpreter REQUIRED)

function(alp_find_local_dawn_package out_var)
    set(ALP_DAWN_INSTALL_DIR "${ALP_DAWN_DIR}/install/${ALP_DAWN_CONFIG}")
    set(ALP_DAWN_VERSION_FILE "${ALP_DAWN_INSTALL_DIR}/.alp_dawn_version")
    if (NOT EXISTS "${ALP_DAWN_VERSION_FILE}")
        set(${out_var} "" PARENT_SCOPE)
        return()
    endif()

    file(READ "${ALP_DAWN_VERSION_FILE}" ALP_DAWN_INSTALLED_VERSION)
    string(STRIP "${ALP_DAWN_INSTALLED_VERSION}" ALP_DAWN_INSTALLED_VERSION)
    if (NOT ALP_DAWN_INSTALLED_VERSION STREQUAL ALP_DAWN_VERSION)
        set(${out_var} "" PARENT_SCOPE)
        return()
    endif()

    foreach(ALP_DAWN_LIB_DIR lib lib64)
        set(ALP_DAWN_PACKAGE_CANDIDATE "${ALP_DAWN_INSTALL_DIR}/${ALP_DAWN_LIB_DIR}/cmake/Dawn")
        if (EXISTS "${ALP_DAWN_PACKAGE_CANDIDATE}/DawnConfig.cmake")
            set(${out_var} "${ALP_DAWN_PACKAGE_CANDIDATE}" PARENT_SCOPE)
            return()
        endif()
    endforeach()
    set(${out_var} "" PARENT_SCOPE)
endfunction()

function(alp_mark_local_dawn_package)
    foreach(ALP_DAWN_MARK_CONFIG Debug Release)
        set(ALP_DAWN_MARK_DIR "${ALP_DAWN_DIR}/install/${ALP_DAWN_MARK_CONFIG}")
        if (EXISTS "${ALP_DAWN_MARK_DIR}")
            file(WRITE "${ALP_DAWN_MARK_DIR}/.alp_dawn_version" "${ALP_DAWN_VERSION}\n")
        endif()
    endforeach()
endfunction()

if (EMSCRIPTEN)
    if (NOT EXISTS "${ALP_DAWN_PORT_DIR}")
        message(STATUS "Dawn port not found, fetching...")
        execute_process(
            COMMAND ${Python3_EXECUTABLE}
                "${CMAKE_SOURCE_DIR}/misc/scripts/fetch_dawn_port.py"
                --dawn-version "${ALP_DAWN_VERSION}"
                --extern-dir "${ALP_EXTERN_DIR}"
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            RESULT_VARIABLE FETCH_RESULT
        )
        if (NOT FETCH_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to fetch Dawn Emscripten port.")
        endif()
    endif()

    target_compile_options(alp_webgpu_api INTERFACE
        "--use-port=${ALP_DAWN_PORT_DIR}"
    )
    target_link_options(alp_webgpu_api INTERFACE
        "-sASYNCIFY=1"
        "--use-port=${ALP_DAWN_PORT_DIR}"
    )
    target_compile_options(alp_sdl2_platform INTERFACE
        "--use-port=sdl2"
    )
    target_link_options(alp_sdl2_platform INTERFACE
        "--use-port=sdl2"
    )
else()
    if (CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(ALP_DAWN_CONFIG "Debug")
    else()
        set(ALP_DAWN_CONFIG "Release")
    endif()

    if (WIN32)
        set(ALP_DAWN_NATIVE_PLATFORM "windows-latest")
    elseif(APPLE)
        set(ALP_DAWN_NATIVE_PLATFORM "macos-latest")
    else()
        set(ALP_DAWN_NATIVE_PLATFORM "ubuntu-latest")
    endif()

    alp_find_local_dawn_package(ALP_DAWN_CMAKE_PACKAGE)
    if (NOT ALP_DAWN_CMAKE_PACKAGE)
        message(STATUS "Dawn installation missing - fetching native Dawn package...")
        execute_process(
            COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/misc/scripts/fetch_dawn_native.py"
                --dawn-version "${ALP_DAWN_VERSION}"
                --extern-dir "${ALP_EXTERN_DIR}"
                --build-type "${ALP_DAWN_CONFIG}"
                --platform "${ALP_DAWN_NATIVE_PLATFORM}"
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            RESULT_VARIABLE DAWN_FETCH_RESULT
        )

        if (NOT DAWN_FETCH_RESULT EQUAL 0)
            message(STATUS "Fetching native Dawn package failed - running install_dawn.py...")
            if (NOT DEFINED ALP_CMAKE_EXECUTABLE)
                find_program(ALP_CMAKE_EXECUTABLE cmake)
                if (NOT ALP_CMAKE_EXECUTABLE)
                    message(FATAL_ERROR "cmake not found in PATH")
                endif()
            endif()
            if (NOT DEFINED ALP_NINJA_EXECUTABLE)
                find_program(ALP_NINJA_EXECUTABLE ninja)
                if (NOT ALP_NINJA_EXECUTABLE)
                    message(FATAL_ERROR "ninja not found in PATH")
                endif()
            endif()

            execute_process(
                COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/misc/scripts/install_dawn.py"
                    --dawn-version ${ALP_DAWN_VERSION}
                    --extern-dir "${ALP_EXTERN_DIR}"
                    --cmake-path "${ALP_CMAKE_EXECUTABLE}"
                    --ninja-path "${ALP_NINJA_EXECUTABLE}"
                WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
                RESULT_VARIABLE DAWN_RESULT
            )
            if (NOT DAWN_RESULT EQUAL 0)
                message(FATAL_ERROR "install_dawn.py failed.")
            endif()
        endif()

        alp_mark_local_dawn_package()
        alp_find_local_dawn_package(ALP_DAWN_CMAKE_PACKAGE)
    endif()

    if (NOT ALP_DAWN_CMAKE_PACKAGE)
        message(FATAL_ERROR "Dawn ${ALP_DAWN_VERSION} was not found in ${ALP_DAWN_DIR}/install/${ALP_DAWN_CONFIG}.")
    endif()

    set(Dawn_DIR "${ALP_DAWN_CMAKE_PACKAGE}")
    find_package(Dawn REQUIRED CONFIG PATHS "${ALP_DAWN_CMAKE_PACKAGE}" NO_DEFAULT_PATH)
    target_link_libraries(alp_webgpu_api INTERFACE dawn::webgpu_dawn)

    if (NOT EXISTS "${ALP_SDL_INSTALL_DIR}")
        message(STATUS "SDL not found - running install_sdl.py...")
        if (NOT DEFINED ALP_CMAKE_EXECUTABLE)
            find_program(ALP_CMAKE_EXECUTABLE cmake)
            if (NOT ALP_CMAKE_EXECUTABLE)
                message(FATAL_ERROR "cmake not found")
            endif()
        endif()
        if (NOT DEFINED ALP_NINJA_EXECUTABLE)
            find_program(ALP_NINJA_EXECUTABLE ninja)
            if (NOT ALP_NINJA_EXECUTABLE)
                message(FATAL_ERROR "ninja not found")
            endif()
        endif()
        if (NOT DEFINED ALP_GIT_EXECUTABLE)
            find_program(ALP_GIT_EXECUTABLE git)
            if (NOT ALP_GIT_EXECUTABLE)
                message(FATAL_ERROR "git not found")
            endif()
        endif()

        execute_process(
            COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/misc/scripts/install_sdl.py"
                --install-prefix "${ALP_SDL_INSTALL_DIR}"
                --cmake-path "${ALP_CMAKE_EXECUTABLE}"
                --ninja-path "${ALP_NINJA_EXECUTABLE}"
                --git-path "${ALP_GIT_EXECUTABLE}"
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            RESULT_VARIABLE SDL_RESULT
        )
        if (NOT SDL_RESULT EQUAL 0)
            message(FATAL_ERROR "install_sdl.py failed.")
        endif()
    endif()

    list(APPEND CMAKE_PREFIX_PATH "${ALP_SDL_INSTALL_DIR}")
    find_package(SDL2 REQUIRED)
    target_link_libraries(alp_sdl2_platform INTERFACE SDL2::SDL2)

    file(GLOB_RECURSE TINT_LIB_PATH "${ALP_DAWN_DIR}/out/Debug/src/tint/tint_lang_hlsl_writer.lib")
    if (TINT_LIB_PATH)
        message(STATUS "Found tint_lang_hlsl_writer.lib, Dawn seems to be compiled with DX-Backends. Will link against dxguid.lib.")
        find_library(DXGUID_LIB dxguid.lib)
        if (DXGUID_LIB)
            target_link_libraries(alp_webgpu_api INTERFACE ${DXGUID_LIB})
        else()
            message(FATAL_ERROR "dxguid.lib not found.")
        endif()
    endif()
endif()
