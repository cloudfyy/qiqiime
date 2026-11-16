# SPDX-FileCopyrightText: 2026 cloudfyy
# SPDX-License-Identifier: GPL-3.0-or-later

set(QIQI_RESOURCE_FILE_TYPE VFT_DLL)
set(QIQI_RESOURCE_FILE_SUBTYPE VFT2_DRV_INPUTMETHOD)
set(QIQI_RESOURCE_FILENAME "qiqiime.dll")
if(QIQI_METRICS_BENCHMARK_BASELINE)
    set(QIQI_RESOURCE_FILENAME "qiqiime_metrics_baseline.dll")
endif()
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
    dialog.cpp
    dialog.h
    dictcodec.h
    comobj.h
    changesync.cpp
    changesync.h
    config.cpp
    config.h
    cfgjson.cpp
    cfgjson.h
    controls.cpp
    controls.h
    ctrlutil.h
    dlgutil.h
    etwreg.h
    etwwrite.h
    fileutil.cpp
    fileutil.h
    guids.h
    i18n.h
    logger.cpp
    logger.h
    metric.cpp
    metric.h
    dpi.h
    RAII.h
    sound.h
    theme.h
    window.h
    toolbar.cpp
    toolbar.h
    version.h.in
    version.rc.in
    uiutil.h
)
add_library(qiqi_common STATIC
    ${QIQI_COMMON_SOURCES}
    "${QIQIIME_GENERATED_DIR}/common/version.h"
)
if(QIQI_METRICS_BENCHMARK_BASELINE)
    target_compile_definitions(qiqi_common PRIVATE QIQI_METRICS_BENCHMARK_BASELINE)
endif()
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${QIQI_COMMON_SOURCES})
target_include_directories(qiqi_common PUBLIC
    "${PROJECT_SOURCE_DIR}/src"
    "${QIQIIME_GENERATED_DIR}"
)
target_link_libraries(qiqi_common PUBLIC
    Microsoft.GSL::GSL
    advapi32
    ole32
    ktmw32
    user32
    gdi32
    comctl32
    uxtheme
    d2d1
    dwrite
    dwmapi
    shell32
)
target_link_libraries(qiqi_common PRIVATE nlohmann_json::nlohmann_json)
