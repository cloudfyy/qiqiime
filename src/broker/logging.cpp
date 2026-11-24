// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "broker/logging.h"
#include "common/logger.h"

#include <TraceLoggingProvider.h>
#include <winmeta.h>
#include <gsl/gsl>
#include <array>
#include <cstdio>
#include <filesystem>
#include <string>

#pragma warning(push)
#pragma warning(disable : 26447 26477 26812)
TRACELOGGING_DEFINE_PROVIDER(
    g_qiqi_config_broker_provider,
    "QiQiIME.ConfigBroker.TraceLogging",
    (0x8e7b9d31, 0x5a42, 0x4c81, 0x9f,
     0x11, 0x20, 0x26, 0x10, 0x29, 0x00,
     0x11));
#pragma warning(pop)

namespace qiqi::broker::diag {
namespace {

constexpr std::string_view kProcessName =
    "QiQiIMEConfigBroker.exe";
constexpr uint64_t kConfigKeyword = 0x0080;
bool g_registered = false;
HANDLE g_diagnostic_log =
    INVALID_HANDLE_VALUE;

void OpenDiagnosticLog() noexcept {
    if (::GetEnvironmentVariableW(
            L"QIQI_CONFIG_BROKER_DIAGNOSTIC_LOG",
            nullptr,
            0) == 0) {
        return;
    }
    std::array<wchar_t, 32768> path{};
    const DWORD length =
        ::GetEnvironmentVariableW(
            L"LOCALAPPDATA",
            path.data(),
            gsl::narrow_cast<DWORD>(
                path.size()));
    if (length == 0 ||
        length >= path.size()) {
        return;
    }
    try {
        const auto directory =
            std::filesystem::path(
                path.data()) /
            L"QiQiIME" /
            L"ConfigBroker";
        std::error_code error;
        std::filesystem::create_directories(
            directory,
            error);
        if (error) {
            return;
        }
        g_diagnostic_log =
            ::CreateFileW(
                (directory /
                 L"broker-test.log").c_str(),
                FILE_APPEND_DATA,
                FILE_SHARE_READ |
                    FILE_SHARE_WRITE,
                nullptr,
                OPEN_ALWAYS,
                FILE_ATTRIBUTE_NORMAL,
                nullptr);
    } catch (...) {
        g_diagnostic_log =
            INVALID_HANDLE_VALUE;
    }
}

void WriteDiagnosticLine(
    std::string_view message) noexcept {
    if (g_diagnostic_log ==
        INVALID_HANDLE_VALUE) {
        return;
    }
    SYSTEMTIME time{};
    ::GetLocalTime(&time);
    std::array<char, 96> timestamp{};
    const int prefix =
        std::snprintf(
            timestamp.data(),
            timestamp.size(),
            "%04u-%02u-%02u "
            "%02u:%02u:%02u.%03u ",
            gsl::narrow_cast<unsigned>(
                time.wYear),
            gsl::narrow_cast<unsigned>(
                time.wMonth),
            gsl::narrow_cast<unsigned>(
                time.wDay),
            gsl::narrow_cast<unsigned>(
                time.wHour),
            gsl::narrow_cast<unsigned>(
                time.wMinute),
            gsl::narrow_cast<unsigned>(
                time.wSecond),
            gsl::narrow_cast<unsigned>(
                time.wMilliseconds));
    DWORD written = 0;
    if (prefix > 0) {
        ::WriteFile(
            g_diagnostic_log,
            timestamp.data(),
            gsl::narrow_cast<DWORD>(prefix),
            &written,
            nullptr);
    }
    ::WriteFile(
        g_diagnostic_log,
        message.data(),
        gsl::narrow_cast<DWORD>(
            message.size()),
        &written,
        nullptr);
    constexpr char newline = '\n';
    ::WriteFile(
        g_diagnostic_log,
        &newline,
        1,
        &written,
        nullptr);
}

#pragma warning(push)
#pragma warning(disable : 26447 26812) // TraceLogging generated wrappers are non-throwing COM-free code.
void WriteExternal(
    log::Level level,
    std::string_view message,
    const log::LogContext&) noexcept {
    WriteDiagnosticLine(message);
    if (!g_registered ||
        message.size() >
        gsl::narrow_cast<size_t>(USHRT_MAX) ||
        kProcessName.size() >
        gsl::narrow_cast<size_t>(USHRT_MAX)) {
        return;
    }
    const int count =
    gsl::narrow_cast<int>(message.size());
    const int process_count =
    gsl::narrow_cast<int>(kProcessName.size());
    switch (level) {
        case log::Level::trace:
        case log::Level::debug:
            TraceLoggingWrite(
                g_qiqi_config_broker_provider,
                "QiQiConfigBrokerVerbose",
                TraceLoggingLevel(
                    WINEVENT_LEVEL_VERBOSE),
                TraceLoggingKeyword(
                    kConfigKeyword),
                TraceLoggingCountedString(
                    kProcessName.data(),
                    process_count,
                    "process"),
                TraceLoggingCountedString(
                    message.data(),
                    count,
                    "message"));
            break;
        case log::Level::info:
            TraceLoggingWrite(
                g_qiqi_config_broker_provider,
                "QiQiConfigBrokerInfo",
                TraceLoggingLevel(
                    WINEVENT_LEVEL_INFO),
                TraceLoggingKeyword(
                    kConfigKeyword),
                TraceLoggingCountedString(
                    kProcessName.data(),
                    process_count,
                    "process"),
                TraceLoggingCountedString(
                    message.data(),
                    count,
                    "message"));
            break;
        case log::Level::warn:
            TraceLoggingWrite(
                g_qiqi_config_broker_provider,
                "QiQiConfigBrokerWarning",
                TraceLoggingLevel(
                    WINEVENT_LEVEL_WARNING),
                TraceLoggingKeyword(
                    kConfigKeyword),
                TraceLoggingCountedString(
                    kProcessName.data(),
                    process_count,
                    "process"),
                TraceLoggingCountedString(
                    message.data(),
                    count,
                    "message"));
            break;
        case log::Level::err:
        case log::Level::critical:
            TraceLoggingWrite(
                g_qiqi_config_broker_provider,
                "QiQiConfigBrokerError",
                TraceLoggingLevel(
                    WINEVENT_LEVEL_ERROR),
                TraceLoggingKeyword(
                    kConfigKeyword),
                TraceLoggingCountedString(
                    kProcessName.data(),
                    process_count,
                    "process"),
                TraceLoggingCountedString(
                    message.data(),
                    count,
                    "message"));
            break;
    }
}
#pragma warning(pop)

} // namespace

HRESULT Initialize() noexcept {
    const ULONG result =
        ::TraceLoggingRegister(
            g_qiqi_config_broker_provider);
    if (result != ERROR_SUCCESS) {
        std::array<char, 160> message{};
        std::snprintf(
            message.data(),
            message.size(),
            "QiQiIMEConfigBroker: "
            "TraceLoggingRegister failed "
            "(hr=0x%08lX)\n",
            gsl::narrow_cast<unsigned long>(
                result));
        ::OutputDebugStringA(message.data());
        return HRESULT_FROM_WIN32(result);
    }
    g_registered = true;
    OpenDiagnosticLog();
    log::SetExternalLogWriter(
        &WriteExternal);
    return S_OK;
}

void Shutdown() noexcept {
    log::SetExternalLogWriter(nullptr);
    if (g_diagnostic_log !=
        INVALID_HANDLE_VALUE) {
        ::CloseHandle(
            g_diagnostic_log);
        g_diagnostic_log =
            INVALID_HANDLE_VALUE;
    }
    if (g_registered) {
        ::TraceLoggingUnregister(
            g_qiqi_config_broker_provider);
        g_registered = false;
    }
}

} // namespace qiqi::broker::diag
