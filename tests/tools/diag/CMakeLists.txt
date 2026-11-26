# SPDX-FileCopyrightText: 2026 cloudfyy
# SPDX-License-Identifier: GPL-3.0-or-later

add_executable(qiqi_diag_test
    test.h
    main.cpp
    privacy.cpp
    report.cpp
)
target_link_libraries(qiqi_diag_test PRIVATE
    qiqi_diag
    nlohmann_json::nlohmann_json
)
target_include_directories(qiqi_diag_test PRIVATE
    "${PROJECT_SOURCE_DIR}/tests"
)
add_test(
    NAME QiQiIMEDiagLibraryTest
    COMMAND qiqi_diag_test
)
set_tests_properties(
    QiQiIMEDiagLibraryTest
    PROPERTIES TIMEOUT 30)

find_program(
    QIQI_DIAG_POWERSHELL_EXECUTABLE
    NAMES pwsh powershell
    REQUIRED)
add_test(
    NAME QiQiIMEDiagCliTest
    COMMAND
        "${QIQI_DIAG_POWERSHELL_EXECUTABLE}"
        -NoLogo
        -NoProfile
        -ExecutionPolicy Bypass
        -File
        "${CMAKE_CURRENT_SOURCE_DIR}/CliTest.ps1"
        -DiagnosticPath
        "$<TARGET_FILE:QiQiIMEDiag>"
)
set_tests_properties(
    QiQiIMEDiagCliTest
    PROPERTIES TIMEOUT 60)
