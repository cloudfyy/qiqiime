# SPDX-FileCopyrightText: 2026 cloudfyy
# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 4.0)
project(QiQiImePackagedFullTrustHost LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT MSVC OR NOT CMAKE_GENERATOR MATCHES "Visual Studio")
    message(FATAL_ERROR "This standalone host requires an MSVC Visual Studio generator.")
endif()

if(NOT CMAKE_GENERATOR_PLATFORM STREQUAL "x64")
    message(FATAL_ERROR "The packaged full-trust test host is currently x64 only.")
endif()

add_executable(QiQiImePackagedFullTrustHost WIN32
    diagnostics.cpp
    hoststate.cpp
    main.cpp
    searchprobe.cpp
    window.cpp
)

target_include_directories(QiQiImePackagedFullTrustHost PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../src")
target_compile_definitions(QiQiImePackagedFullTrustHost PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX)
target_compile_options(QiQiImePackagedFullTrustHost PRIVATE
    /W4
    /permissive-
    /EHsc
    /bigobj
    /utf-8
    "$<$<CONFIG:Debug>:/Od>"
    "$<$<NOT:$<CONFIG:Debug>>:/O1;/Gy;/Gw;/GL>")
target_link_options(QiQiImePackagedFullTrustHost PRIVATE
    /APPCONTAINER:NO
    /SUBSYSTEM:WINDOWS
    /MANIFEST:NO
    "$<$<NOT:$<CONFIG:Debug>>:/LTCG;/OPT:REF;/OPT:ICF>")
target_link_libraries(QiQiImePackagedFullTrustHost PRIVATE
    Comctl32
    Ole32
    OleAut32
    Advapi32
    Shell32
    User32
    Gdi32)
set_property(TARGET QiQiImePackagedFullTrustHost PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include("${CMAKE_CURRENT_SOURCE_DIR}/../HostIcon.cmake")
qiqi_add_host_window_icon(
    TARGET QiQiImePackagedFullTrustHost
    GLYPH "${CMAKE_CURRENT_SOURCE_DIR}/../../../data/qiqiime-light.ico"
    BACKGROUND "#9A3412"
    BADGE_COLOR "#FDBA74"
    BADGE_TEXT_COLOR "#2D1600"
    BADGE_TEXT "F"
    BADGE_SHAPE "Square")

