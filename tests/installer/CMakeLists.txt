# SPDX-FileCopyrightText: 2026 cloudfyy
# SPDX-License-Identifier: GPL-3.0-or-later

add_executable(qiqi_installer_actions_test installer_main.cpp cleanup.cpp
    "${PROJECT_SOURCE_DIR}/installer/actions.cpp"
    "${PROJECT_SOURCE_DIR}/installer/wizard.cpp")
target_include_directories(qiqi_installer_actions_test PRIVATE
    "${PROJECT_SOURCE_DIR}/installer" "${PROJECT_SOURCE_DIR}/src" "${PROJECT_SOURCE_DIR}/tests")
target_link_libraries(qiqi_installer_actions_test PRIVATE qiqi_installer_cleanup msi ole32 user32)
add_dependencies(qiqi_installer_actions_test QiQiIMEInstallerActions)
add_test(NAME QiQiInstallerActionsTest COMMAND qiqi_installer_actions_test)
set_tests_properties(QiQiInstallerActionsTest PROPERTIES RUN_SERIAL TRUE TIMEOUT 60)
