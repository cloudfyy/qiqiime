# SPDX-FileCopyrightText: 2026 cloudfyy
# SPDX-License-Identifier: GPL-3.0-or-later

add_library(qiqi_diag STATIC
    diag.h
    internal.h
    common.cpp
    machine.cpp
    cpu.cpp
    display.cpp
    fonts.cpp
    a11y.cpp
    theme.cpp
    security.cpp
    ime.cpp
    broker.cpp
    dcom.cpp
    report.cpp
)
target_link_libraries(qiqi_diag PUBLIC
    qiqi_common
)
target_link_libraries(qiqi_diag PRIVATE
    nlohmann_json::nlohmann_json
    shcore
)

set(QIQI_RESOURCE_FILE_TYPE VFT_APP)
set(QIQI_RESOURCE_FILE_SUBTYPE 0x0L)
set(QIQI_RESOURCE_FILENAME "QiQiIMEDiag.exe")
set(QIQI_RESOURCE_DESCRIPTION "奇奇五笔诊断工具 (QiQi IME Diag - ${QIQIIME_ARCH_NAME})")
set(QIQI_RESOURCE_COMMENTS "QiQi IME read-only command-line diagnostics")
set(QIQI_RESOURCE_ICON "${PROJECT_SOURCE_DIR}/data/qiqiime-diag.ico")
set(QIQI_DIAG_RESOURCE "${QIQIIME_GENERATED_DIR}/tools/diag/version.rc")
configure_file("${PROJECT_SOURCE_DIR}/src/common/version.rc.in" "${QIQI_DIAG_RESOURCE}" @ONLY)
set_source_files_properties("${QIQI_DIAG_RESOURCE}"
    PROPERTIES OBJECT_DEPENDS "${QIQI_RESOURCE_ICON}")

add_executable(QiQiIMEDiag
    main.cpp
    "${QIQI_DIAG_RESOURCE}"
)
target_link_libraries(QiQiIMEDiag PRIVATE
    qiqi_diag
    ftxui::component
    ftxui::dom
    ftxui::screen
)
target_link_options(QiQiIMEDiag PRIVATE
    /SUBSYSTEM:CONSOLE
)
