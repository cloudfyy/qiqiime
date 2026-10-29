set(QIQIIME_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/common/version.h.in"
    "${QIQIIME_GENERATED_DIR}/common/version.h"
    @ONLY
)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/common/version.rc.in"
    "${QIQIIME_GENERATED_DIR}/common/version.rc"
    @ONLY
)

# Core Wubi 86 Engine (Static library, pure C++26 + Win32 UTF conversion, testable independently)
set(QIQI_ENGINE_SOURCES
    common/config.cpp
    common/config.h
    common/logger.cpp
    common/logger.h
    engine/wubi_fsm.h
    engine/wubi_engine.cpp
    engine/wubi_engine.h
    engine/wubi_default_dict.h
)
add_library(qiqi_engine STATIC
    ${QIQI_ENGINE_SOURCES}
    "${QIQIIME_GENERATED_DIR}/common/version.h"
)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${QIQI_ENGINE_SOURCES})
target_include_directories(qiqi_engine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${QIQIIME_GENERATED_DIR}
)
target_compile_definitions(qiqi_engine PUBLIC SPDLOG_USE_STD_FORMAT SPDLOG_WCHAR_FILENAMES)
target_link_libraries(qiqi_engine PUBLIC spdlog::spdlog_header_only)

# TSF Text Input Processor COM DLL
set(QIQIIME_SOURCES
    common/guids.h
    tsf/dllmain.cpp
    tsf/class_factory.h
    tsf/class_factory.cpp
    tsf/register.h
    tsf/register.cpp
    tsf/tip_service.h
    tsf/tip_service.cpp
    tsf/edit_session.h
    tsf/qiqiime.def
    ui/candidate_ui.h
    ui/candidate_ui.cpp
)
add_library(qiqiime SHARED
    ${QIQIIME_SOURCES}
    "${QIQIIME_GENERATED_DIR}/common/version.rc"
)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${QIQIIME_SOURCES})
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

# Copy Wubi 86 dictionaries (Jidian, Jidian Extra, and Haifeng) and default config.ini next to build outputs
add_custom_command(TARGET qiqiime POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${PROJECT_SOURCE_DIR}/data/config.ini"
        "$<TARGET_FILE_DIR:qiqiime>/config.ini"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${PROJECT_SOURCE_DIR}/data/wubi86_jidian.txt"
        "$<TARGET_FILE_DIR:qiqiime>/wubi86_jidian.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${PROJECT_SOURCE_DIR}/data/wubi86_jidian_extra.txt"
        "$<TARGET_FILE_DIR:qiqiime>/wubi86_jidian_extra.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${PROJECT_SOURCE_DIR}/data/wubi86_haifeng.txt"
        "$<TARGET_FILE_DIR:qiqiime>/wubi86_haifeng.txt"
)
