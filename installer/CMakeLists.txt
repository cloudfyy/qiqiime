# SPDX-FileCopyrightText: 2026 cloudfyy
# SPDX-License-Identifier: GPL-3.0-or-later

add_library(qiqi_installer_cleanup STATIC cleanup.cpp cleanup.h internal.h)
target_include_directories(qiqi_installer_cleanup PUBLIC "${PROJECT_SOURCE_DIR}/src" "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(qiqi_installer_cleanup PUBLIC Microsoft.GSL::GSL advapi32 ole32)
add_library(QiQiIMEInstallerActions SHARED actions.cpp actions.h wizard.cpp wizard.h actions.def)
target_link_libraries(QiQiIMEInstallerActions PRIVATE qiqi_installer_cleanup msi user32)
if(MSVC)
    target_link_options(QiQiIMEInstallerActions PRIVATE /DEBUG:FULL)
    set_target_properties(QiQiIMEInstallerActions PROPERTIES
        PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>")
endif()
