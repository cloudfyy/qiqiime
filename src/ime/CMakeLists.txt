# SPDX-FileCopyrightText: 2026 cloudfyy
# SPDX-License-Identifier: GPL-3.0-or-later

set(QIQI_ENGINE_SOURCES
    engine/backup.cpp
    engine/backup.h
    engine/dict.cpp
    engine/dict.h
    fsm/fsm.h
    fsm/keymap.h
    fsm/pagination.h
    fsm/transitions.h
    fsm/types.h
    engine/engine.cpp
    engine/dictdetail.h
    engine/dictutil.cpp
    engine/dictentry.cpp
    engine/dictfreq.cpp
    engine/dictload.cpp
    engine/dictlookup.cpp
    engine/fullcode.cpp
    engine/fullcode.h
    engine/dictsync.cpp
    engine/dictuser.cpp
    engine/engine.h
    engine/builtindict.h
    fsm/table.h
)
add_library(qiqi_engine STATIC ${QIQI_ENGINE_SOURCES})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${QIQI_ENGINE_SOURCES})
target_link_libraries(qiqi_engine PUBLIC qiqi_common)

configure_file("${PROJECT_SOURCE_DIR}/data/full.idx" "${CMAKE_BINARY_DIR}/data/full.idx" COPYONLY)

set(QIQIIME_SOURCES
    tsf/dllmain.cpp
    tsf/classfactory.h
    tsf/classfactory.cpp
    tsf/catmgr.h
    comp/attribute.h
    comp/cleanup.h
    comp/cleanup.cpp
    comp/edit.h
    comp/finish.h
    comp/layout.h
    comp/presentation.h
    comp/range.h
    comp/start.h
    comp/state.h
    comp/target.h
    comp/update.h
    tsf/register.h
    tsf/register.cpp
    tsf/shifttap.h
    svc/service.h
    svc/service.cpp
    svc/keys.cpp
    svc/keyhandler.cpp
    svc/ctxaccess.cpp
    svc/composition.cpp
    svc/config.cpp
    tsf/dispattr.h
    tsf/dispattr.cpp
    tsf/editsession.h
    tsf/qiqiime.def
    ui/cadifsm.h
    ui/cadifsm.cpp
    ui/candsearch.h
    ui/candsearch.cpp
    ui/candobj.h
    ui/candobj.cpp
    ui/candinput.cpp
    ui/candlayout.cpp
    ui/candpaint.cpp
    ui/candpopup.cpp
    ui/canduia.cpp
    ui/fltbarbtn.h
    ui/fltbarbtn.cpp
    ui/ctxmenu.cpp
    ui/statusbar.h
    ui/statusbar.cpp
    ui/icons.cpp
    ui/tooltips.cpp
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
if(QIQI_METRICS_BENCHMARK_BASELINE)
    set_target_properties(qiqiime PROPERTIES OUTPUT_NAME qiqiime_metrics_baseline)
endif()
target_link_libraries(qiqiime PRIVATE
    qiqi_engine
    dwmapi
    shell32
    ole32
    oleaut32
    uiautomationcore
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
