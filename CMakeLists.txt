cmake_minimum_required(VERSION 3.25)

# Automatically locate vcpkg toolchain if not explicitly provided
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    if(DEFINED ENV{VCPKG_ROOT} AND EXISTS "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
        set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "Vcpkg toolchain file")
    else()
        foreach(_vs_vcpkg_candidate
            "$ENV{ProgramFiles}/Microsoft Visual Studio/18/Enterprise/VC/vcpkg/scripts/buildsystems/vcpkg.cmake"
            "$ENV{ProgramFiles}/Microsoft Visual Studio/18/Professional/VC/vcpkg/scripts/buildsystems/vcpkg.cmake"
            "$ENV{ProgramFiles}/Microsoft Visual Studio/18/Community/VC/vcpkg/scripts/buildsystems/vcpkg.cmake"
            "$ENV{ProgramFiles}/Microsoft Visual Studio/2022/Enterprise/VC/vcpkg/scripts/buildsystems/vcpkg.cmake"
            "$ENV{ProgramFiles}/Microsoft Visual Studio/2022/Professional/VC/vcpkg/scripts/buildsystems/vcpkg.cmake"
            "$ENV{ProgramFiles}/Microsoft Visual Studio/2022/Community/VC/vcpkg/scripts/buildsystems/vcpkg.cmake"
        )
            if(EXISTS "${_vs_vcpkg_candidate}")
                set(CMAKE_TOOLCHAIN_FILE "${_vs_vcpkg_candidate}" CACHE STRING "Vcpkg toolchain file")
                break()
            endif()
        endforeach()
    endif()
endif()

# Read version from VERSION file (single source of truth)
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" QIQIIME_VERSION_RAW LIMIT_COUNT 1)
string(STRIP "${QIQIIME_VERSION_RAW}" QIQIIME_VERSION_STRING)

project(QiQiIME VERSION ${QIQIIME_VERSION_STRING} LANGUAGES CXX)

if(DEFINED PROJECT_VERSION_TWEAK AND NOT "${PROJECT_VERSION_TWEAK}" STREQUAL "")
    set(QIQIIME_VERSION_TWEAK ${PROJECT_VERSION_TWEAK})
else()
    set(QIQIIME_VERSION_TWEAK 0)
endif()

# Query short Git commit hash for build metadata
execute_process(
    COMMAND git rev-parse --short=7 HEAD
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE QIQIIME_GIT_COMMIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT QIQIIME_GIT_COMMIT_HASH)
    set(QIQIIME_GIT_COMMIT_HASH "unknown")
endif()
set(QIQIIME_FULL_VERSION "${PROJECT_VERSION}+${QIQIIME_GIT_COMMIT_HASH}")

set(QIQIIME_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/common/version.h.in"
    "${QIQIIME_GENERATED_DIR}/common/version.h"
    @ONLY
)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/common/version.rc.in"
    "${QIQIIME_GENERATED_DIR}/common/version.rc"
    @ONLY
)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_definitions(UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)

if(MSVC)
    # CMake does not yet map MSVC's C++26 draft mode.
    if(NOT DEFINED CMAKE_CXX26_STANDARD_COMPILE_OPTION)
        set(CMAKE_CXX26_STANDARD_COMPILE_OPTION "/std:c++latest")
        set(CMAKE_CXX26_EXTENSION_COMPILE_OPTION "/std:c++latest")
        list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES
            CMAKE_CXX26_STANDARD_COMPILE_OPTION
            CMAKE_CXX26_EXTENSION_COMPILE_OPTION
        )
    endif()
    add_compile_options(/W4 /utf-8 /permissive- /EHsc)
endif()

# Dependencies managed via vcpkg (vcpkg.json)
find_package(spdlog CONFIG REQUIRED)

# Core Wubi 86 Engine (Static library, pure C++26 + Win32 UTF conversion, testable independently)
set(QIQI_ENGINE_SOURCES
    src/common/logger.cpp
    src/common/logger.h
    src/engine/wubi_engine.cpp
    src/engine/wubi_engine.h
    src/engine/wubi_default_dict.h
)
add_library(qiqi_engine STATIC
    ${QIQI_ENGINE_SOURCES}
    "${QIQIIME_GENERATED_DIR}/common/version.h"
)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" FILES ${QIQI_ENGINE_SOURCES})
target_include_directories(qiqi_engine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${QIQIIME_GENERATED_DIR}
)
target_compile_definitions(qiqi_engine PUBLIC SPDLOG_USE_STD_FORMAT SPDLOG_WCHAR_FILENAMES)
target_link_libraries(qiqi_engine PUBLIC spdlog::spdlog_header_only)

# TSF Text Input Processor COM DLL
set(QIQIIME_SOURCES
    src/common/guids.h
    src/tsf/dllmain.cpp
    src/tsf/class_factory.h
    src/tsf/class_factory.cpp
    src/tsf/register.h
    src/tsf/register.cpp
    src/tsf/tip_service.h
    src/tsf/tip_service.cpp
    src/tsf/edit_session.h
    src/tsf/qiqiime.def
    src/ui/candidate_ui.h
    src/ui/candidate_ui.cpp
)
add_library(qiqiime SHARED
    ${QIQIIME_SOURCES}
    "${QIQIIME_GENERATED_DIR}/common/version.rc"
)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" FILES ${QIQIIME_SOURCES})
set_target_properties(qiqiime PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

target_link_libraries(qiqiime PRIVATE
    qiqi_engine
    user32
    gdi32
    ole32
    oleaut32
    advapi32
    uuid
)

# Copy default dictionary table next to build outputs
add_custom_command(TARGET qiqiime POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/data/wubi86.txt"
        "$<TARGET_FILE_DIR:qiqiime>/wubi86.txt"
)

# Engine & State Machine Unit Tests
enable_testing()
add_executable(qiqi_engine_test
    tests/engine_test.cpp
)
target_link_libraries(qiqi_engine_test PRIVATE qiqi_engine)
add_test(NAME QiQiEngineTest COMMAND qiqi_engine_test)

# TSF COM Service & Candidate UI Unit Tests
add_executable(qiqi_tsf_ui_test
    tests/tsf_ui_test.cpp
    src/tsf/dllmain.cpp
    src/tsf/class_factory.cpp
    src/tsf/register.cpp
    src/tsf/tip_service.cpp
    src/ui/candidate_ui.cpp
)
target_link_libraries(qiqi_tsf_ui_test PRIVATE
    qiqi_engine
    user32
    gdi32
    ole32
    oleaut32
    advapi32
    uuid
)
add_test(NAME QiQiTsfUiTest COMMAND qiqi_tsf_ui_test)
