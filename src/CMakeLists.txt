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

# Common infrastructure library (config, file/UTF helpers, lock-free ETW logging, RAII wrappers)
set(QIQI_COMMON_SOURCES
    common/config.cpp
    common/config.h
    common/file_util.cpp
    common/file_util.h
    common/d2d_util.h
    common/logger.cpp
    common/logger.h
    common/scoped_handle.h
    common/win32_ui_util.h
)
add_library(qiqi_common STATIC
    ${QIQI_COMMON_SOURCES}
    "${QIQIIME_GENERATED_DIR}/common/version.h"
)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${QIQI_COMMON_SOURCES})
target_include_directories(qiqi_common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${QIQIIME_GENERATED_DIR}
)
target_link_libraries(qiqi_common PUBLIC
    Microsoft.GSL::GSL
    advapi32
)

# Core Wubi 86 Engine (Static library, pure C++26 + Win32 UTF conversion, testable independently)
set(QIQI_ENGINE_SOURCES
    engine/wubi_binary_dict.cpp
    engine/wubi_binary_dict.h
    engine/wubi_fsm.h
    engine/wubi_engine.cpp
    engine/wubi_engine.h
    engine/wubi_default_dict.h
)
add_library(qiqi_engine STATIC
    ${QIQI_ENGINE_SOURCES}
)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${QIQI_ENGINE_SOURCES})
target_link_libraries(qiqi_engine PUBLIC
    qiqi_common
)

# TSF Text Input Processor COM DLL
set(QIQIIME_SOURCES
    common/com_object.h
    common/guids.h
    tsf/dllmain.cpp
    tsf/class_factory.h
    tsf/class_factory.cpp
    tsf/register.h
    tsf/register.cpp
    tsf/tip_service.h
    tsf/tip_service.cpp
    tsf/display_attribute.h
    tsf/display_attribute.cpp
    tsf/edit_session.h
    tsf/qiqiime.def
    ui/candidate_ui.h
    ui/candidate_ui.cpp
    ui/about_dialog.cpp
    ui/dialog_util.h
    ui/d2d_controls.h
    ui/d2d_controls.cpp
    ui/langbar_button.h
    ui/langbar_button.cpp
    ui/popup_menu.cpp
    ui/settings_dialog.h
    ui/settings_dialog.cpp
    ui/settings_dialog_controls.cpp
    ui/settings_dialog_internal.h
    ui/status_bar.h
    ui/status_bar.cpp
)
add_library(qiqiime SHARED
    ${QIQIIME_SOURCES}
    "${QIQIIME_GENERATED_DIR}/common/version.rc"
)
set_source_files_properties("${QIQIIME_GENERATED_DIR}/common/version.rc" PROPERTIES
    OBJECT_DEPENDS "${PROJECT_SOURCE_DIR}/data/qiqiime.ico"
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
    comctl32
    d2d1
    dwrite
    shell32
    ole32
    oleaut32
    advapi32
    uuid
)

# Copy Wubi 86 dictionaries (Jidian, Jidian Extra, and Haifeng) next to build outputs
add_custom_command(TARGET qiqiime POST_BUILD
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

# Standalone Direct2D Fluent UI Settings executable (QiQiIMESettings.exe)
add_executable(QiQiIMESettings WIN32
    ui/settings_main.cpp
    ui/about_dialog.cpp
    ui/dialog_util.h
    ui/settings_dialog.cpp
    ui/settings_dialog_controls.cpp
    ui/settings_dialog_internal.h
    ui/settings_dialog.h
    ui/d2d_controls.h
    ui/d2d_controls.cpp
    common/guids.h
    "${QIQIIME_GENERATED_DIR}/common/version.rc"
)
target_link_libraries(QiQiIMESettings PRIVATE
    qiqi_common
    user32
    gdi32
    comctl32
    d2d1
    dwrite
    shell32
    ole32
    oleaut32
    advapi32
    uuid
)
set_target_properties(QiQiIMESettings PROPERTIES
    OUTPUT_NAME "QiQiIMESettings"
)
