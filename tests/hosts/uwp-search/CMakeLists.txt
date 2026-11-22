# SPDX-FileCopyrightText: 2026 cloudfyy
# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 4.0)
project(QiQiImeUwpSearchHost LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT MSVC OR NOT CMAKE_GENERATOR MATCHES "Visual Studio")
    message(FATAL_ERROR "This standalone host requires an MSVC Visual Studio generator.")
endif()

if(NOT CMAKE_GENERATOR_PLATFORM STREQUAL "x64")
    message(FATAL_ERROR "The development MSIX is currently built for x64 only.")
endif()

set(QIQI_WINDOWS_KITS_ROOT
    "$ENV{ProgramFiles\(x86\)}/Windows Kits/10"
    CACHE PATH "Windows 10/11 SDK root")
set(QIQI_WINDOWS_SDK_VERSION
    "${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}"
    CACHE STRING "Windows SDK version")

if(QIQI_WINDOWS_SDK_VERSION STREQUAL "")
    message(FATAL_ERROR "CMake did not select a Windows SDK version.")
endif()

find_program(QIQI_CPPWINRT_EXECUTABLE
    NAMES cppwinrt.exe
    PATHS "${QIQI_WINDOWS_KITS_ROOT}/bin/${QIQI_WINDOWS_SDK_VERSION}/x64"
    REQUIRED)

set(QIQI_WINRT_PROJECTION "${CMAKE_BINARY_DIR}/generated")
add_custom_command(
    OUTPUT "${QIQI_WINRT_PROJECTION}/winrt/Windows.UI.Xaml.h"
    COMMAND "${QIQI_CPPWINRT_EXECUTABLE}"
        -input "${QIQI_WINDOWS_SDK_VERSION}+"
        -include Windows.ApplicationModel.Activation
        -include Windows.ApplicationModel.Search
        -include Windows.Data.Json
        -include Windows.Storage
        -include Windows.UI.Text
        -include Windows.UI.Xaml
        -output "${QIQI_WINRT_PROJECTION}"
        -pch .
        -overwrite
    COMMENT "Generating the focused C++/WinRT projection"
    VERBATIM)
add_custom_target(qiqi_generate_uwp_search_projection
    DEPENDS "${QIQI_WINRT_PROJECTION}/winrt/Windows.UI.Xaml.h")

add_executable(QiQiImeUwpSearchHost WIN32
    diagnostics.cpp
    fixtures.cpp
    hostapp.cpp
    main.cpp
)
add_dependencies(QiQiImeUwpSearchHost qiqi_generate_uwp_search_projection)

target_include_directories(QiQiImeUwpSearchHost PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${QIQI_WINRT_PROJECTION}")
target_compile_definitions(QiQiImeUwpSearchHost PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    WINRT_LEAN_AND_MEAN
    NOMINMAX
    WINAPI_FAMILY=WINAPI_FAMILY_APP)
target_compile_options(QiQiImeUwpSearchHost PRIVATE
    /W4
    /permissive-
    /EHsc
    /bigobj
    /utf-8
    "$<$<CONFIG:Debug>:/Od>"
    "$<$<NOT:$<CONFIG:Debug>>:/O1;/Gy;/Gw;/GL>")
target_link_options(QiQiImeUwpSearchHost PRIVATE
    /APPCONTAINER
    /SUBSYSTEM:WINDOWS
    /MANIFEST:NO
    "$<$<NOT:$<CONFIG:Debug>>:/LTCG;/OPT:REF;/OPT:ICF>")
target_link_libraries(QiQiImeUwpSearchHost PRIVATE WindowsApp.lib)
set_property(TARGET QiQiImeUwpSearchHost PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
