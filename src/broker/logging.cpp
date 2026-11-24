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

std::array<char, 256> FormatTraceMetadata(
    const log::LogContext& context) noexcept {
    std::array<char, 256> metadata{};
    if ((!context.trace_id ||
         *context.trace_id == GUID_NULL) &&
        (!context.activity_id ||
         *context.activity_id == GUID_NULL) &&
        (!context.related_activity_id ||
         *context.related_activity_id ==
             GUID_NULL)) {
        return metadata;
    }
    const auto format_guid =
        [](const GUID* value,
           std::span<char> destination) noexcept {
            if (destination.empty()) {
                return;
            }
            if (!value ||
                *value == GUID_NULL) {
                destination.front() = '\0';
                return;
            }
            std::snprintf(
                destination.data(),
                destination.size(),
                "{%08lX-%04X-%04X-"
                "%02X%02X-%02X%02X%02X"
                "%02X%02X%02X}",
                gsl::narrow_cast<unsigned long>(
                    value->Data1),
                gsl::narrow_cast<unsigned>(
                    value->Data2),
                gsl::narrow_cast<unsigned>(
                    value->Data3),
                gsl::narrow_cast<unsigned>(
                    gsl::at(
                        value->Data4,
                        0)),
                gsl::narrow_cast<unsigned>(
                    gsl::at(
                        value->Data4,
                        1)),
                gsl::narrow_cast<unsigned>(
                    gsl::at(
                        value->Data4,
                        2)),
                gsl::narrow_cast<unsigned>(
                    gsl::at(
                        value->Data4,
                        3)),
                gsl::narrow_cast<unsigned>(
                    gsl::at(
                        value->Data4,
                        4)),
                gsl::narrow_cast<unsigned>(
                    gsl::at(
                        value->Data4,
                        5)),
                gsl::narrow_cast<unsigned>(
                    gsl::at(
                        value->Data4,
                        6)),
                gsl::narrow_cast<unsigned>(
                    gsl::at(
                        value->Data4,
                        7)));
        };
    std::array<char, 40> trace{};
    std::array<char, 40> activity{};
    std::array<char, 40> related{};
    format_guid(
        context.trace_id,
        trace);
    format_guid(
        context.activity_id,
        activity);
    format_guid(
        context.related_activity_id,
        related);
    std::snprintf(
        metadata.data(),
        metadata.size(),
        " [trace=%s activity=%s "
        "related=%s]",
        trace.data(),
        activity.data(),
        related.data());
    return metadata;
}

void WriteFallbackLine(
    std::string_view message,
    const log::LogContext& context) noexcept {
    std::array<char, 4096> line{};
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
    gsl::at(line, copied) = ' ';
    const std::array<char, 256>
        metadata =
            FormatTraceMetadata(context);
    const size_t metadata_length =
        std::char_traits<char>::length(
            metadata.data());
    const size_t metadata_offset =
        std::min(
            copied + 1,
            available);
    const size_t metadata_copy =
        std::min(
            metadata_length,
            available -
                metadata_offset);
    std::copy_n(
        metadata.begin(),
        metadata_copy,
        line.begin() +
            metadata_offset);
    const size_t length =
        std::min(
            copied +
                1 +
                metadata_copy,
            available);
    gsl::at(line, length) = '\n';
    ::OutputDebugStringA(
        line.data());
}

void WriteDiagnosticLine(
    std::string_view message,
    const log::LogContext& context) noexcept {
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
    remaining =
        remaining.subspan(copy_count);
    const auto metadata =
        FormatTraceMetadata(context);
    const size_t metadata_length =
        std::char_traits<char>::length(
            metadata.data());
    const size_t metadata_capacity =
        remaining.empty()
            ? 0
            : remaining.size() - 1;
    const size_t metadata_copy =
        std::min(
            metadata_length,
            metadata_capacity);
    std::copy_n(
        metadata.begin(),
        metadata_copy,
        remaining.begin());
    length += metadata_copy;
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
template <log::Opcode Op>
void WriteTraceEvent(
    log::Level level,
    std::string_view message,
    int count,
    int process_count,
    const GUID* activity,
    const GUID* related,
    const GUID& trace) noexcept {
    constexpr UCHAR kOpcode =
        std::to_underlying(Op);
    switch (level) {
        case log::Level::trace:
        case log::Level::debug:
            TraceLoggingWriteActivity(
                g_qiqi_config_broker_provider,
                "QiQiConfigBrokerVerbose",
                activity,
                related,
                TraceLoggingLevel(
                    WINEVENT_LEVEL_VERBOSE),
                TraceLoggingKeyword(
                    kConfigKeyword),
                TraceLoggingOpcode(kOpcode),
                TraceLoggingCountedString(
                    kProcessName.data(),
                    process_count,
                    "process"),
                TraceLoggingCountedString(
                    message.data(),
                    count,
                    "message"),
                TraceLoggingGuid(
                    trace,
                    "trace_id"));
            break;
        case log::Level::info:
            TraceLoggingWriteActivity(
                g_qiqi_config_broker_provider,
                "QiQiConfigBrokerInfo",
                activity,
                related,
                TraceLoggingLevel(
                    WINEVENT_LEVEL_INFO),
                TraceLoggingKeyword(
                    kConfigKeyword),
                TraceLoggingOpcode(kOpcode),
                TraceLoggingCountedString(
                    kProcessName.data(),
                    process_count,
                    "process"),
                TraceLoggingCountedString(
                    message.data(),
                    count,
                    "message"),
                TraceLoggingGuid(
                    trace,
                    "trace_id"));
            break;
        case log::Level::warn:
            TraceLoggingWriteActivity(
                g_qiqi_config_broker_provider,
                "QiQiConfigBrokerWarning",
                activity,
                related,
                TraceLoggingLevel(
                    WINEVENT_LEVEL_WARNING),
                TraceLoggingKeyword(
                    kConfigKeyword),
                TraceLoggingOpcode(kOpcode),
                TraceLoggingCountedString(
                    kProcessName.data(),
                    process_count,
                    "process"),
                TraceLoggingCountedString(
                    message.data(),
                    count,
                    "message"),
                TraceLoggingGuid(
                    trace,
                    "trace_id"));
            break;
        case log::Level::err:
        case log::Level::critical:
            TraceLoggingWriteActivity(
                g_qiqi_config_broker_provider,
                "QiQiConfigBrokerError",
                activity,
                related,
                TraceLoggingLevel(
                    WINEVENT_LEVEL_ERROR),
                TraceLoggingKeyword(
                    kConfigKeyword),
                TraceLoggingOpcode(kOpcode),
                TraceLoggingCountedString(
                    kProcessName.data(),
                    process_count,
                    "process"),
                TraceLoggingCountedString(
                    message.data(),
                    count,
                    "message"),
                TraceLoggingGuid(
                    trace,
                    "trace_id"));
            break;
        case log::Level::off:
            break;
    }
}

void WriteExternal(
    log::Level level,
    std::string_view message,
    const log::LogContext& context) noexcept {
    WriteDiagnosticLine(
        message,
        context);
    if (!g_registered) {
        WriteFallbackLine(
            message,
            context);
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
    const GUID* const activity =
        context.activity_id
        ? context.activity_id
        : &GUID_NULL;
    const GUID* const related =
        context.related_activity_id &&
            *context
                     .related_activity_id !=
                GUID_NULL
        ? context
              .related_activity_id
        : nullptr;
    const GUID trace =
        context.trace_id
        ? *context.trace_id
        : GUID_NULL;
    switch (log::NormalizeOpcode(
        context.opcode)) {
        case log::Opcode::Start:
            WriteTraceEvent<
                log::Opcode::Start>(
                level,
                message,
                count,
                process_count,
                activity,
                related,
                trace);
            break;
        case log::Opcode::Stop:
            WriteTraceEvent<
                log::Opcode::Stop>(
                level,
                message,
                count,
                process_count,
                activity,
                related,
                trace);
            break;
        case log::Opcode::Info:
            WriteTraceEvent<
                log::Opcode::Info>(
                level,
                message,
                count,
                process_count,
                activity,
                related,
                trace);
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
