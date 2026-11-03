set(QIQI_COMMON_TEST_SOURCES
    test_support.h
    common.cpp
    common_main.cpp
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
)
add_test(NAME QiQiCommonTest COMMAND qiqi_common_test)

add_executable(qiqi_change_sync_test change_sync.cpp)
target_include_directories(qiqi_change_sync_test PRIVATE "${PROJECT_SOURCE_DIR}/tests")
target_link_libraries(qiqi_change_sync_test PRIVATE qiqi_engine qiqi_common ole32 userenv)
add_test(NAME QiQiChangeSyncTest COMMAND qiqi_change_sync_test)
set_tests_properties(QiQiChangeSyncTest PROPERTIES TIMEOUT 90)
