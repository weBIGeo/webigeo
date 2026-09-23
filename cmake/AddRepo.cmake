#############################################################################
# AlpineMaps.org
# Copyright (C) 2023-2026 Adam Celarek <family name at cg tuwien ac at>
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program. If not, see <http://www.gnu.org/licenses/>.
#############################################################################

include_guard(GLOBAL)

function(_alp_add_repo_fail name repo_dir revision operation diagnostic)
    string(STRIP "${diagnostic}" _diagnostic)
    if(_diagnostic STREQUAL "")
        set(_diagnostic "no diagnostic was produced")
    endif()
    message(FATAL_ERROR
        "[alp/git] ${name}: ${operation} failed in '${repo_dir}' for "
        "revision '${revision}': ${_diagnostic}")
endfunction()

function(_alp_add_repo_resolve_commit repo_dir revision output_var)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse --verify "${revision}^{commit}"
        WORKING_DIRECTORY "${repo_dir}"
        RESULT_VARIABLE _result
        OUTPUT_VARIABLE _output
        ERROR_QUIET
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(_result EQUAL 0)
        set("${output_var}" "${_output}" PARENT_SCOPE)
    else()
        set("${output_var}" "" PARENT_SCOPE)
    endif()
endfunction()

function(_alp_add_repo_assert_postconditions name repo_dir revision expected_commit)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse --verify HEAD
        WORKING_DIRECTORY "${repo_dir}"
        RESULT_VARIABLE _head_result
        OUTPUT_VARIABLE _head
        ERROR_VARIABLE _error
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT _head_result EQUAL 0 OR NOT _head STREQUAL expected_commit)
        _alp_add_repo_fail("${name}" "${repo_dir}" "${revision}"
            "verifying HEAD" "expected '${expected_commit}', got '${_head}'; ${_error}")
    endif()

    execute_process(
        COMMAND "${GIT_EXECUTABLE}" symbolic-ref -q HEAD
        WORKING_DIRECTORY "${repo_dir}"
        RESULT_VARIABLE _attached_result
        OUTPUT_VARIABLE _attached_ref
        ERROR_VARIABLE _error
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(_attached_result EQUAL 0)
        _alp_add_repo_fail("${name}" "${repo_dir}" "${revision}"
            "verifying detached HEAD" "HEAD is attached to '${_attached_ref}'")
    elseif(NOT _attached_result EQUAL 1)
        _alp_add_repo_fail("${name}" "${repo_dir}" "${revision}"
            "verifying detached HEAD" "${_error}")
    endif()

    execute_process(
        COMMAND "${GIT_EXECUTABLE}" status --porcelain --untracked-files=all --ignored=no
        WORKING_DIRECTORY "${repo_dir}"
        RESULT_VARIABLE _status_result
        OUTPUT_VARIABLE _status
        ERROR_VARIABLE _error
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT _status_result EQUAL 0 OR NOT _status STREQUAL "")
        _alp_add_repo_fail("${name}" "${repo_dir}" "${revision}"
            "verifying the prepared working tree" "${_error}${_status}")
    endif()

    execute_process(
        COMMAND "${GIT_EXECUTABLE}" submodule status --recursive
        WORKING_DIRECTORY "${repo_dir}"
        RESULT_VARIABLE _submodule_result
        OUTPUT_VARIABLE _submodule_status
        ERROR_VARIABLE _error
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT _submodule_result EQUAL 0)
        _alp_add_repo_fail("${name}" "${repo_dir}" "${revision}"
            "verifying recursive submodules" "${_error}")
    endif()
    if(_submodule_status MATCHES "(^|\n)[-+U]")
        _alp_add_repo_fail("${name}" "${repo_dir}" "${revision}"
            "verifying recursive submodules" "${_submodule_status}")
    endif()
endfunction()

function(_alp_add_repo_prepare_submodules name repo_dir revision checkout_performed)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" submodule status --recursive
        WORKING_DIRECTORY "${repo_dir}"
        RESULT_VARIABLE _status_result
        OUTPUT_VARIABLE _status
        ERROR_VARIABLE _error
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT _status_result EQUAL 0)
        _alp_add_repo_fail("${name}" "${repo_dir}" "${revision}"
            "inspecting recursive submodules" "${_error}")
    endif()

    if(checkout_performed AND EXISTS "${repo_dir}/.gitmodules")
        message(STATUS "[alp/git] ${name}: synchronizing recursive submodule URLs in '${repo_dir}'.")
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" submodule sync --recursive
            WORKING_DIRECTORY "${repo_dir}"
            RESULT_VARIABLE _sync_result
            OUTPUT_VARIABLE _output
            ERROR_VARIABLE _error)
        if(NOT _sync_result EQUAL 0)
            _alp_add_repo_fail("${name}" "${repo_dir}" "${revision}"
                "synchronizing recursive submodule URLs" "${_error}${_output}")
        endif()
    endif()

    if(_status STREQUAL "" OR NOT _status MATCHES "(^|\n)[-+U]")
        message(STATUS "[alp/git] ${name}: recursive submodules are already correct in '${repo_dir}'.")
        return()
    endif()

    if(NOT checkout_performed)
        message(STATUS "[alp/git] ${name}: synchronizing recursive submodule URLs in '${repo_dir}'.")
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" submodule sync --recursive
            WORKING_DIRECTORY "${repo_dir}"
            RESULT_VARIABLE _sync_result
            OUTPUT_VARIABLE _output
            ERROR_VARIABLE _error)
        if(NOT _sync_result EQUAL 0)
            _alp_add_repo_fail("${name}" "${repo_dir}" "${revision}"
                "synchronizing recursive submodule URLs" "${_error}${_output}")
        endif()
    endif()

    message(STATUS "[alp/git] ${name}: updating recursive submodules from local objects in '${repo_dir}'.")
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -c protocol.file.allow=always submodule update
            --init --recursive --checkout --depth 1 --no-fetch
        WORKING_DIRECTORY "${repo_dir}"
        RESULT_VARIABLE _local_result
        OUTPUT_VARIABLE _output
        ERROR_VARIABLE _error)

    if(NOT _local_result EQUAL 0)
        message(STATUS "[alp/git] ${name}: fetching missing recursive submodule revisions in '${repo_dir}'.")
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" -c protocol.file.allow=always submodule update
                --init --recursive --checkout --depth 1
            WORKING_DIRECTORY "${repo_dir}"
            RESULT_VARIABLE _update_result
            OUTPUT_VARIABLE _output
            ERROR_VARIABLE _error)
        if(NOT _update_result EQUAL 0)
            _alp_add_repo_fail("${name}" "${repo_dir}" "${revision}"
                "updating recursive submodules" "${_error}${_output}")
        endif()
    endif()
endfunction()

function(alp_add_git_repository name)
    if(CMAKE_VERSION VERSION_LESS 3.25)
        message(FATAL_ERROR
            "[alp/git] ${name}: AddRepo.cmake requires CMake 3.25 or newer; "
            "found ${CMAKE_VERSION}.")
    endif()

    set(options
        DO_NOT_ADD_SUBPROJECT
        NOT_SYSTEM
        PRIVATE_DO_NOT_CHECK_FOR_SCRIPT_UPDATES)
    set(one_value_args URL COMMITISH DESTINATION_PATH)
    cmake_parse_arguments(PARSE_ARGV 1 PARAM "${options}" "${one_value_args}" "")

    if("${name}" STREQUAL "")
        message(FATAL_ERROR "[alp/git] <empty>: dependency name must be non-empty.")
    endif()
    if(PARAM_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR
            "[alp/git] ${name}: unknown arguments: ${PARAM_UNPARSED_ARGUMENTS}")
    endif()
    if(PARAM_KEYWORDS_MISSING_VALUES)
        message(FATAL_ERROR
            "[alp/git] ${name}: arguments require non-empty values: "
            "${PARAM_KEYWORDS_MISSING_VALUES}")
    endif()
    if(NOT DEFINED PARAM_URL OR PARAM_URL STREQUAL "")
        message(FATAL_ERROR "[alp/git] ${name}: URL must be non-empty.")
    endif()
    if(NOT DEFINED PARAM_COMMITISH OR PARAM_COMMITISH STREQUAL "")
        message(FATAL_ERROR "[alp/git] ${name}: COMMITISH must be non-empty.")
    endif()

    find_package(Git 2.22 QUIET)
    if(NOT Git_FOUND)
        message(FATAL_ERROR
            "[alp/git] ${name}: Git 2.22 or newer is required but was not found.")
    endif()

    if(DEFINED PARAM_DESTINATION_PATH)
        set(_relative_destination "${PARAM_DESTINATION_PATH}")
    else()
        if(NOT DEFINED ALP_EXTERN_DIR OR ALP_EXTERN_DIR STREQUAL "")
            set(_alp_extern_dir "extern")
        else()
            set(_alp_extern_dir "${ALP_EXTERN_DIR}")
        endif()
        set(_relative_destination "${_alp_extern_dir}/${name}")
    endif()

    cmake_path(IS_ABSOLUTE _relative_destination _destination_is_absolute)
    if(_destination_is_absolute)
        message(FATAL_ERROR
            "[alp/git] ${name}: destination '${_relative_destination}' must be relative to "
            "CMAKE_SOURCE_DIR ('${CMAKE_SOURCE_DIR}').")
    endif()
    cmake_path(NORMAL_PATH _relative_destination OUTPUT_VARIABLE _normalized_destination)
    if(_normalized_destination STREQUAL ".." OR _normalized_destination MATCHES "^\.\./")
        message(FATAL_ERROR
            "[alp/git] ${name}: destination '${_relative_destination}' escapes "
            "CMAKE_SOURCE_DIR ('${CMAKE_SOURCE_DIR}').")
    endif()
    cmake_path(ABSOLUTE_PATH _normalized_destination
        BASE_DIRECTORY "${CMAKE_SOURCE_DIR}" NORMALIZE OUTPUT_VARIABLE repo_dir)

    get_property(_check_ran GLOBAL PROPERTY _alp_add_repo_check_flag)
    if(NOT PARAM_PRIVATE_DO_NOT_CHECK_FOR_SCRIPT_UPDATES AND NOT _check_ran)
        # Set the guard before entering the checker. The private option remains the
        # explicit recursion escape hatch used by CheckForScriptUpdates.cmake.
        set_property(GLOBAL PROPERTY _alp_add_repo_check_flag TRUE)
        if(NOT COMMAND alp_check_for_script_updates)
            include("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/CheckForScriptUpdates.cmake")
        endif()
        alp_check_for_script_updates("${CMAKE_CURRENT_FUNCTION_LIST_FILE}")
    endif()

    set(_new_repository FALSE)
    if(EXISTS "${repo_dir}")
        if(NOT IS_DIRECTORY "${repo_dir}")
            message(FATAL_ERROR
                "[alp/git] ${name}: destination '${repo_dir}' exists but is not a directory.")
        endif()
        if(NOT EXISTS "${repo_dir}/.git")
            file(GLOB _destination_entries LIST_DIRECTORIES TRUE
                "${repo_dir}/*" "${repo_dir}/.[!.]*" "${repo_dir}/..?*")
            if(_destination_entries)
                message(FATAL_ERROR
                    "[alp/git] ${name}: destination '${repo_dir}' is non-empty and is not "
                    "a Git working tree; refusing to overwrite it.")
            endif()
            set(_new_repository TRUE)
        endif()
    else()
        get_filename_component(_repo_parent "${repo_dir}" DIRECTORY)
        file(MAKE_DIRECTORY "${_repo_parent}")
        set(_new_repository TRUE)
    endif()

    if(_new_repository)
        message(STATUS
            "[alp/git] ${name}: shallow-cloning '${PARAM_URL}' into '${repo_dir}'.")
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" clone --no-checkout --depth 1
                "${PARAM_URL}" "${repo_dir}"
            RESULT_VARIABLE _clone_result
            OUTPUT_VARIABLE _output
            ERROR_VARIABLE _error)
        if(NOT _clone_result EQUAL 0)
            _alp_add_repo_fail("${name}" "${repo_dir}" "${PARAM_COMMITISH}"
                "shallow clone" "${_error}${_output}")
        endif()
    else()
        # This must be the first Git command for an existing working tree. An
        # attached HEAD makes it developer managed and suppresses all other Git work.
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" symbolic-ref -q HEAD
            WORKING_DIRECTORY "${repo_dir}"
            RESULT_VARIABLE _attached_result
            OUTPUT_VARIABLE _attached_ref
            ERROR_VARIABLE _error
            OUTPUT_STRIP_TRAILING_WHITESPACE)
        if(_attached_result EQUAL 0)
            message(WARNING
                "[alp/git] ${name}: protected developer-managed repository at "
                "'${_normalized_destination}' is attached to '${_attached_ref}'. "
                "Requested revision '${PARAM_COMMITISH}' was NOT checked out; no other "
                "Git operations were performed.")
            set("${name}_SOURCE_DIR" "${repo_dir}" PARENT_SCOPE)
            if(NOT PARAM_DO_NOT_ADD_SUBPROJECT)
                if(PARAM_NOT_SYSTEM)
                    add_subdirectory("${repo_dir}" "${CMAKE_BINARY_DIR}/alp_external/${name}")
                else()
                    add_subdirectory("${repo_dir}" "${CMAKE_BINARY_DIR}/alp_external/${name}" SYSTEM)
                endif()
            endif()
            return()
        elseif(NOT _attached_result EQUAL 1)
            _alp_add_repo_fail("${name}" "${repo_dir}" "${PARAM_COMMITISH}"
                "classifying repository ownership" "${_error}")
        endif()

        execute_process(
            COMMAND "${GIT_EXECUTABLE}" remote get-url origin
            WORKING_DIRECTORY "${repo_dir}"
            RESULT_VARIABLE _origin_result
            OUTPUT_VARIABLE _origin_url
            ERROR_VARIABLE _error
            OUTPUT_STRIP_TRAILING_WHITESPACE)
        if(NOT _origin_result EQUAL 0)
            _alp_add_repo_fail("${name}" "${repo_dir}" "${PARAM_COMMITISH}"
                "reading origin URL" "${_error}")
        endif()
        if(NOT _origin_url STREQUAL PARAM_URL)
            _alp_add_repo_fail("${name}" "${repo_dir}" "${PARAM_COMMITISH}"
                "validating origin URL"
                "origin is '${_origin_url}', but URL is '${PARAM_URL}'. Attach HEAD to make this a developer-managed repository")
        endif()

        execute_process(
            COMMAND "${GIT_EXECUTABLE}" status --porcelain --untracked-files=all --ignored=no
            WORKING_DIRECTORY "${repo_dir}"
            RESULT_VARIABLE _status_result
            OUTPUT_VARIABLE _status
            ERROR_VARIABLE _error
            OUTPUT_STRIP_TRAILING_WHITESPACE)
        if(NOT _status_result EQUAL 0)
            _alp_add_repo_fail("${name}" "${repo_dir}" "${PARAM_COMMITISH}"
                "checking repository cleanliness" "${_error}")
        endif()
        if(NOT _status STREQUAL "")
            _alp_add_repo_fail("${name}" "${repo_dir}" "${PARAM_COMMITISH}"
                "checking repository cleanliness"
                "the CMake-managed repository is dirty:\n${_status}\nAttach HEAD to make this a developer-managed repository")
        endif()
    endif()

    string(REGEX MATCH "^origin/(.+)$" _remote_match "${PARAM_COMMITISH}")
    if(_remote_match)
        set(_branch "${CMAKE_MATCH_1}")
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" check-ref-format "refs/heads/${_branch}"
            WORKING_DIRECTORY "${repo_dir}"
            RESULT_VARIABLE _ref_result
            ERROR_VARIABLE _error)
        if(NOT _ref_result EQUAL 0)
            _alp_add_repo_fail("${name}" "${repo_dir}" "${PARAM_COMMITISH}"
                "validating remote-tracking revision" "${_error}")
        endif()

        message(STATUS
            "[alp/git] ${name}: fetching moving revision '${PARAM_COMMITISH}' in '${repo_dir}'.")
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" fetch --depth 1 origin
                "+refs/heads/${_branch}:refs/remotes/origin/${_branch}"
            WORKING_DIRECTORY "${repo_dir}"
            RESULT_VARIABLE _fetch_result
            OUTPUT_VARIABLE _output
            ERROR_VARIABLE _error)
        if(NOT _fetch_result EQUAL 0)
            _alp_add_repo_resolve_commit("${repo_dir}"
                "refs/remotes/origin/${_branch}" _target_commit)
            if(_target_commit STREQUAL "")
                _alp_add_repo_fail("${name}" "${repo_dir}" "${PARAM_COMMITISH}"
                    "refreshing remote-tracking revision" "${_error}${_output}")
            endif()
            string(STRIP "${_error}${_output}" _fetch_diagnostic)
            message(WARNING
                "[alp/git] ${name}: refresh of '${PARAM_COMMITISH}' failed in "
                "'${repo_dir}'; reusing cached commit '${_target_commit}'. "
                "Git diagnostic: ${_fetch_diagnostic}")
        else()
            _alp_add_repo_resolve_commit("${repo_dir}"
                "refs/remotes/origin/${_branch}" _target_commit)
            if(_target_commit STREQUAL "")
                _alp_add_repo_fail("${name}" "${repo_dir}" "${PARAM_COMMITISH}"
                    "resolving fetched remote-tracking revision" "the ref does not resolve to a commit")
            endif()
        endif()
    else()
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" rev-parse --verify --quiet
                "refs/heads/${PARAM_COMMITISH}"
            WORKING_DIRECTORY "${repo_dir}"
            RESULT_VARIABLE _local_branch_result
            OUTPUT_QUIET ERROR_QUIET)
        if(_local_branch_result EQUAL 0)
            _alp_add_repo_fail("${name}" "${repo_dir}" "${PARAM_COMMITISH}"
                "classifying fixed revision"
                "local branch names are not accepted; use a tag, commit ID, or origin/<branch>")
        endif()

        _alp_add_repo_resolve_commit("${repo_dir}" "${PARAM_COMMITISH}" _target_commit)
        if(_target_commit STREQUAL "")
            message(STATUS
                "[alp/git] ${name}: fixed revision '${PARAM_COMMITISH}' is not cached; "
                "fetching it into '${repo_dir}'.")
            string(LENGTH "${PARAM_COMMITISH}" _revision_length)
            if(PARAM_COMMITISH MATCHES "^[0-9a-fA-F]+$"
                    AND _revision_length GREATER_EQUAL 4
                    AND _revision_length LESS_EQUAL 40)
                execute_process(
                    COMMAND "${GIT_EXECUTABLE}" fetch --depth 1 origin "${PARAM_COMMITISH}"
                    WORKING_DIRECTORY "${repo_dir}"
                    RESULT_VARIABLE _fetch_result
                    OUTPUT_VARIABLE _output
                    ERROR_VARIABLE _error)
            else()
                execute_process(
                    COMMAND "${GIT_EXECUTABLE}" fetch --depth 1 origin
                        "refs/tags/${PARAM_COMMITISH}:refs/tags/${PARAM_COMMITISH}"
                    WORKING_DIRECTORY "${repo_dir}"
                    RESULT_VARIABLE _fetch_result
                    OUTPUT_VARIABLE _output
                    ERROR_VARIABLE _error)
            endif()
            if(NOT _fetch_result EQUAL 0)
                _alp_add_repo_fail("${name}" "${repo_dir}" "${PARAM_COMMITISH}"
                    "fetching fixed revision" "${_error}${_output}")
            endif()
            _alp_add_repo_resolve_commit("${repo_dir}" "${PARAM_COMMITISH}" _target_commit)
            if(_target_commit STREQUAL "")
                # A raw commit fetch is normally available only through FETCH_HEAD.
                _alp_add_repo_resolve_commit("${repo_dir}" FETCH_HEAD _target_commit)
            endif()
            if(_target_commit STREQUAL "")
                _alp_add_repo_fail("${name}" "${repo_dir}" "${PARAM_COMMITISH}"
                    "resolving fetched fixed revision" "the fetched object does not resolve to a commit")
            endif()
        else()
            message(STATUS
                "[alp/git] ${name}: fixed revision '${PARAM_COMMITISH}' is available locally in '${repo_dir}'.")
        endif()
    endif()

    set(_checkout_performed FALSE)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse --verify HEAD
        WORKING_DIRECTORY "${repo_dir}"
        RESULT_VARIABLE _head_result
        OUTPUT_VARIABLE _head_commit
        ERROR_QUIET
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(_new_repository OR NOT _head_result EQUAL 0 OR NOT _head_commit STREQUAL _target_commit)
        if(_head_result EQUAL 0 AND EXISTS "${repo_dir}/.gitmodules")
            # Deinitializing before switching prevents removed submodule worktrees
            # from becoming untracked files after the checkout.
            execute_process(
                COMMAND "${GIT_EXECUTABLE}" submodule deinit --force --all
                WORKING_DIRECTORY "${repo_dir}"
                RESULT_VARIABLE _deinit_result
                OUTPUT_VARIABLE _output
                ERROR_VARIABLE _error)
            if(NOT _deinit_result EQUAL 0)
                _alp_add_repo_fail("${name}" "${repo_dir}" "${PARAM_COMMITISH}"
                    "deinitializing old submodules before checkout" "${_error}${_output}")
            endif()
        endif()

        message(STATUS
            "[alp/git] ${name}: checking out '${PARAM_COMMITISH}' at '${_target_commit}' in '${repo_dir}'.")
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" checkout --quiet --detach "${_target_commit}"
            WORKING_DIRECTORY "${repo_dir}"
            RESULT_VARIABLE _checkout_result
            OUTPUT_VARIABLE _output
            ERROR_VARIABLE _error)
        if(NOT _checkout_result EQUAL 0)
            _alp_add_repo_fail("${name}" "${repo_dir}" "${PARAM_COMMITISH}"
                "checking out requested revision" "${_error}${_output}")
        endif()
        set(_checkout_performed TRUE)
    else()
        message(STATUS
            "[alp/git] ${name}: HEAD is already at '${PARAM_COMMITISH}' in '${repo_dir}'; skipping checkout.")
    endif()

    _alp_add_repo_prepare_submodules(
        "${name}" "${repo_dir}" "${PARAM_COMMITISH}" "${_checkout_performed}")
    _alp_add_repo_assert_postconditions(
        "${name}" "${repo_dir}" "${PARAM_COMMITISH}" "${_target_commit}")

    set("${name}_SOURCE_DIR" "${repo_dir}" PARENT_SCOPE)

    if(NOT PARAM_DO_NOT_ADD_SUBPROJECT)
        message(STATUS
            "[alp/git] ${name}: adding '${repo_dir}' as a CMake subproject.")
        if(PARAM_NOT_SYSTEM)
            add_subdirectory("${repo_dir}" "${CMAKE_BINARY_DIR}/alp_external/${name}")
        else()
            add_subdirectory("${repo_dir}" "${CMAKE_BINARY_DIR}/alp_external/${name}" SYSTEM)
        endif()
    endif()
endfunction()
