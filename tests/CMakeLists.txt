# Engine & State Machine Unit Tests
set(QIQI_ENGINE_TEST_SOURCES
    common.h
    common/common.cpp
    engine/engine_dict.cpp
    engine/engine_input.cpp
    engine/engine_modes_punct.cpp
    engine_main.cpp
)
add_executable(qiqi_engine_test
    ${QIQI_ENGINE_TEST_SOURCES}
)
target_include_directories(qiqi_engine_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${QIQI_ENGINE_TEST_SOURCES})
target_link_libraries(qiqi_engine_test PRIVATE qiqi_engine)
add_test(NAME QiQiEngineTest COMMAND qiqi_engine_test)

# TSF COM Service & Candidate UI Unit Tests
set(QIQI_TSF_UI_TEST_SOURCES
    common.h
    tsf/tsf_mocks.h
    tsf/tsf_com.cpp
    tsf/tsf_service.cpp
    ui/ui_candidate.cpp
    ui/ui_langbar_menu.cpp
    ui/ui_settings_dialog.cpp
    ui/ui_status_bar.cpp
    tsf_ui_main.cpp
)
set(QIQI_TSF_UI_IMPL_SOURCES
    "${PROJECT_SOURCE_DIR}/src/tsf/dllmain.cpp"
    "${PROJECT_SOURCE_DIR}/src/tsf/class_factory.cpp"
    "${PROJECT_SOURCE_DIR}/src/tsf/register.cpp"
    "${PROJECT_SOURCE_DIR}/src/tsf/tip_service.cpp"
    "${PROJECT_SOURCE_DIR}/src/tsf/display_attribute.cpp"
    "${PROJECT_SOURCE_DIR}/src/ui/candidate_ui.cpp"
    "${PROJECT_SOURCE_DIR}/src/ui/langbar_button.cpp"
    "${PROJECT_SOURCE_DIR}/src/ui/popup_menu.cpp"
    "${PROJECT_SOURCE_DIR}/src/ui/settings_dialog.cpp"
    "${PROJECT_SOURCE_DIR}/src/ui/status_bar.cpp"
)
add_executable(qiqi_tsf_ui_test
    ${QIQI_TSF_UI_TEST_SOURCES}
    ${QIQI_TSF_UI_IMPL_SOURCES}
)
target_include_directories(qiqi_tsf_ui_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${QIQI_TSF_UI_TEST_SOURCES})
source_group(TREE "${PROJECT_SOURCE_DIR}/src" PREFIX "src" FILES ${QIQI_TSF_UI_IMPL_SOURCES})
target_link_libraries(qiqi_tsf_ui_test PRIVATE
    qiqi_engine
    user32
    gdi32
    comctl32
    d2d1
    dwrite
    shell32
    ole32
    oleaut32
    advapi32
    uuid
)
add_test(NAME QiQiTsfUiTest COMMAND qiqi_tsf_ui_test)
