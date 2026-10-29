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

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(QIQIIME_ARCH_NAME "x64")
elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(QIQIIME_ARCH_NAME "x86")
else()
    set(QIQIIME_ARCH_NAME "${CMAKE_CXX_COMPILER_ARCHITECTURE_ID}")
endif()
message(STATUS "QiQiIME target architecture: ${QIQIIME_ARCH_NAME} (${CMAKE_VS_PLATFORM_NAME})")

# Unify build artifact output directory across subdirectories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

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

# Build source and test subdirectories
enable_testing()
add_subdirectory(src)
add_subdirectory(tests)
