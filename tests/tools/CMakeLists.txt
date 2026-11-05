add_subdirectory(settings)
add_subdirectory(log_monitor)
add_subdirectory(dict_manager)

find_program(QIQI_POWERSHELL_EXECUTABLE NAMES pwsh powershell REQUIRED)
add_test(NAME QiQiStandaloneToolsTest
    COMMAND "${QIQI_POWERSHELL_EXECUTABLE}"
        -NoLogo -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_CURRENT_SOURCE_DIR}/startup.ps1"
        -BinaryDirectory "$<TARGET_FILE_DIR:QiQiIMESettings>"
)
set_tests_properties(QiQiStandaloneToolsTest PROPERTIES RUN_SERIAL TRUE TIMEOUT 150)
