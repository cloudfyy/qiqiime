// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "diagnostics.h"

#include <TraceLoggingProvider.h>
#include <winmeta.h>
#include <winrt/Windows.Foundation.h>

#include <format>
#include <new>
#include <utility>

#pragma warning(push)
#pragma warning(disable : 26447 26477 26812)
TRACELOGGING_DEFINE_PROVIDER(
    g_qiqi_etw_provider,
    "QiQiIME.TraceLogging",
    (0x8e7b9d31, 0x5a42, 0x4c81, 0x9f, 0x11, 0x20, 0x26, 0x10, 0x29, 0x00, 0x99));
#pragma warning(pop)

namespace qiqi::host {
namespace {

constexpr uint64_t kGeneralKeyword = 0x0001;
constexpr uint64_t kInputKeyword = 0x0004;
constexpr uint8_t kInfoLevel = WINEVENT_LEVEL_INFO;
constexpr uint8_t kWarningLevel = WINEVENT_LEVEL_WARNING;
constexpr uint8_t kErrorLevel = WINEVENT_LEVEL_ERROR;
constexpr std::wstring_view kProcessName = L"QiQiImeUwpSearchHost.exe";
constexpr std::wstring_view kPersistentLogName = L"qiqi-uwp-search-host.log";

template <uint8_t EventLevel, uint64_t Keyword>
void EmitEtw(std::wstring_view message) noexcept {
    if constexpr (EventLevel == kErrorLevel) {
        TraceLoggingWrite(
            g_qiqi_etw_provider,
            "QiQiLogError",
            TraceLoggingLevel(EventLevel),
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
            TraceLoggingLevel(EventLevel),
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
            TraceLoggingLevel(EventLevel),
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

} // namespace

Diagnostics::Diagnostics() noexcept
    : registration_error_(::TraceLoggingRegister(g_qiqi_etw_provider)) {}

Diagnostics::~Diagnostics() noexcept {
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
        std::wstring message = std::format(L"{}: {}", stage, detail);
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
        QueuePersistent(Timestamp(message));
    } catch (const std::bad_alloc&) {
        if (registration_error_ == ERROR_SUCCESS) {
            EmitEtw<kErrorLevel, kGeneralKeyword>(
                L"diagnostics: allocation failure while formatting host event");
        }
    }
}

void Diagnostics::QueuePersistent(std::wstring message) noexcept {
    try {
        pending_.push_back(std::move(message));
        if (!flush_started_) {
            flush_started_ = true;
            FlushPersistentAsync();
        }
    } catch (const std::bad_alloc&) {
        if (registration_error_ == ERROR_SUCCESS) {
            EmitEtw<kErrorLevel, kGeneralKeyword>(
                L"diagnostics: allocation failure while queuing persistent log");
        }
        persistent_error_.store(E_OUTOFMEMORY, std::memory_order_relaxed);
    }
}

winrt::fire_and_forget Diagnostics::FlushPersistentAsync() {
    const auto self = shared_from_this();
    for (;;) {
        std::vector<std::wstring> batch;
        batch.swap(pending_);
        if (batch.empty()) {
            flush_started_ = false;
            if (!pending_.empty()) {
                flush_started_ = true;
                continue;
            }
            co_return;
        }

        std::wstring text;
        try {
            size_t size = 0;
            for (const auto& line : batch) {
                size += line.size();
            }
            text.reserve(size);
            for (const auto& line : batch) {
                text += line;
            }
        } catch (const std::bad_alloc&) {
            persistent_error_.store(E_OUTOFMEMORY, std::memory_order_relaxed);
            if (registration_error_ == ERROR_SUCCESS) {
                EmitEtw<kErrorLevel, kGeneralKeyword>(
                    L"diagnostics: allocation failure while batching persistent log");
            }
            continue;
        }

        try {
            const auto folder = winrt::Windows::Storage::ApplicationData::Current().LocalFolder();
            const auto file = co_await folder.CreateFileAsync(
                kPersistentLogName,
                winrt::Windows::Storage::CreationCollisionOption::OpenIfExists);
            co_await winrt::Windows::Storage::FileIO::AppendTextAsync(
                file,
                winrt::hstring(text));
            persistent_error_.store(S_OK, std::memory_order_relaxed);
        } catch (const winrt::hresult_error& error) {
            SetPersistentError(error.code(), L"AppendTextAsync");
        } catch (const std::bad_alloc&) {
            persistent_error_.store(E_OUTOFMEMORY, std::memory_order_relaxed);
            if (registration_error_ == ERROR_SUCCESS) {
                EmitEtw<kErrorLevel, kGeneralKeyword>(
                    L"diagnostics: persistent log allocation failed");
            }
        }
    }
}

void Diagnostics::SetPersistentError(
    HRESULT error,
    std::wstring_view operation) noexcept {
    persistent_error_.store(error, std::memory_order_relaxed);
    if (registration_error_ != ERROR_SUCCESS) {
        return;
    }
    try {
        const std::wstring detail = std::format(
            L"diagnostics: persistent log operation={} failed hr=0x{:08X}",
            operation,
            static_cast<uint32_t>(error));
        EmitEtw<kErrorLevel, kGeneralKeyword>(detail);
    } catch (const std::bad_alloc&) {
        EmitEtw<kErrorLevel, kGeneralKeyword>(
            L"diagnostics: persistent log failed and detail allocation failed");
    }
}

std::wstring Diagnostics::Timestamp(std::wstring_view message) {
    SYSTEMTIME time{};
    ::GetLocalTime(&time);
    return std::format(
        L"{:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:03} {}\r\n",
        time.wYear,
        time.wMonth,
        time.wDay,
        time.wHour,
        time.wMinute,
        time.wSecond,
        time.wMilliseconds,
        message);
}

} // namespace qiqi::host
