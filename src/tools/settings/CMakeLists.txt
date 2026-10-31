set(QIQI_SETTINGS_UI_SOURCES
    about.cpp
    util.h
    window.cpp
    window.h
    controls.cpp
    internal.h
)
add_library(qiqi_settings_ui STATIC ${QIQI_SETTINGS_UI_SOURCES})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${QIQI_SETTINGS_UI_SOURCES})
target_link_libraries(qiqi_settings_ui PUBLIC qiqi_common shell32)

set(QIQI_RESOURCE_FILE_TYPE VFT_APP)
set(QIQI_RESOURCE_FILE_SUBTYPE 0x0L)
set(QIQI_RESOURCE_FILENAME "QiQiIMESettings.exe")
set(QIQI_RESOURCE_DESCRIPTION "奇奇五笔设置 (QiQi Wubi Settings - ${QIQIIME_ARCH_NAME})")
set(QIQI_RESOURCE_COMMENTS "奇奇五笔设置 (QiQi Wubi Settings) - https://github.com/cloudfyy/qiqiime")
set(QIQI_SETTINGS_VERSION_RESOURCE "${QIQIIME_GENERATED_DIR}/tools/settings/version.rc")
configure_file(
    "${PROJECT_SOURCE_DIR}/src/common/version.rc.in"
    "${QIQI_SETTINGS_VERSION_RESOURCE}"
    @ONLY
)
set_source_files_properties("${QIQI_SETTINGS_VERSION_RESOURCE}"
    PROPERTIES OBJECT_DEPENDS "${PROJECT_SOURCE_DIR}/data/qiqiime.ico"
)

add_executable(QiQiIMESettings WIN32
    main.cpp
    "${QIQI_SETTINGS_VERSION_RESOURCE}"
)
target_link_libraries(QiQiIMESettings PRIVATE
    qiqi_settings_ui
    ole32
    oleaut32
    uuid
)
set_target_properties(QiQiIMESettings PROPERTIES OUTPUT_NAME "QiQiIMESettings")
