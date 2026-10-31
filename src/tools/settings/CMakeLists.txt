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

add_executable(QiQiIMESettings WIN32
    main.cpp
    "${QIQIIME_GENERATED_DIR}/common/version.rc"
)
target_link_libraries(QiQiIMESettings PRIVATE
    qiqi_settings_ui
    ole32
    oleaut32
    uuid
)
set_target_properties(QiQiIMESettings PROPERTIES OUTPUT_NAME "QiQiIMESettings")
