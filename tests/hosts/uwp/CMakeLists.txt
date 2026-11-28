# SPDX-FileCopyrightText: 2026 cloudfyy
# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 4.0)
project(QiQiImeAppContainerSearchHost LANGUAGES CXX RC)

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
add_custom_target(qiqi_generate_appcontainer_search_projection
    DEPENDS "${QIQI_WINRT_PROJECTION}/winrt/Windows.UI.Xaml.h")

add_library(uwp-core OBJECT
    ../common/tokeninfo.cpp
    ../common/brokerprobe.cpp
    fixtures.cpp
    hostapp.cpp
    main.cpp
)
add_dependencies(uwp-core
    qiqi_generate_appcontainer_search_projection)

target_include_directories(uwp-core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../common"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../src"
    "${QIQI_WINRT_PROJECTION}")
target_compile_definitions(uwp-core PUBLIC
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    WINRT_LEAN_AND_MEAN
    NOMINMAX
    WINAPI_FAMILY=WINAPI_FAMILY_APP)
target_compile_options(uwp-core PUBLIC
    /W4
    /permissive-
    /EHsc
    /bigobj
    /utf-8
    "$<$<CONFIG:Debug>:/Od>"
    "$<$<NOT:$<CONFIG:Debug>>:/O1;/Gy;/Gw;/GL>")
target_link_options(uwp-core INTERFACE
    /APPCONTAINER
    /SUBSYSTEM:WINDOWS
    /MANIFEST:NO
    "$<$<NOT:$<CONFIG:Debug>>:/LTCG;/OPT:REF;/OPT:ICF>")
target_link_libraries(uwp-core PUBLIC WindowsApp.lib)
set_property(TARGET uwp-core PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include("${CMAKE_CURRENT_SOURCE_DIR}/../HostIcon.cmake")
foreach(profile IN ITEMS zero com reg)
    set(target "uwp-${profile}")
    add_executable("${target}" WIN32 diagnostics.cpp)
    target_link_libraries("${target}" PRIVATE uwp-core)
    target_compile_definitions("${target}" PRIVATE "QIQI_HOST_PROCESS_NAME=L\"${target}.exe\"")
    set_property(TARGET "${target}" PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    qiqi_add_host_window_icon(TARGET "${target}" KIND "${profile}")
endforeach()
