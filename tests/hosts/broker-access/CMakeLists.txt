# SPDX-FileCopyrightText: 2026 cloudfyy
# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 4.0)
project(QiQiImeAppContainerPermissions LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT MSVC OR NOT CMAKE_GENERATOR MATCHES "Visual Studio")
    message(FATAL_ERROR "This standalone host requires an MSVC Visual Studio generator.")
endif()

if(NOT CMAKE_GENERATOR_PLATFORM STREQUAL "x64")
    message(FATAL_ERROR "The LPAC host is currently x64 only.")
endif()

add_library(qiqi_lpac_token_info STATIC
    ../common/tokeninfo.cpp)
target_include_directories(qiqi_lpac_token_info PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../common")
target_compile_definitions(qiqi_lpac_token_info PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX)
target_compile_options(qiqi_lpac_token_info PRIVATE
    /W4
    /permissive-
    /EHsc
    /utf-8
    "$<$<CONFIG:Debug>:/Od>"
    "$<$<NOT:$<CONFIG:Debug>>:/O1;/Gy;/Gw;/GL>")
set_property(TARGET qiqi_lpac_token_info PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include("${CMAKE_CURRENT_SOURCE_DIR}/../HostIcon.cmake")

function(qiqi_add_permission_host target executable display_name
    background badge_color badge_text_color badge_text)
    add_executable("${target}" WIN32
        host.cpp
        ../common/brokerprobe.cpp)
    target_include_directories("${target}" PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../common"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../src")
    target_compile_definitions("${target}" PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        "QIQI_HOST_EXECUTABLE_NAME=L\"${executable}\""
        "QIQI_HOST_DISPLAY_NAME=L\"${display_name}\"")
    target_compile_options("${target}" PRIVATE
        /W4
        /permissive-
        /EHsc
        /bigobj
        /utf-8
        "$<$<CONFIG:Debug>:/Od>"
        "$<$<NOT:$<CONFIG:Debug>>:/O1;/Gy;/Gw;/GL>")
    target_link_options("${target}" PRIVATE
        /SUBSYSTEM:WINDOWS
        /MANIFEST:NO
        "$<$<NOT:$<CONFIG:Debug>>:/LTCG;/OPT:REF;/OPT:ICF>")
    target_link_libraries("${target}" PRIVATE
        qiqi_lpac_token_info
        Advapi32
        Comctl32
        Gdi32
        Ole32
        OleAut32
        Shell32
        User32
        Uuid)
    set_property(TARGET "${target}" PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    qiqi_add_host_window_icon(
        TARGET "${target}"
        GLYPH "${CMAKE_CURRENT_SOURCE_DIR}/../../../data/qiqiime-dark.ico"
        BACKGROUND "${background}"
        BADGE_COLOR "${badge_color}"
        BADGE_TEXT_COLOR "${badge_text_color}"
        BADGE_TEXT "${badge_text}"
        BADGE_SHAPE "Circle")
endfunction()

qiqi_add_permission_host(
    QiQiImeBroadGroupHost
    "QiQiImeBroadGroupHost.exe"
    "QiQiIME Broad Group COM Host"
    "#2563EB"
    "#DBEAFE"
    "#1E3A8A"
    "B")
qiqi_add_permission_host(
    QiQiImePackageOnlyHost
    "QiQiImePackageOnlyHost.exe"
    "QiQiIME Package-only COM Host"
    "#6D28D9"
    "#DDD6FE"
    "#2E1065"
    "P")

add_executable(QiQiImePermissionLauncher launcher.cpp)
target_compile_definitions(QiQiImePermissionLauncher PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX)
target_compile_options(QiQiImePermissionLauncher PRIVATE
    /W4
    /permissive-
    /EHsc
    /utf-8
    "$<$<CONFIG:Debug>:/Od>"
    "$<$<NOT:$<CONFIG:Debug>>:/O1;/Gy;/Gw;/GL>")
target_link_options(QiQiImePermissionLauncher PRIVATE
    /SUBSYSTEM:CONSOLE
    /MANIFEST:NO
    "$<$<NOT:$<CONFIG:Debug>>:/LTCG;/OPT:REF;/OPT:ICF>")
target_link_libraries(QiQiImePermissionLauncher PRIVATE
    Advapi32
    Kernel32
    Ole32
    OneCoreUAP
    Shell32
    Userenv)
set_property(TARGET QiQiImePermissionLauncher PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

enable_testing()
find_program(QIQI_LPAC_POWERSHELL_EXECUTABLE
    NAMES pwsh.exe powershell.exe
    REQUIRED)
add_test(
    NAME QiQiImeAppContainerPermissionSmoke
    COMMAND "${QIQI_LPAC_POWERSHELL_EXECUTABLE}"
        -NoLogo
        -NoProfile
        -NonInteractive
        -File "${CMAKE_CURRENT_SOURCE_DIR}/Run-Host.ps1"
        -LauncherPath "$<TARGET_FILE:QiQiImePermissionLauncher>"
        -SmokeAll)
set_tests_properties(QiQiImeAppContainerPermissionSmoke PROPERTIES
    TIMEOUT 30)
