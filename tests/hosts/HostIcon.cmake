# SPDX-FileCopyrightText: 2026 cloudfyy
# SPDX-License-Identifier: GPL-3.0-or-later

include_guard(GLOBAL)

function(qiqi_add_host_window_icon)
    cmake_parse_arguments(ARG "" "TARGET;KIND" "" ${ARGN})
    foreach(required IN ITEMS TARGET KIND)
        if(NOT ARG_${required})
            message(FATAL_ERROR
                "qiqi_add_host_window_icon requires ${required}.")
        endif()
    endforeach()
    if(NOT TARGET "${ARG_TARGET}")
        message(FATAL_ERROR
            "Unknown host icon target: ${ARG_TARGET}")
    endif()

    find_program(QIQI_HOST_POWERSHELL_EXECUTABLE
        NAMES pwsh.exe powershell.exe
        REQUIRED)
    set(output_directory
        "${CMAKE_CURRENT_BINARY_DIR}/generated/icon")
    set(QIQI_HOST_WINDOW_ICON
        "${output_directory}/${ARG_KIND}.ico")
    set(resource_file
        "${output_directory}/${ARG_KIND}.rc")
    file(MAKE_DIRECTORY "${output_directory}")

    add_custom_command(
        OUTPUT "${QIQI_HOST_WINDOW_ICON}"
        COMMAND "${QIQI_HOST_POWERSHELL_EXECUTABLE}"
            -NoLogo
            -NoProfile
            -NonInteractive
            -File
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/Icon.ps1"
            -OutputPath "${QIQI_HOST_WINDOW_ICON}"
            -Kind "${ARG_KIND}"
        DEPENDS "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/Icon.ps1"
        VERBATIM)
    file(TO_CMAKE_PATH
        "${QIQI_HOST_WINDOW_ICON}"
        QIQI_HOST_WINDOW_ICON)
    configure_file(
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/HostIcon.rc.in"
        "${resource_file}"
        @ONLY)
    set_source_files_properties(
        "${resource_file}"
        PROPERTIES
            GENERATED TRUE
            OBJECT_DEPENDS "${QIQI_HOST_WINDOW_ICON}")
    add_custom_target("${ARG_TARGET}-window-icon"
        DEPENDS "${QIQI_HOST_WINDOW_ICON}")
    add_dependencies(
        "${ARG_TARGET}"
        "${ARG_TARGET}-window-icon")
    target_sources("${ARG_TARGET}"
        PRIVATE "${resource_file}")
endfunction()
