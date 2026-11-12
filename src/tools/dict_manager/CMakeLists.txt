# SPDX-FileCopyrightText: 2026 cloudfyy
# SPDX-License-Identifier: GPL-3.0-or-later

set(QIQI_DICT_MANAGER_SOURCES
    model.h model.cpp
    editor.h editor.cpp
    window.h internal.h window.cpp
)
add_library(qiqi_dict_manager STATIC ${QIQI_DICT_MANAGER_SOURCES})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${QIQI_DICT_MANAGER_SOURCES})
target_link_libraries(qiqi_dict_manager PUBLIC qiqi_engine qiqi_common comdlg32 uxtheme)
target_link_options(qiqi_dict_manager INTERFACE
    "/MANIFESTDEPENDENCY:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'")

set(QIQI_RESOURCE_FILE_TYPE VFT_APP)
set(QIQI_RESOURCE_FILE_SUBTYPE 0x0L)
set(QIQI_RESOURCE_FILENAME "QiQiIMEDictManager.exe")
set(QIQI_RESOURCE_DESCRIPTION "奇奇五笔词库管理 (QiQiIME Dictionary Manager - ${QIQIIME_ARCH_NAME})")
set(QIQI_RESOURCE_COMMENTS "QiQiIME dictionary and phrase frequency manager")
set(QIQI_RESOURCE_ICON "${PROJECT_SOURCE_DIR}/data/qiqiime-dict.ico")
set(QIQI_DICT_MANAGER_RESOURCE "${QIQIIME_GENERATED_DIR}/tools/dict_manager/version.rc")
configure_file("${PROJECT_SOURCE_DIR}/src/common/version.rc.in" "${QIQI_DICT_MANAGER_RESOURCE}" @ONLY)
set_source_files_properties("${QIQI_DICT_MANAGER_RESOURCE}"
    PROPERTIES OBJECT_DEPENDS "${QIQI_RESOURCE_ICON}")
add_executable(QiQiIMEDictManager WIN32 main.cpp "${QIQI_DICT_MANAGER_RESOURCE}")
target_link_libraries(QiQiIMEDictManager PRIVATE qiqi_dict_manager)
