# SPDX-FileCopyrightText: 2026 cloudfyy
# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 4.0)
project(QiQiImeLpacHost LANGUAGES CXX RC)

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

add_executable(QiQiImeLpacHost WIN32 host.cpp)
target_include_directories(QiQiImeLpacHost PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../common")
target_compile_definitions(QiQiImeLpacHost PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX)
target_compile_options(QiQiImeLpacHost PRIVATE
    /W4
    /permissive-
    /EHsc
    /bigobj
    /utf-8
    "$<$<CONFIG:Debug>:/Od>"
    "$<$<NOT:$<CONFIG:Debug>>:/O1;/Gy;/Gw;/GL>")
target_link_options(QiQiImeLpacHost PRIVATE
    /SUBSYSTEM:WINDOWS
    /MANIFEST:NO
    "$<$<NOT:$<CONFIG:Debug>>:/LTCG;/OPT:REF;/OPT:ICF>")
target_link_libraries(QiQiImeLpacHost PRIVATE
    qiqi_lpac_token_info
    Advapi32
    Comctl32
    Gdi32
    Ole32
    OleAut32
    Shell32
    User32
    Uuid)
set_property(TARGET QiQiImeLpacHost PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include("${CMAKE_CURRENT_SOURCE_DIR}/../HostIcon.cmake")
qiqi_add_host_window_icon(
    TARGET QiQiImeLpacHost
    GLYPH "${CMAKE_CURRENT_SOURCE_DIR}/../../../data/qiqiime-dark.ico"
    BACKGROUND "#6D28D9"
    BADGE_COLOR "#DDD6FE"
    BADGE_TEXT_COLOR "#2E1065"
    BADGE_TEXT "L"
    BADGE_SHAPE "Circle")

add_executable(QiQiImeLpacLauncher launcher.cpp)
target_compile_definitions(QiQiImeLpacLauncher PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX)
target_compile_options(QiQiImeLpacLauncher PRIVATE
    /W4
    /permissive-
    /EHsc
    /utf-8
    "$<$<CONFIG:Debug>:/Od>"
    "$<$<NOT:$<CONFIG:Debug>>:/O1;/Gy;/Gw;/GL>")
target_link_options(QiQiImeLpacLauncher PRIVATE
    /SUBSYSTEM:CONSOLE
    /MANIFEST:NO
    "$<$<NOT:$<CONFIG:Debug>>:/LTCG;/OPT:REF;/OPT:ICF>")
target_link_libraries(QiQiImeLpacLauncher PRIVATE
    Advapi32
    Kernel32
    Ole32
    OneCoreUAP
    Shell32
    Userenv)
set_property(TARGET QiQiImeLpacLauncher PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

enable_testing()
find_program(QIQI_LPAC_POWERSHELL_EXECUTABLE
    NAMES pwsh.exe powershell.exe
    REQUIRED)
add_test(
    NAME QiQiImeLpacSmoke
    COMMAND "${QIQI_LPAC_POWERSHELL_EXECUTABLE}"
        -NoLogo
        -NoProfile
        -NonInteractive
        -File "${CMAKE_CURRENT_SOURCE_DIR}/Run-Host.ps1"
        -LauncherPath "$<TARGET_FILE:QiQiImeLpacLauncher>"
        -SmokeAll)
set_tests_properties(QiQiImeLpacSmoke PROPERTIES
    TIMEOUT 30)
