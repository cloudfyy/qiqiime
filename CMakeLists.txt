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

project(QiQiIME VERSION 0.1.0 LANGUAGES CXX)

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
add_library(qiqi_engine STATIC
    src/logger.cpp
    src/logger.h
    src/wubi_engine.cpp
    src/wubi_engine.h
    src/wubi_default_dict.h
)
target_include_directories(qiqi_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(qiqi_engine PUBLIC SPDLOG_USE_STD_FORMAT SPDLOG_WCHAR_FILENAMES)
target_link_libraries(qiqi_engine PUBLIC spdlog::spdlog_header_only)

# TSF Text Input Processor COM DLL
add_library(qiqiime SHARED
    src/guids.h
    src/dllmain.cpp
    src/class_factory.h
    src/class_factory.cpp
    src/register.h
    src/register.cpp
    src/tip_service.h
    src/tip_service.cpp
    src/edit_session.h
    src/candidate_ui.h
    src/candidate_ui.cpp
    src/qiqiime.def
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
