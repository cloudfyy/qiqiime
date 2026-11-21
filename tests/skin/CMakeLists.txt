# SPDX-FileCopyrightText: 2026 cloudfyy
# SPDX-License-Identifier: GPL-3.0-or-later

set(QIQI_SKIN_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/catalog.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/settings.cpp"
)
target_sources(qiqi_tsf_ui_test PRIVATE ${QIQI_SKIN_TEST_SOURCES})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" PREFIX "skin" FILES ${QIQI_SKIN_TEST_SOURCES})

add_test(NAME QiQiSkinTest
    COMMAND "${QIQI_TEST_POWERSHELL}" -NoLogo -NoProfile -ExecutionPolicy Bypass
        -File "${PROJECT_SOURCE_DIR}/tests/ime/test_watchdog.ps1"
        -TestExecutable "$<TARGET_FILE:qiqi_tsf_ui_test>"
        -Debugger "${QIQI_TEST_CDB}" -Mode skins)
set_tests_properties(QiQiSkinTest PROPERTIES RUN_SERIAL TRUE TIMEOUT 150 LABELS "skin")
