set(QIQI_MONITOR_SOURCES
    model.h model.cpp collector.h collector.cpp
    window.h internal.h window.cpp
    ui.cpp view.cpp actions.cpp
)
add_library(qiqi_log_monitor STATIC ${QIQI_MONITOR_SOURCES})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${QIQI_MONITOR_SOURCES})
target_link_libraries(qiqi_log_monitor PUBLIC qiqi_common tdh comdlg32 uxtheme)
target_link_options(qiqi_log_monitor INTERFACE
    "/MANIFESTDEPENDENCY:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'")

set(QIQI_RESOURCE_FILE_TYPE VFT_APP)
set(QIQI_RESOURCE_FILE_SUBTYPE 0x0L)
set(QIQI_RESOURCE_FILENAME "QiQiIMELogMonitor.exe")
set(QIQI_RESOURCE_DESCRIPTION "奇奇日志监控 (QiQiIME Log Monitor - ${QIQIIME_ARCH_NAME})")
set(QIQI_RESOURCE_COMMENTS "QiQiIME real-time ETW log monitor")
set(QIQI_RESOURCE_ICON "${PROJECT_SOURCE_DIR}/data/qiqiime-monitor.ico")
set(QIQI_MONITOR_RESOURCE "${QIQIIME_GENERATED_DIR}/tools/log_monitor/version.rc")
configure_file("${PROJECT_SOURCE_DIR}/src/common/version.rc.in" "${QIQI_MONITOR_RESOURCE}" @ONLY)
set_source_files_properties("${QIQI_MONITOR_RESOURCE}"
    PROPERTIES OBJECT_DEPENDS "${QIQI_RESOURCE_ICON}")
add_executable(QiQiIMELogMonitor WIN32 main.cpp "${QIQI_MONITOR_RESOURCE}")
target_link_libraries(QiQiIMELogMonitor PRIVATE qiqi_log_monitor)
