# SPDX-FileCopyrightText: 2026 cloudfyy
# SPDX-License-Identifier: GPL-3.0-or-later

set(QIQI_COMMON_TEST_SOURCES
    test_support.h
    common.cpp
    common_main.cpp
    core/test.h
    core/config.cpp
    core/files.cpp
    core/logger.cpp
    core/resources.cpp
    etw.cpp
    ui_d2d_controls.cpp
)
add_executable(qiqi_common_test ${QIQI_COMMON_TEST_SOURCES})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${QIQI_COMMON_TEST_SOURCES})
target_include_directories(qiqi_common_test PRIVATE "${PROJECT_SOURCE_DIR}/tests")
target_link_libraries(qiqi_common_test PRIVATE
    qiqi_common
    ole32
    oleaut32
    uuid
    tdh
)
add_test(NAME QiQiCommonTest COMMAND qiqi_common_test)

add_executable(qiqi_change_sync_test change_sync.cpp)
target_include_directories(qiqi_change_sync_test PRIVATE "${PROJECT_SOURCE_DIR}/tests")
target_link_libraries(qiqi_change_sync_test PRIVATE qiqi_engine qiqi_common ole32 userenv)
add_test(NAME QiQiChangeSyncTest COMMAND qiqi_change_sync_test)
set_tests_properties(QiQiChangeSyncTest PROPERTIES TIMEOUT 90)

add_executable(qiqi_metrics_test metric.cpp etw.cpp)
target_include_directories(qiqi_metrics_test PRIVATE "${PROJECT_SOURCE_DIR}/tests")
target_link_libraries(qiqi_metrics_test PRIVATE qiqi_common tdh)
add_test(NAME QiQiMetricsTest COMMAND qiqi_metrics_test)
add_test(NAME QiQiMetricsEtwTest COMMAND qiqi_metrics_test --etw)
set_tests_properties(QiQiMetricsTest PROPERTIES TIMEOUT 30)
set_tests_properties(QiQiMetricsEtwTest PROPERTIES TIMEOUT 90 SKIP_RETURN_CODE 77)
