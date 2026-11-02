set(QIQI_RESOURCE_FILE_TYPE VFT_DLL)
set(QIQI_RESOURCE_FILE_SUBTYPE VFT2_DRV_INPUTMETHOD)
set(QIQI_RESOURCE_FILENAME "qiqiime.dll")
set(QIQI_RESOURCE_DESCRIPTION "奇奇五笔输入法 (QiQi Wubi 86 TSF IME - ${QIQIIME_ARCH_NAME})")
set(QIQI_RESOURCE_COMMENTS "奇奇五笔 (QiQi Wubi 86 TSF IME) - https://github.com/cloudfyy/qiqiime")

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/version.h.in"
    "${QIQIIME_GENERATED_DIR}/common/version.h"
    @ONLY
)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/version.rc.in"
    "${QIQIIME_GENERATED_DIR}/common/version.rc"
    @ONLY
)

set(QIQI_COMMON_SOURCES
    about_dialog.cpp
    about_dialog.h
    com_object.h
    config.cpp
    config.h
    d2d_controls.cpp
    d2d_controls.h
    d2d_util.h
    dialog_util.h
    etw_registration.h
    file_util.cpp
    file_util.h
    guids.h
    i18n.h
    logger.cpp
    logger.h
    monitor_dpi.h
    scoped_handle.h
    sound.h
    theme.h
    version.h.in
    version.rc.in
    win32_ui_util.h
)
add_library(qiqi_common STATIC
    ${QIQI_COMMON_SOURCES}
    "${QIQIIME_GENERATED_DIR}/common/version.h"
)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${QIQI_COMMON_SOURCES})
target_include_directories(qiqi_common PUBLIC
    "${PROJECT_SOURCE_DIR}/src"
    "${QIQIIME_GENERATED_DIR}"
)
target_link_libraries(qiqi_common PUBLIC
    Microsoft.GSL::GSL
    advapi32
    ktmw32
    user32
    gdi32
    comctl32
    d2d1
    dwrite
    shell32
)
