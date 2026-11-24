// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "broker/logging.h"
#include "common/logger.h"

#include <TraceLoggingProvider.h>
#include <winmeta.h>
#include <gsl/gsl>
#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <new>
#include <span>
#include <stdexcept>
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
SRWLOCK g_diagnostic_log_lock =
    SRWLOCK_INIT;
bool g_diagnostic_write_failed = false;

HRESULT OpenDiagnosticLog() noexcept {
    if (::GetEnvironmentVariableW(
            L"QIQI_CONFIG_BROKER_DIAGNOSTIC_LOG",
            nullptr,
            0) == 0) {
        const DWORD error =
            ::GetLastError();
        return error ==
                ERROR_ENVVAR_NOT_FOUND
            ? S_FALSE
            : HRESULT_FROM_WIN32(error);
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
        const DWORD error =
            length == 0
                ? ::GetLastError()
                : ERROR_INSUFFICIENT_BUFFER;
        return error ==
                ERROR_SUCCESS
            ? E_UNEXPECTED
            : HRESULT_FROM_WIN32(error);
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
            return HRESULT_FROM_WIN32(
                gsl::narrow_cast<DWORD>(
                    error.value()));
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
        return g_diagnostic_log !=
                INVALID_HANDLE_VALUE
            ? S_OK
            : HRESULT_FROM_WIN32(
                  ::GetLastError());
    } catch (const std::bad_alloc&) {
        g_diagnostic_log =
            INVALID_HANDLE_VALUE;
        return E_OUTOFMEMORY;
    } catch (
        const std::length_error&) {
        g_diagnostic_log =
            INVALID_HANDLE_VALUE;
        return HRESULT_FROM_WIN32(
            ERROR_ARITHMETIC_OVERFLOW);
    } catch (
        const std::filesystem::
            filesystem_error& error) {
        g_diagnostic_log =
            INVALID_HANDLE_VALUE;
        return HRESULT_FROM_WIN32(
            gsl::narrow_cast<DWORD>(
                error.code().value()));
    }
}

void WriteFallbackLine(
    std::string_view message) noexcept {
    std::array<char, 1024> line{};
    const size_t available =
        line.size() - 1;
    const size_t copied =
        std::min(
            message.size(),
            available);
    std::copy_n(
        message.begin(),
        copied,
        line.begin());
    gsl::at(line, copied) = '\n';
    ::OutputDebugStringA(
        line.data());
}

void WriteDiagnosticLine(
    std::string_view message) noexcept {
    ::AcquireSRWLockExclusive(
        &g_diagnostic_log_lock);
    const auto unlock =
        gsl::finally(
            []() noexcept {
                ::ReleaseSRWLockExclusive(
                    &g_diagnostic_log_lock);
            });
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
    std::array<char, 4096> line{};
    std::span<char> remaining(
        line);
    size_t length = 0;
    if (prefix > 0) {
        const size_t count =
            std::min(
                gsl::narrow_cast<size_t>(
                    prefix),
                remaining.size());
        std::copy_n(
            timestamp.begin(),
            count,
            remaining.begin());
        remaining =
            remaining.subspan(count);
        length += count;
    }
    const size_t content_capacity =
        remaining.empty()
            ? 0
            : remaining.size() - 1;
    const size_t copy_count =
        std::min(
            message.size(),
            content_capacity);
    std::copy_n(
        message.begin(),
        copy_count,
        remaining.begin());
    length += copy_count;
    gsl::at(line, length) = '\n';
    ++length;
    DWORD written = 0;
    const bool written_ok =
        ::WriteFile(
            g_diagnostic_log,
            line.data(),
            gsl::narrow_cast<DWORD>(
                length),
            &written,
            nullptr);
    const DWORD write_error =
        written_ok
            ? ERROR_SUCCESS
            : ::GetLastError();
    if (!written_ok &&
        !g_diagnostic_write_failed) {
        g_diagnostic_write_failed = true;
        std::array<char, 128>
            failure{};
        std::snprintf(
            failure.data(),
            failure.size(),
            "QiQiIMEConfigBroker: "
            "diagnostic log write failed "
            "(hr=0x%08lX)\n",
            gsl::narrow_cast<
                unsigned long>(
                    HRESULT_FROM_WIN32(
                        write_error)));
        ::OutputDebugStringA(
            failure.data());
    }
}

#pragma warning(push)
#pragma warning(disable : 26447 26812) // TraceLogging generated wrappers are non-throwing COM-free code.
void WriteExternal(
    log::Level level,
    std::string_view message,
    const log::LogContext&) noexcept {
    WriteDiagnosticLine(message);
    if (!g_registered) {
        WriteFallbackLine(message);
        return;
    }
    if (
        message.size() >
        gsl::narrow_cast<size_t>(USHRT_MAX) ||
        kProcessName.size() >
        gsl::narrow_cast<size_t>(USHRT_MAX)) {
        return;
    }
    const int count =
        gsl::narrow_cast<int>(
            message.size());
    const int process_count =
        gsl::narrow_cast<int>(
            kProcessName.size());
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
    const HRESULT diagnostic_result =
        OpenDiagnosticLog();
    const ULONG register_result =
        ::TraceLoggingRegister(
            g_qiqi_config_broker_provider);
    g_registered =
        register_result == ERROR_SUCCESS;
    log::SetExternalLogWriter(
        &WriteExternal);
    if (FAILED(
            diagnostic_result)) {
        QIQI_LOG_CAT_WARN(
            log::Keyword::Config,
            "Broker diagnostic file setup "
            "failed (hr=0x{:08X})",
            gsl::narrow_cast<unsigned long>(
                diagnostic_result));
    }
    if (register_result != ERROR_SUCCESS) {
        std::array<char, 160> message{};
        std::snprintf(
            message.data(),
            message.size(),
            "QiQiIMEConfigBroker: "
            "TraceLoggingRegister failed "
            "(error=%lu)\n",
            gsl::narrow_cast<unsigned long>(
                register_result));
        ::OutputDebugStringA(message.data());
        QIQI_LOG_CAT_ERROR(
            log::Keyword::Config,
            "Broker TraceLogging registration "
            "failed (hr=0x{:08X}); using "
            "fallback diagnostics",
            gsl::narrow_cast<unsigned long>(
                HRESULT_FROM_WIN32(
                    register_result)));
        return HRESULT_FROM_WIN32(
            register_result);
    }
    QIQI_LOG_CAT_INFO(
        log::Keyword::Config,
        "Broker logging initialized "
        "(provider={}, diagnostic_file={})",
        "QiQiIME.ConfigBroker."
        "TraceLogging",
        diagnostic_result == S_OK);
    return S_OK;
}

void Shutdown() noexcept {
    log::SetExternalLogWriter(nullptr);
    {
        ::AcquireSRWLockExclusive(
            &g_diagnostic_log_lock);
        const auto unlock =
            gsl::finally(
                []() noexcept {
                    ::ReleaseSRWLockExclusive(
                        &g_diagnostic_log_lock);
                });
        if (g_diagnostic_log !=
            INVALID_HANDLE_VALUE) {
            ::CloseHandle(
                g_diagnostic_log);
            g_diagnostic_log =
                INVALID_HANDLE_VALUE;
        }
    }
    if (g_registered) {
        ::TraceLoggingUnregister(
            g_qiqi_config_broker_provider);
        g_registered = false;
    }
}

} // namespace qiqi::broker::diag
