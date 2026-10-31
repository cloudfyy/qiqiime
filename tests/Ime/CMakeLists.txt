set(QIQI_ENGINE_TEST_SOURCES
    engine/dict.cpp
    engine/input.cpp
    engine/modes_punct.cpp
    engine_main.cpp
)
add_executable(qiqi_engine_test ${QIQI_ENGINE_TEST_SOURCES})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${QIQI_ENGINE_TEST_SOURCES})
target_include_directories(qiqi_engine_test PRIVATE "${PROJECT_SOURCE_DIR}/tests")
target_link_libraries(qiqi_engine_test PRIVATE qiqi_engine)
add_test(NAME QiQiEngineTest COMMAND qiqi_engine_test)

set(QIQI_TSF_UI_TEST_SOURCES
    tsf/tsf_mocks.h
    tsf/tsf_com.cpp
    tsf/tsf_service.cpp
    ui/ui_candidate.cpp
    ui/ui_langbar_menu.cpp
    ui/ui_status_bar.cpp
    tsf_ui_main.cpp
)
set(QIQI_TSF_UI_IMPL_SOURCES
    "${PROJECT_SOURCE_DIR}/src/Ime/tsf/dllmain.cpp"
    "${PROJECT_SOURCE_DIR}/src/Ime/tsf/class_factory.cpp"
    "${PROJECT_SOURCE_DIR}/src/Ime/tsf/register.cpp"
    "${PROJECT_SOURCE_DIR}/src/Ime/tsf/tip_service.cpp"
    "${PROJECT_SOURCE_DIR}/src/Ime/tsf/display_attribute.cpp"
    "${PROJECT_SOURCE_DIR}/src/Ime/ui/candidate_ui.cpp"
    "${PROJECT_SOURCE_DIR}/src/Ime/ui/langbar_button.cpp"
    "${PROJECT_SOURCE_DIR}/src/Ime/ui/popup_menu.cpp"
    "${PROJECT_SOURCE_DIR}/src/Ime/ui/status_bar.cpp"
)
add_executable(qiqi_tsf_ui_test
    ${QIQI_TSF_UI_TEST_SOURCES}
    ${QIQI_TSF_UI_IMPL_SOURCES}
)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${QIQI_TSF_UI_TEST_SOURCES})
source_group(TREE "${PROJECT_SOURCE_DIR}/src/Ime" PREFIX "src" FILES ${QIQI_TSF_UI_IMPL_SOURCES})
target_include_directories(qiqi_tsf_ui_test PRIVATE "${PROJECT_SOURCE_DIR}/tests")
target_link_libraries(qiqi_tsf_ui_test PRIVATE
    qiqi_engine
    qiqi_settings_ui
    shell32
    ole32
    oleaut32
    uuid
)
add_test(NAME QiQiTsfUiTest COMMAND qiqi_tsf_ui_test)
