add_subdirectory(common)
add_subdirectory(ime)
add_subdirectory(tools)

find_program(QIQI_POWERSHELL_EXECUTABLE NAMES pwsh powershell REQUIRED)
add_test(NAME QiQiInstallerShortcutsTest
    COMMAND "${QIQI_POWERSHELL_EXECUTABLE}"
        -NoLogo -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_CURRENT_SOURCE_DIR}/installer/shortcuts.ps1"
)
add_test(NAME QiQiRuntimeArchitectureTest
    COMMAND "${QIQI_POWERSHELL_EXECUTABLE}"
        -NoLogo -NoProfile -ExecutionPolicy Bypass
        -File "${CMAKE_CURRENT_SOURCE_DIR}/installer/runtime.ps1"
)
