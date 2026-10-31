set(QIQI_ENGINE_SOURCES
    engine/binary_dict.cpp
    engine/binary_dict.h
    engine/fsm.h
    engine/engine.cpp
    engine/engine.h
    engine/default_dict.h
)
add_library(qiqi_engine STATIC ${QIQI_ENGINE_SOURCES})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${QIQI_ENGINE_SOURCES})
target_link_libraries(qiqi_engine PUBLIC qiqi_common)

set(QIQIIME_SOURCES
    tsf/dllmain.cpp
    tsf/class_factory.h
    tsf/class_factory.cpp
    tsf/register.h
    tsf/register.cpp
    tsf/tip_service.h
    tsf/tip_service.cpp
    tsf/display_attribute.h
    tsf/display_attribute.cpp
    tsf/edit_session.h
    tsf/qiqiime.def
    ui/candidate_ui.h
    ui/candidate_ui.cpp
    ui/langbar_button.h
    ui/langbar_button.cpp
    ui/popup_menu.cpp
    ui/status_bar.h
    ui/status_bar.cpp
)
add_library(qiqiime SHARED
    ${QIQIIME_SOURCES}
    "${QIQIIME_GENERATED_DIR}/common/version.rc"
)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${QIQIIME_SOURCES})
set_target_properties(qiqiime PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)
target_link_libraries(qiqiime PRIVATE
    qiqi_engine
    qiqi_settings_ui
    shell32
    ole32
    oleaut32
    uuid
)

add_custom_command(TARGET qiqiime POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${PROJECT_SOURCE_DIR}/data/wubi86_jidian.txt"
        "$<TARGET_FILE_DIR:qiqiime>/wubi86_jidian.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${PROJECT_SOURCE_DIR}/data/wubi86_jidian_extra.txt"
        "$<TARGET_FILE_DIR:qiqiime>/wubi86_jidian_extra.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${PROJECT_SOURCE_DIR}/data/wubi86_haifeng.txt"
        "$<TARGET_FILE_DIR:qiqiime>/wubi86_haifeng.txt"
)
