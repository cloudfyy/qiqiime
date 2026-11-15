# SPDX-FileCopyrightText: 2026 cloudfyy
# SPDX-License-Identifier: GPL-3.0-or-later

add_library(QiQiIMEInstallerActions SHARED actions.cpp actions.h actions.def)
target_include_directories(QiQiIMEInstallerActions PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(QiQiIMEInstallerActions PRIVATE Microsoft.GSL::GSL msi)
if(MSVC)
    target_link_options(QiQiIMEInstallerActions PRIVATE /DEBUG:FULL)
    set_target_properties(QiQiIMEInstallerActions PROPERTIES
        PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>")
endif()
