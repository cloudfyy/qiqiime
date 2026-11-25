# SPDX-FileCopyrightText: 2026 cloudfyy
# SPDX-License-Identifier: GPL-3.0-or-later

add_executable(qiqi_broker_test
    main.cpp
)
target_include_directories(qiqi_broker_test PRIVATE
    "${PROJECT_SOURCE_DIR}/tests"
)
target_link_libraries(qiqi_broker_test PRIVATE
    qiqi_config_broker_store
)
add_test(
    NAME QiQiConfigBrokerStoreTest
    COMMAND qiqi_broker_test
)
set_tests_properties(
    QiQiConfigBrokerStoreTest
    PROPERTIES TIMEOUT 30)

add_executable(qiqi_broker_probe
    probe.cpp
)
target_include_directories(qiqi_broker_probe PRIVATE
    "${PROJECT_SOURCE_DIR}/tests"
)
target_link_libraries(qiqi_broker_probe PRIVATE
    qiqi_common
    ole32
    oleaut32
    uuid
)
add_dependencies(qiqi_broker_probe
    QiQiIMEConfigBroker
    QiQiIMEConfigProxy
)

find_program(QIQI_BROKER_CLI_POWERSHELL_EXECUTABLE
    NAMES pwsh powershell
    REQUIRED)
add_test(
    NAME QiQiConfigBrokerCliTest
    COMMAND
        "${QIQI_BROKER_CLI_POWERSHELL_EXECUTABLE}"
        -NoLogo
        -NoProfile
        -NonInteractive
        -File
        "${CMAKE_CURRENT_SOURCE_DIR}/Run-CliTest.ps1"
        -BrokerPath
        "$<TARGET_FILE:QiQiIMEConfigBroker>"
)
set_tests_properties(
    QiQiConfigBrokerCliTest
    PROPERTIES TIMEOUT 30)

add_test(
    NAME QiQiConfigBrokerDcomTest
    COMMAND
        "${QIQI_BROKER_CLI_POWERSHELL_EXECUTABLE}"
        -NoLogo
        -NoProfile
        -NonInteractive
        -File
        "${CMAKE_CURRENT_SOURCE_DIR}/Run-DcomTest.ps1"
        -BuildDirectory
        "${CMAKE_BINARY_DIR}"
        -Configuration
        "$<CONFIG>"
)
set_tests_properties(
    QiQiConfigBrokerDcomTest
    PROPERTIES
        TIMEOUT 90
        RUN_SERIAL TRUE)

if(CMAKE_SIZEOF_VOID_P EQUAL 8
   AND EXISTS "${PROJECT_SOURCE_DIR}/build/x86/Release/qiqi_broker_probe.exe")
    find_program(QIQI_BROKER_POWERSHELL_EXECUTABLE
        NAMES pwsh powershell
        REQUIRED)
    add_test(
        NAME QiQiConfigBrokerCrossArchTest
        COMMAND
            "${QIQI_BROKER_POWERSHELL_EXECUTABLE}"
            -NoLogo
            -NoProfile
            -NonInteractive
            -File
            "${CMAKE_CURRENT_SOURCE_DIR}/Run-CrossArchDcomTest.ps1"
            -X64BuildDirectory
            "${CMAKE_BINARY_DIR}"
            -X86BuildDirectory
            "${PROJECT_SOURCE_DIR}/build/x86"
            -Configuration
            Release
    )
    set_tests_properties(
        QiQiConfigBrokerCrossArchTest
        PROPERTIES
            TIMEOUT 60
            RUN_SERIAL TRUE)
endif()
