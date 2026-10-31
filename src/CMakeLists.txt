set(QIQIIME_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")

add_subdirectory(common)
add_subdirectory(tools)
add_subdirectory(Ime)

set_source_files_properties("${QIQIIME_GENERATED_DIR}/common/version.rc"
    TARGET_DIRECTORY qiqiime QiQiIMESettings
    PROPERTIES OBJECT_DEPENDS "${PROJECT_SOURCE_DIR}/data/qiqiime.ico"
)
