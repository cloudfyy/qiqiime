# SPDX-FileCopyrightText: 2026 cloudfyy
# SPDX-License-Identifier: GPL-3.0-or-later

add_executable(qiqi_dict_manager_test
    main.cpp
    test.h
    backup.cpp
    commands.cpp
    editor.cpp
    helpers.cpp
    inline.cpp
    layout.cpp
    model.cpp
    search.cpp
    version.cpp
    window.cpp
    "${CMAKE_BINARY_DIR}/generated/tools/dict_manager/version.rc"
)
target_include_directories(qiqi_dict_manager_test PRIVATE "${PROJECT_SOURCE_DIR}/tests")
target_link_libraries(qiqi_dict_manager_test PRIVATE qiqi_dict_manager version)
add_dependencies(qiqi_dict_manager_test QiQiIMEDictManager)
add_test(NAME QiQiDictManagerTest COMMAND qiqi_dict_manager_test)
