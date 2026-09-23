#############################################################################
# AlpineMaps.org
# Copyright (C) 2026 Adam Celarek <family name at cg tuwien ac at>
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

include_guard(GLOBAL)

if(NOT COMMAND alp_add_git_repository)
    include(${CMAKE_CURRENT_LIST_DIR}/AddRepo.cmake)
endif()

if(NOT COMMAND alp_check_for_script_updates)
    include(${CMAKE_CURRENT_LIST_DIR}/CheckForScriptUpdates.cmake)
endif()

alp_check_for_script_updates("${CMAKE_CURRENT_LIST_FILE}")

macro(_alp_append_cache_arg out var type)
    if(DEFINED ${var} AND NOT "${${var}}" STREQUAL "")
        set(_alp_cache_value "${${var}}")
        string(REPLACE ";" "\\;" _alp_cache_value "${_alp_cache_value}")
        list(APPEND ${out} "-D${var}:${type}=${_alp_cache_value}")
        unset(_alp_cache_value)
    endif()
endmacro()

macro(_alp_append_key_value out var)
    if(DEFINED ${var})
        set(_alp_key_value "${${var}}")
        string(REPLACE ";" "\\;" _alp_key_value "${_alp_key_value}")
        list(APPEND ${out} "${var}=${_alp_key_value}")
        unset(_alp_key_value)
    endif()
endmacro()

set(_ALP_CMAKE_PROJECT_FORWARD_VARS
    CMAKE_TOOLCHAIN_FILE
    CMAKE_SYSROOT
    CMAKE_FIND_ROOT_PATH
    CMAKE_FIND_ROOT_PATH_MODE_PROGRAM
    CMAKE_FIND_ROOT_PATH_MODE_LIBRARY
    CMAKE_FIND_ROOT_PATH_MODE_INCLUDE
    CMAKE_FIND_ROOT_PATH_MODE_PACKAGE
    CMAKE_SYSTEM_NAME
    CMAKE_SYSTEM_PROCESSOR
    CMAKE_SYSTEM_VERSION
    CMAKE_C_COMPILER
    CMAKE_CXX_COMPILER
    CMAKE_AR
    CMAKE_RANLIB
    CMAKE_MAKE_PROGRAM
    CMAKE_CROSSCOMPILING_EMULATOR
    CMAKE_POSITION_INDEPENDENT_CODE
    CMAKE_MSVC_RUNTIME_LIBRARY
    QT_HOST_PATH
    QT_HOST_PATH_CMAKE_DIR
    Qt6HostInfo_DIR
    ANDROID_ABI
    ANDROID_PLATFORM
    ANDROID_STL
    ANDROID_NDK
    ANDROID_SDK_ROOT
    ANDROID_USE_LEGACY_TOOLCHAIN_FILE
    EMSCRIPTEN
    EMSCRIPTEN_FORCE_COMPILERS
    EMSCRIPTEN_GENERATE_BITCODE_STATIC_LIBRARIES)

function(_alp_build_and_install_cmake_project NAME SRC_DIR BUILD_DIR INSTALL_DIR BUILD_CONFIG)
    set(_generator_args)
    if(CMAKE_GENERATOR_PLATFORM)
        list(APPEND _generator_args -A "${CMAKE_GENERATOR_PLATFORM}")
    endif()
    if(CMAKE_GENERATOR_TOOLSET)
        list(APPEND _generator_args -T "${CMAKE_GENERATOR_TOOLSET}")
    endif()

    set(_configure_args
        "-DCMAKE_INSTALL_PREFIX:PATH=${INSTALL_DIR}"
    )
    _alp_append_cache_arg(_configure_args CMAKE_PREFIX_PATH PATH)

    if(CMAKE_CONFIGURATION_TYPES)
        _alp_append_cache_arg(_configure_args CMAKE_CONFIGURATION_TYPES STRING)
    else()
        list(APPEND _configure_args "-DCMAKE_BUILD_TYPE:STRING=${BUILD_CONFIG}")
    endif()

    foreach(_var IN LISTS _ALP_CMAKE_PROJECT_FORWARD_VARS)
        _alp_append_cache_arg(_configure_args ${_var} STRING)
    endforeach()

    string(JOIN " " _alp_sanitizer_flags ${ALP_SANITIZER_FLAGS})

    foreach(_lang C CXX)
        set(_alp_effective_flags "${CMAKE_${_lang}_FLAGS}")
        if(_alp_sanitizer_flags)
            string(APPEND _alp_effective_flags " ${_alp_sanitizer_flags}")
        endif()
        if(_alp_effective_flags)
            string(REPLACE ";" "\\;" _alp_effective_flags "${_alp_effective_flags}")
            list(APPEND _configure_args "-DCMAKE_${_lang}_FLAGS:STRING=${_alp_effective_flags}")
        endif()
        foreach(_config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
            _alp_append_cache_arg(_configure_args CMAKE_${_lang}_FLAGS_${_config} STRING)
        endforeach()
    endforeach()

    foreach(_kind EXE SHARED MODULE STATIC)
        set(_alp_effective_flags "${CMAKE_${_kind}_LINKER_FLAGS}")
        if(_alp_sanitizer_flags AND NOT _kind STREQUAL "STATIC")
            string(APPEND _alp_effective_flags " ${_alp_sanitizer_flags}")
        endif()
        if(_alp_effective_flags)
            string(REPLACE ";" "\\;" _alp_effective_flags "${_alp_effective_flags}")
            list(APPEND _configure_args "-DCMAKE_${_kind}_LINKER_FLAGS:STRING=${_alp_effective_flags}")
        endif()
        foreach(_config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
            _alp_append_cache_arg(_configure_args CMAKE_${_kind}_LINKER_FLAGS_${_config} STRING)
        endforeach()
    endforeach()

    list(APPEND _configure_args ${ARGN})

    message(STATUS "[alp] Configuring ${NAME}")
    execute_process(
        COMMAND "${CMAKE_COMMAND}"
                -G "${CMAKE_GENERATOR}"
                ${_generator_args}
                -S "${SRC_DIR}"
                -B "${BUILD_DIR}"
                ${_configure_args}
        RESULT_VARIABLE _cfg_res)

    if(_cfg_res)
        message(FATAL_ERROR "[alp] Configuring ${NAME} failed!")
    endif()

    message(STATUS "[alp] Building + installing ${NAME}")
    execute_process(
        COMMAND "${CMAKE_COMMAND}"
                --build "${BUILD_DIR}"
                --config "${BUILD_CONFIG}"
                --parallel
                --target install
        RESULT_VARIABLE _bld_res)

    if(_bld_res)
        message(FATAL_ERROR "[alp] Building ${NAME} failed!")
    endif()
endfunction()

function(alp_setup_cmake_project arg_NAME)
    set(options)
    set(oneValueArgs URL COMMITISH)
    set(multiValueArgs CMAKE_ARGUMENTS)
    cmake_parse_arguments(PARSE_ARGV 1 arg "${options}" "${oneValueArgs}" "${multiValueArgs}")

    if(NOT arg_NAME OR NOT arg_URL OR NOT arg_COMMITISH)
        message(FATAL_ERROR "[alp] alp_setup_cmake_project() needs: <name> URL <url> COMMITISH <tag>")
    endif()

    set(_build_config "${CMAKE_BUILD_TYPE}")
    if(NOT _build_config)
        set(_build_config Release)
    endif()

    alp_add_git_repository(${arg_NAME} URL ${arg_URL} COMMITISH ${arg_COMMITISH} DO_NOT_ADD_SUBPROJECT)
    set(_src_dir "${${arg_NAME}_SOURCE_DIR}")
    set(_build_dir "${CMAKE_BINARY_DIR}/alp_external/${arg_NAME}_build")
    set(_install_dir "${CMAKE_BINARY_DIR}/alp_external/${arg_NAME}")

    set(_version_var "ALP_INSTALLED_${arg_NAME}_VERSION")
    set(_path_var "ALP_INSTALLED_${arg_NAME}_PATH")
    set(_key_parts
        "URL=${arg_URL}"
        "COMMITISH=${arg_COMMITISH}"
        "CMAKE_ARGUMENTS=${arg_CMAKE_ARGUMENTS}"
        "CMAKE_GENERATOR=${CMAKE_GENERATOR}"
        "CMAKE_GENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}"
        "CMAKE_GENERATOR_TOOLSET=${CMAKE_GENERATOR_TOOLSET}"
        "CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
        "CMAKE_CONFIGURATION_TYPES=${CMAKE_CONFIGURATION_TYPES}"
        "BUILD_CONFIG=${_build_config}")
    _alp_append_key_value(_key_parts ALP_SANITIZER_FLAGS)
    _alp_append_key_value(_key_parts CMAKE_PREFIX_PATH)
    foreach(_var IN LISTS _ALP_CMAKE_PROJECT_FORWARD_VARS)
        _alp_append_key_value(_key_parts ${_var})
    endforeach()
    foreach(_lang C CXX)
        _alp_append_key_value(_key_parts CMAKE_${_lang}_FLAGS)
        foreach(_config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
            _alp_append_key_value(_key_parts CMAKE_${_lang}_FLAGS_${_config})
        endforeach()
    endforeach()
    foreach(_kind EXE SHARED MODULE STATIC)
        _alp_append_key_value(_key_parts CMAKE_${_kind}_LINKER_FLAGS)
        foreach(_config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
            _alp_append_key_value(_key_parts CMAKE_${_kind}_LINKER_FLAGS_${_config})
        endforeach()
    endforeach()
    string(JOIN "\n" _key_input ${_key_parts})
    string(SHA256 _key "${_key_input}")

    if(DEFINED ${_version_var}
            AND "${${_version_var}}" STREQUAL "${_key}"
            AND DEFINED ${_path_var}
            AND EXISTS "${${_path_var}}")
        list(PREPEND CMAKE_PREFIX_PATH "${${_path_var}}")
        set(CMAKE_PREFIX_PATH "${CMAKE_PREFIX_PATH}" PARENT_SCOPE)
        set(ALP_${arg_NAME}_INSTALL_DIR "${${_path_var}}" PARENT_SCOPE)
        return()
    endif()

    file(REMOVE_RECURSE "${_build_dir}" "${_install_dir}")
    _alp_build_and_install_cmake_project(
        ${arg_NAME}
        "${_src_dir}"
        "${_build_dir}"
        "${_install_dir}"
        "${_build_config}"
        ${arg_CMAKE_ARGUMENTS})

    list(PREPEND CMAKE_PREFIX_PATH "${_install_dir}")
    set(CMAKE_PREFIX_PATH "${CMAKE_PREFIX_PATH}" PARENT_SCOPE)
    set(ALP_${arg_NAME}_INSTALL_DIR "${_install_dir}" PARENT_SCOPE)

    set(${_path_var} "${_install_dir}" CACHE PATH "Install path for ${arg_NAME}" FORCE)
    set(${_version_var} "${_key}" CACHE STRING "Installed cache key for ${arg_NAME}" FORCE)
endfunction()
