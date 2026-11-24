// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "diagnostics.h"

#include <appmodel.h>
#include <TraceLoggingProvider.h>
#include <winmeta.h>

#include <filesystem>
#include <format>
#include <string>

#pragma warning(push)
#pragma warning(disable : 26447 26477 26812)
TRACELOGGING_DEFINE_PROVIDER(
    g_qiqi_etw_provider,
    "QiQiIME.TraceLogging",
    (0x8e7b9d31, 0x5a42, 0x4c81, 0x9f, 0x11, 0x20, 0x26, 0x10, 0x29, 0x00, 0x99));
#pragma warning(pop)

namespace qiqi::host::fulltrust {
namespace {

constexpr uint64_t kGeneralKeyword = 0x0001;
constexpr uint64_t kInputKeyword = 0x0004;
constexpr uint8_t kInfoLevel = WINEVENT_LEVEL_INFO;
constexpr uint8_t kWarningLevel = WINEVENT_LEVEL_WARNING;
constexpr uint8_t kErrorLevel = WINEVENT_LEVEL_ERROR;
constexpr std::wstring_view kProcessName =
    L"QiQiImePackagedFullTrustHost.exe";
constexpr std::wstring_view kLogFileName =
    L"qiqi-fulltrust-host.log";

template <uint8_t EventLevel, uint64_t Keyword>
void EmitEtw(std::wstring_view message) noexcept {
    if constexpr (EventLevel == kErrorLevel) {
        TraceLoggingWrite(
            g_qiqi_etw_provider,
            "QiQiLogError",
            TraceLoggingLevel(kErrorLevel),
            TraceLoggingKeyword(Keyword),
            TraceLoggingCountedWideString(
                kProcessName.data(),
                static_cast<USHORT>(kProcessName.size()),
                "process"),
            TraceLoggingCountedWideString(
                message.data(),
                static_cast<USHORT>(message.size()),
                "message"));
    } else if constexpr (EventLevel == kWarningLevel) {
        TraceLoggingWrite(
            g_qiqi_etw_provider,
            "QiQiLogWarning",
            TraceLoggingLevel(kWarningLevel),
            TraceLoggingKeyword(Keyword),
            TraceLoggingCountedWideString(
                kProcessName.data(),
                static_cast<USHORT>(kProcessName.size()),
                "process"),
            TraceLoggingCountedWideString(
                message.data(),
                static_cast<USHORT>(message.size()),
                "message"));
    } else {
        TraceLoggingWrite(
            g_qiqi_etw_provider,
            "QiQiLogInfo",
            TraceLoggingLevel(kInfoLevel),
            TraceLoggingKeyword(Keyword),
            TraceLoggingCountedWideString(
                kProcessName.data(),
                static_cast<USHORT>(kProcessName.size()),
                "process"),
            TraceLoggingCountedWideString(
                message.data(),
                static_cast<USHORT>(message.size()),
                "message"));
    }
}

std::string ToUtf8(std::wstring_view text) {
    if (text.empty()) {
        return {};
    }
    const int count = ::WideCharToMultiByte(
        CP_UTF8,
        0,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0,
        nullptr,
        nullptr);
    if (count <= 0) {
        return {};
    }
    std::string result(static_cast<size_t>(count), '\0');
    const int converted = ::WideCharToMultiByte(
        CP_UTF8,
        0,
        text.data(),
        static_cast<int>(text.size()),
        result.data(),
        count,
        nullptr,
        nullptr);
    if (converted != count) {
        return {};
    }
    return result;
}

std::wstring Timestamp(std::wstring_view message) {
    SYSTEMTIME now{};
    ::GetLocalTime(&now);
    return std::format(
        L"{:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:03} {}\r\n",
        now.wYear,
        now.wMonth,
        now.wDay,
        now.wHour,
        now.wMinute,
        now.wSecond,
        now.wMilliseconds,
        message);
}

} // namespace

Diagnostics::Diagnostics() noexcept
    : registration_error_(::TraceLoggingRegister(g_qiqi_etw_provider)) {
    std::ignore = EnsureLogFile();
}

Diagnostics::~Diagnostics() noexcept {
    if (log_file_ != INVALID_HANDLE_VALUE) {
        ::CloseHandle(log_file_);
        log_file_ = INVALID_HANDLE_VALUE;
    }
    if (registration_error_ == ERROR_SUCCESS) {
        ::TraceLoggingUnregister(g_qiqi_etw_provider);
    }
}

void Diagnostics::Info(
    std::wstring_view stage,
    std::wstring_view detail,
    DiagnosticArea area) noexcept {
    Write(kInfoLevel, area, stage, detail);
}

void Diagnostics::Warning(
    std::wstring_view stage,
    std::wstring_view detail,
    DiagnosticArea area) noexcept {
    Write(kWarningLevel, area, stage, detail);
}

void Diagnostics::Error(
    std::wstring_view stage,
    std::wstring_view detail,
    DiagnosticArea area) noexcept {
    Write(kErrorLevel, area, stage, detail);
}

void Diagnostics::Write(
    uint8_t level,
    DiagnosticArea area,
    std::wstring_view stage,
    std::wstring_view detail) noexcept {
    try {
        const std::wstring message = std::format(L"{}: {}", stage, detail);
        if (registration_error_ == ERROR_SUCCESS) {
            if (level == kErrorLevel) {
                if (area == DiagnosticArea::Input) {
                    EmitEtw<kErrorLevel, kInputKeyword>(message);
                } else {
                    EmitEtw<kErrorLevel, kGeneralKeyword>(message);
                }
            } else if (level == kWarningLevel) {
                if (area == DiagnosticArea::Input) {
                    EmitEtw<kWarningLevel, kInputKeyword>(message);
                } else {
                    EmitEtw<kWarningLevel, kGeneralKeyword>(message);
                }
            } else if (area == DiagnosticArea::Input) {
                EmitEtw<kInfoLevel, kInputKeyword>(message);
            } else {
                EmitEtw<kInfoLevel, kGeneralKeyword>(message);
            }
        }
        AppendPersistent(Timestamp(message));
    } catch (const std::bad_alloc&) {
        if (registration_error_ == ERROR_SUCCESS) {
            EmitEtw<kErrorLevel, kGeneralKeyword>(
                L"diagnostics: event allocation failed");
        }
    }
}

bool Diagnostics::EnsureLogFile() noexcept {
    if (log_file_ != INVALID_HANDLE_VALUE) {
        return true;
    }
    try {
        std::wstring local_app_data(
            static_cast<size_t>(::GetEnvironmentVariableW(
                L"LOCALAPPDATA",
                nullptr,
                0)) + 1,
            L'\0');
        const DWORD size = ::GetEnvironmentVariableW(
            L"LOCALAPPDATA",
            local_app_data.data(),
            static_cast<DWORD>(local_app_data.size()));
        if (size == 0 || size >= local_app_data.size()) {
            ReportPersistentFailure(
                ::GetLastError(),
                L"GetEnvironmentVariableW");
            return false;
        }
        local_app_data.resize(size);
        std::filesystem::path directory;
        uint32_t family_size = 0;
        const LONG family_error = ::GetCurrentPackageFamilyName(
            &family_size,
            nullptr);
        if (family_error == ERROR_INSUFFICIENT_BUFFER &&
            family_size != 0) {
            std::wstring family_name(
                static_cast<size_t>(family_size),
                L'\0');
            const LONG read_family_error =
                ::GetCurrentPackageFamilyName(
                    &family_size,
                    family_name.data());
            if (read_family_error == ERROR_SUCCESS) {
                family_name.resize(
                    std::wcslen(family_name.c_str()));
                directory =
                    std::filesystem::path(local_app_data) /
                    L"Packages" /
                    family_name /
                    L"LocalState";
            }
        }
        if (directory.empty()) {
            directory =
                std::filesystem::path(local_app_data) /
                L"QiQiImePackagedFullTrustHost";
        }
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        if (error) {
            ReportPersistentFailure(
                static_cast<DWORD>(error.value()),
                L"create_directories");
            return false;
        }
        const std::filesystem::path path = directory / kLogFileName;
        log_file_ = ::CreateFileW(
            path.c_str(),
            FILE_APPEND_DATA,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (log_file_ == INVALID_HANDLE_VALUE) {
            ReportPersistentFailure(::GetLastError(), L"CreateFileW");
            return false;
        }
        persistent_error_ = ERROR_SUCCESS;
        return true;
    } catch (const std::bad_alloc&) {
        persistent_error_ = ERROR_NOT_ENOUGH_MEMORY;
        if (registration_error_ == ERROR_SUCCESS) {
            EmitEtw<kErrorLevel, kGeneralKeyword>(
                L"diagnostics: persistent log allocation failed");
        }
        return false;
    } catch (const std::filesystem::filesystem_error& error) {
        ReportPersistentFailure(
            static_cast<DWORD>(error.code().value()),
            L"filesystem");
        return false;
    }
}

void Diagnostics::AppendPersistent(std::wstring_view message) noexcept {
    if (!EnsureLogFile()) {
        return;
    }
    try {
        const std::string bytes = ToUtf8(message);
        if (bytes.empty() && !message.empty()) {
            ReportPersistentFailure(
                ERROR_NO_UNICODE_TRANSLATION,
                L"WideCharToMultiByte");
            return;
        }
        DWORD written = 0;
        if (!::WriteFile(
                log_file_,
                bytes.data(),
                static_cast<DWORD>(bytes.size()),
                &written,
                nullptr)) {
            ReportPersistentFailure(::GetLastError(), L"WriteFile");
            return;
        }
        if (written != bytes.size()) {
            ReportPersistentFailure(ERROR_WRITE_FAULT, L"WriteFile");
            return;
        }
        persistent_error_ = ERROR_SUCCESS;
    } catch (const std::bad_alloc&) {
        persistent_error_ = ERROR_NOT_ENOUGH_MEMORY;
    }
}

void Diagnostics::ReportPersistentFailure(
    DWORD error,
    std::wstring_view operation) noexcept {
    persistent_error_ = error;
    if (registration_error_ != ERROR_SUCCESS) {
        return;
    }
    try {
        EmitEtw<kErrorLevel, kGeneralKeyword>(std::format(
            L"diagnostics: persistent operation={} error={}",
            operation,
            error));
    } catch (const std::bad_alloc&) {
        EmitEtw<kErrorLevel, kGeneralKeyword>(
            L"diagnostics: persistent log error; detail allocation failed");
    }
}

std::wstring_view Diagnostics::PersistentLogFileName() const noexcept {
    return kLogFileName;
}

} // namespace qiqi::host::fulltrust

