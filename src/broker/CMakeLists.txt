# SPDX-FileCopyrightText: 2026 cloudfyy
# SPDX-License-Identifier: GPL-3.0-or-later

enable_language(C)

find_program(QIQI_MIDL_EXECUTABLE
    NAMES midl.exe midl
    PATHS
        "$ENV{ProgramFiles\(x86\)}/Windows Kits/10/bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/x64"
        "$ENV{ProgramFiles\(x86\)}/Windows Kits/10/bin/x64"
    REQUIRED)

set(QIQI_BROKER_MIDL_DIRECTORY
    "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(QIQI_BROKER_RPC_HEADER
    "${QIQI_BROKER_MIDL_DIRECTORY}/configbroker.h")
set(QIQI_BROKER_RPC_IID
    "${QIQI_BROKER_MIDL_DIRECTORY}/configbroker_i.c")
set(QIQI_BROKER_RPC_PROXY
    "${QIQI_BROKER_MIDL_DIRECTORY}/configbroker_p.c")
set(QIQI_BROKER_RPC_DATA
    "${QIQI_BROKER_MIDL_DIRECTORY}/dlldata.c")
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(QIQI_BROKER_MIDL_ENV x64)
else()
    set(QIQI_BROKER_MIDL_ENV win32)
endif()
add_custom_command(
    OUTPUT
        "${QIQI_BROKER_RPC_HEADER}"
        "${QIQI_BROKER_RPC_IID}"
        "${QIQI_BROKER_RPC_PROXY}"
        "${QIQI_BROKER_RPC_DATA}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory
        "${QIQI_BROKER_MIDL_DIRECTORY}"
    COMMAND "${QIQI_MIDL_EXECUTABLE}"
        /nologo
        /W4
        /char signed
        /env ${QIQI_BROKER_MIDL_ENV}
        /h "${QIQI_BROKER_RPC_HEADER}"
        /iid "${QIQI_BROKER_RPC_IID}"
        /proxy "${QIQI_BROKER_RPC_PROXY}"
        /dlldata "${QIQI_BROKER_RPC_DATA}"
        /out "${QIQI_BROKER_MIDL_DIRECTORY}"
        "${CMAKE_CURRENT_SOURCE_DIR}/configbroker.idl"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/configbroker.idl"
    COMMENT "Generating QiQi configuration broker proxy/stub"
    VERBATIM
)
add_custom_target(qiqi_generate_config_broker_rpc
    DEPENDS
        "${QIQI_BROKER_RPC_HEADER}"
        "${QIQI_BROKER_RPC_IID}"
        "${QIQI_BROKER_RPC_PROXY}"
        "${QIQI_BROKER_RPC_DATA}"
)

add_library(QiQiIMEConfigProxy SHARED
    "${QIQI_BROKER_RPC_IID}"
    "${QIQI_BROKER_RPC_PROXY}"
    "${QIQI_BROKER_RPC_DATA}"
    QiQiIMEConfigProxy.def
)
add_dependencies(QiQiIMEConfigProxy
    qiqi_generate_config_broker_rpc)
target_include_directories(QiQiIMEConfigProxy PRIVATE
    "${QIQI_BROKER_MIDL_DIRECTORY}"
)
target_compile_definitions(QiQiIMEConfigProxy PRIVATE
    REGISTER_PROXY_DLL
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)
target_compile_options(QiQiIMEConfigProxy PRIVATE
    /W3
)
target_link_libraries(QiQiIMEConfigProxy PRIVATE
    rpcrt4
    rpcns4
)
set_property(TARGET QiQiIMEConfigProxy PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_library(qiqi_config_broker_store STATIC
    configstore.cpp
    configstore.h
)
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    configstore.cpp
    configstore.h
)
target_include_directories(qiqi_config_broker_store PUBLIC
    "${PROJECT_SOURCE_DIR}/src"
)
target_link_libraries(qiqi_config_broker_store PUBLIC
    qiqi_common
)

set(QIQI_RESOURCE_FILE_TYPE VFT_APP)
set(QIQI_RESOURCE_FILE_SUBTYPE 0x0L)
set(QIQI_RESOURCE_FILENAME "QiQiIMEConfigBroker.exe")
set(QIQI_RESOURCE_DESCRIPTION "QiQiIME Configuration Broker")
set(QIQI_RESOURCE_COMMENTS "QiQiIME Configuration Broker - https://github.com/cloudfyy/qiqiime")
set(QIQI_RESOURCE_ICON "${PROJECT_SOURCE_DIR}/data/qiqiime-broker.ico")
set(QIQI_BROKER_VERSION_RESOURCE
    "${QIQIIME_GENERATED_DIR}/broker/version.rc")
configure_file(
    "${PROJECT_SOURCE_DIR}/src/common/version.rc.in"
    "${QIQI_BROKER_VERSION_RESOURCE}"
    @ONLY
)
set_source_files_properties(
    "${QIQI_BROKER_VERSION_RESOURCE}"
    PROPERTIES OBJECT_DEPENDS
        "${QIQI_RESOURCE_ICON}"
)

add_executable(QiQiIMEConfigBroker WIN32
    logging.cpp
    logging.h
    main.cpp
    registration.cpp
    registration.h
    runtime.cpp
    runtime.h
    server.cpp
    server.h
    "${QIQI_BROKER_VERSION_RESOURCE}"
)
target_include_directories(
    QiQiIMEConfigBroker PRIVATE
    "${PROJECT_SOURCE_DIR}/src"
    "${QIQIIME_GENERATED_DIR}"
)
target_link_libraries(
    QiQiIMEConfigBroker PRIVATE
    qiqi_config_broker_store
    advapi32
    ole32
    oleaut32
    OneCoreUAP
    rpcrt4
    shell32
    uuid
)
set_target_properties(
    QiQiIMEConfigBroker PROPERTIES
    OUTPUT_NAME "QiQiIMEConfigBroker"
)
