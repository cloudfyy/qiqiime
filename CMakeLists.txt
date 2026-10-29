cmake_minimum_required(VERSION 3.20)
project(QiQiIME VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_definitions(UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)

if(MSVC)
    add_compile_options(/W4 /utf-8 /permissive- /EHsc)
endif()

# Core Wubi 86 Engine (Static library, pure C++20 + Win32 UTF conversion, testable independently)
add_library(qiqi_engine STATIC
    src/wubi_engine.cpp
    src/wubi_engine.h
    src/wubi_default_dict.h
)
target_include_directories(qiqi_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

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
