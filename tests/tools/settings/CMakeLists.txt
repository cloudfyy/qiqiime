set(QIQI_SETTINGS_TEST_SOURCES
    main.cpp
    window.cpp
    edges.cpp
)
add_executable(qiqi_settings_test ${QIQI_SETTINGS_TEST_SOURCES})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${QIQI_SETTINGS_TEST_SOURCES})
target_include_directories(qiqi_settings_test PRIVATE "${PROJECT_SOURCE_DIR}/tests")
target_link_libraries(qiqi_settings_test PRIVATE
    qiqi_settings_ui
    ole32
    oleaut32
    uuid
)
add_test(NAME QiQiSettingsTest COMMAND qiqi_settings_test)
