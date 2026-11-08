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
    comp/tests.h
    comp/cleanup.cpp
    comp/cleanup_handoff.cpp
    comp/context_reset.cpp
    comp/edit.cpp
    comp/edit_action.cpp
    comp/edit_execution.cpp
    comp/edit_entry.cpp
    comp/finish.cpp
    comp/finish_state.cpp
    comp/identity.cpp
    comp/layout.cpp
    comp/layout_entry.cpp
    comp/layout_update.cpp
    comp/layout_position.cpp
    comp/presentation.cpp
    comp/presentation_entry.cpp
    comp/range.cpp
    comp/start.cpp
    comp/start_update.cpp
    comp/state.cpp
    comp/target.cpp
    comp/target_entry.cpp
    comp/termination.cpp
    comp/termination_route.cpp
    comp/update.cpp
    tsf/mocks.h
    tsf/test_trace.h
    tsf/com.cpp
    tsf/category_manager.cpp
    tsf/logging.cpp
    tsf/restricted_input.cpp
    tsf/service.cpp
    tsf/service_action.cpp
    tsf/service_lifecycle.h
    tsf/service_lifecycle.cpp
    tsf/service_cleanup.cpp
    tsf/service_context.cpp
    tsf/service_layout.cpp
    tsf/service_deferred.cpp
    tsf/service_native.cpp
    tsf/service_presentation.cpp
    tsf/service_reentrancy.cpp
    tsf/service_target.cpp
    tsf/service_windows.cpp
    ui/ui_candidate.cpp
    ui/ui_langbar_menu.cpp
    ui/ui_statusbar.cpp
    tsf_ui_main.cpp
)
set(QIQI_TSF_UI_IMPL_SOURCES
    "${PROJECT_SOURCE_DIR}/src/ime/comp/cleanup.cpp"
    "${PROJECT_SOURCE_DIR}/src/ime/tsf/dllmain.cpp"
    "${PROJECT_SOURCE_DIR}/src/ime/tsf/class_factory.cpp"
    "${PROJECT_SOURCE_DIR}/src/ime/tsf/register.cpp"
    "${PROJECT_SOURCE_DIR}/src/ime/tsf/tip_service.cpp"
    "${PROJECT_SOURCE_DIR}/src/ime/tsf/tip_keys.cpp"
    "${PROJECT_SOURCE_DIR}/src/ime/tsf/tip_composition.cpp"
    "${PROJECT_SOURCE_DIR}/src/ime/tsf/tip_config.cpp"
    "${PROJECT_SOURCE_DIR}/src/ime/tsf/display_attribute.cpp"
    "${PROJECT_SOURCE_DIR}/src/ime/ui/candidate_ui.cpp"
    "${PROJECT_SOURCE_DIR}/src/ime/ui/langbar_button.cpp"
    "${PROJECT_SOURCE_DIR}/src/ime/ui/popup_menu.cpp"
    "${PROJECT_SOURCE_DIR}/src/ime/ui/statusbar.cpp"
    "${PROJECT_SOURCE_DIR}/src/ime/ui/statusbar_icons.cpp"
    "${PROJECT_SOURCE_DIR}/src/ime/ui/statusbar_tooltip.cpp"
)
add_executable(qiqi_tsf_ui_test
    ${QIQI_TSF_UI_TEST_SOURCES}
    ${QIQI_TSF_UI_IMPL_SOURCES}
    "${PROJECT_SOURCE_DIR}/tests/common/etw.cpp"
)
if(MSVC)
    target_compile_options(qiqi_tsf_ui_test PRIVATE /Zi)
    target_link_options(qiqi_tsf_ui_test PRIVATE /DEBUG:FULL)
    set_target_properties(qiqi_tsf_ui_test PROPERTIES
        PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>"
    )
endif()
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${QIQI_TSF_UI_TEST_SOURCES})
source_group(TREE "${PROJECT_SOURCE_DIR}/src/ime" PREFIX "src" FILES ${QIQI_TSF_UI_IMPL_SOURCES})
target_include_directories(qiqi_tsf_ui_test PRIVATE "${PROJECT_SOURCE_DIR}/tests")
target_link_libraries(qiqi_tsf_ui_test PRIVATE
    qiqi_engine
    qiqi_settings_ui
    qiqi_log_monitor
    shell32
    ole32
    oleaut32
    uuid
    tdh
)
find_program(QIQI_TEST_POWERSHELL NAMES pwsh powershell REQUIRED)
find_program(QIQI_TEST_CDB NAMES cdb
    PATHS "$ENV{ProgramFiles\(x86\)}/Windows Kits/10/Debuggers/${QIQIIME_ARCH_NAME}"
    NO_DEFAULT_PATH REQUIRED)
foreach(_mode IN ITEMS full focused)
    if(_mode STREQUAL "full")
        set(_test_name QiQiTsfUiTest)
    else()
        set(_test_name QiQiCompositionLifecycleTest)
    endif()
    add_test(NAME ${_test_name}
        COMMAND "${QIQI_TEST_POWERSHELL}" -NoLogo -NoProfile -ExecutionPolicy Bypass
            -File "${CMAKE_CURRENT_SOURCE_DIR}/test_watchdog.ps1"
            -TestExecutable "$<TARGET_FILE:qiqi_tsf_ui_test>"
            -Debugger "${QIQI_TEST_CDB}" -Mode "${_mode}")
    set_tests_properties(${_test_name} PROPERTIES RUN_SERIAL TRUE TIMEOUT 150)
endforeach()
add_dependencies(qiqi_tsf_ui_test qiqiime)
add_test(NAME QiQiEtwIntegrationTest COMMAND qiqi_tsf_ui_test --etw-integration)
set_tests_properties(QiQiEtwIntegrationTest PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)
add_test(NAME QiQiFocusMetricsEtwTest COMMAND qiqi_tsf_ui_test --focus-metrics-etw)
set_tests_properties(QiQiFocusMetricsEtwTest PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 90 RUN_SERIAL TRUE)

add_executable(QiQiTsfTestHost WIN32
    tsf_test_host.cpp
    "${PROJECT_SOURCE_DIR}/src/ime/comp/cleanup.cpp"
)
target_include_directories(QiQiTsfTestHost PRIVATE "${PROJECT_SOURCE_DIR}/tests")
target_link_libraries(QiQiTsfTestHost PRIVATE
    qiqi_common
    comctl32
    ole32
    oleaut32
    uuid
)
