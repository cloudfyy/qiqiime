# SPDX-FileCopyrightText: 2026 cloudfyy
# SPDX-License-Identifier: GPL-3.0-or-later

add_executable(qiqi_broker_test
    main.cpp
    "${PROJECT_SOURCE_DIR}/src/broker/runtime.cpp"
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

add_executable(qiqi_broker_launchlow
    launchlow.cpp
)
target_include_directories(qiqi_broker_launchlow PRIVATE
    "${PROJECT_SOURCE_DIR}/tests"
)
target_link_libraries(qiqi_broker_launchlow PRIVATE
    qiqi_common
)

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
        "${CMAKE_CURRENT_SOURCE_DIR}/CliTest.ps1"
        -BrokerPath
        "$<TARGET_FILE:QiQiIMEConfigBroker>"
)
set_tests_properties(
    QiQiConfigBrokerCliTest
    PROPERTIES TIMEOUT 30)

set(QIQI_BROKER_HANDOFF_TEST_ARGUMENTS)
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    list(APPEND QIQI_BROKER_HANDOFF_TEST_ARGUMENTS
        -LowBrokerHandoff)
endif()

add_test(
    NAME QiQiConfigBrokerDcomTest
    COMMAND
        "${QIQI_BROKER_CLI_POWERSHELL_EXECUTABLE}"
        -NoLogo
        -NoProfile
        -NonInteractive
        -File
        "${CMAKE_CURRENT_SOURCE_DIR}/DcomTest.ps1"
        -BuildDirectory
        "${CMAKE_BINARY_DIR}"
        -Configuration
        "$<CONFIG>"
        -LaunchLowPath
        "$<TARGET_FILE:qiqi_broker_launchlow>"
        ${QIQI_BROKER_HANDOFF_TEST_ARGUMENTS}
)
set_tests_properties(
    QiQiConfigBrokerDcomTest
    PROPERTIES
        TIMEOUT 90
        RUN_SERIAL TRUE
        SKIP_RETURN_CODE 77)

add_test(
    NAME QiQiConfigBrokerHandoffControlTest
    COMMAND
        "${QIQI_BROKER_CLI_POWERSHELL_EXECUTABLE}"
        -NoLogo
        -NoProfile
        -NonInteractive
        -File
        "${CMAKE_CURRENT_SOURCE_DIR}/HandoffControlTest.ps1"
        -BrokerPath
        "$<TARGET_FILE:QiQiIMEConfigBroker>"
        -LaunchLowPath
        "$<TARGET_FILE:qiqi_broker_launchlow>"
        -ProbePath
        "$<TARGET_FILE:qiqi_broker_probe>"
)
set_tests_properties(
    QiQiConfigBrokerHandoffControlTest
    PROPERTIES
        TIMEOUT 60
        RUN_SERIAL TRUE)

set(_x86_broker_build "${PROJECT_SOURCE_DIR}/build/x86")
if(CMAKE_SIZEOF_VOID_P EQUAL 8
   AND (EXISTS "${_x86_broker_build}/Release/qiqi_broker_probe.exe"
        OR EXISTS "${_x86_broker_build}/Debug/qiqi_broker_probe.exe"))
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
            "${CMAKE_CURRENT_SOURCE_DIR}/CrossArchDcomTest.ps1"
            -X64BuildDirectory
            "${CMAKE_BINARY_DIR}"
            -X86BuildDirectory
            "${_x86_broker_build}"
            -Configuration
            "$<CONFIG>"
        CONFIGURATIONS Debug Release
    )
    set_tests_properties(
        QiQiConfigBrokerCrossArchTest
        PROPERTIES
            TIMEOUT 60
            RUN_SERIAL TRUE)
endif()
