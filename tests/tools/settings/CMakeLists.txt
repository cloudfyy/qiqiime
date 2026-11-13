# SPDX-FileCopyrightText: 2026 cloudfyy
# SPDX-License-Identifier: GPL-3.0-or-later

set(QIQI_SETTINGS_TEST_SOURCES
    main.cpp
    cleanup.cpp
    window.cpp
    edges.cpp
    version.cpp
    save.cpp
    message.cpp
)
add_executable(qiqi_settings_test
    ${QIQI_SETTINGS_TEST_SOURCES}
    "${PROJECT_SOURCE_DIR}/src/tools/settings/main.cpp"
)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${QIQI_SETTINGS_TEST_SOURCES})
target_include_directories(qiqi_settings_test PRIVATE "${PROJECT_SOURCE_DIR}/tests")
target_link_libraries(qiqi_settings_test PRIVATE
    qiqi_settings_ui
    ole32
    oleaut32
    uuid
    version
)
add_dependencies(qiqi_settings_test QiQiIMESettings)
add_test(NAME QiQiSettingsTest COMMAND qiqi_settings_test)
