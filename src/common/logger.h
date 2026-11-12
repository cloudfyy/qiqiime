// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include <windows.h>
#include <cguid.h>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <utility>

namespace qiqi::metrics { struct Event; }

namespace qiqi::log {

inline constexpr GUID kProviderId =
    {0x8e7b9d31, 0x5a42, 0x4c81, {0x9f, 0x11, 0x20, 0x26, 0x10, 0x29, 0x00, 0x99}};

enum class Level : uint8_t {
    trace = 0,
    debug = 1,
    info = 2,
    warn = 3,
    err = 4,
    critical = 5,
    off = 6
};

// Non-zero ETW provider keywords (low 48 bits are provider-defined).
enum class Keyword : uint64_t {
    General     = 0x0001,
    Lifecycle   = 0x0002,
    Input       = 0x0004,
    Composition = 0x0008,
    Dictionary  = 0x0010,
    Sync        = 0x0020,
    UI          = 0x0040,
    Config      = 0x0080,
    Metrics     = 0x0100,
    Logs        = 0x00FF,
    All         = 0x01FF
};

// Standard ETW opcodes matching WINEVENT_OPCODE_INFO / START / STOP.
enum class Opcode : uint8_t {
    Info  = 0,
    Start = 1,
    Stop  = 2
};

struct LogContext {
    Keyword keyword = Keyword::General;
    Opcode opcode = Opcode::Info;
    const GUID* activity_id = nullptr;
    const GUID* related_activity_id = nullptr;
};

constexpr Keyword NormalizeKeyword(Keyword keyword) noexcept {
    switch (keyword) {
        case Keyword::General:
        case Keyword::Lifecycle:
        case Keyword::Input:
        case Keyword::Composition:
        case Keyword::Dictionary:
        case Keyword::Sync:
        case Keyword::UI:
        case Keyword::Config:
        case Keyword::Metrics:
            return keyword;
        default:
            return Keyword::General;
    }
}

constexpr Opcode NormalizeOpcode(Opcode opcode) noexcept {
    switch (opcode) {
        case Opcode::Info:
        case Opcode::Start:
        case Opcode::Stop:
            return opcode;
        default:
            return Opcode::Info;
    }
}

constexpr std::string_view KeywordTag(Keyword keyword) noexcept {
    switch (NormalizeKeyword(keyword)) {
        case Keyword::Lifecycle:   return "lifecycle";
        case Keyword::Input:       return "input";
        case Keyword::Composition: return "composition";
        case Keyword::Dictionary:  return "dictionary";
        case Keyword::Sync:        return "sync";
        case Keyword::UI:          return "ui";
        case Keyword::Config:      return "config";
        case Keyword::Metrics:     return "metrics";
        case Keyword::General:
        default:                   return "general";
    }
}

constexpr std::wstring_view KeywordNameW( uint64_t keyword) noexcept {
    switch (keyword) {
        case std::to_underlying(Keyword::General):     return L"General";
        case std::to_underlying(Keyword::Lifecycle):   return L"Lifecycle";
        case std::to_underlying(Keyword::Input):       return L"Input";
        case std::to_underlying(Keyword::Composition): return L"Composition";
        case std::to_underlying(Keyword::Dictionary):  return L"Dictionary";
        case std::to_underlying(Keyword::Sync):        return L"Sync";
        case std::to_underlying(Keyword::UI):          return L"UI";
        case std::to_underlying(Keyword::Config):      return L"Config";
        case std::to_underlying(Keyword::Metrics):     return L"Metrics";
        default:                                       return L"General";
    }
}

constexpr std::string_view OpcodeTag(Opcode opcode) noexcept {
    switch (NormalizeOpcode(opcode)) {
        case Opcode::Start: return "start";
        case Opcode::Stop:  return "stop";
        case Opcode::Info:
        default:            return "info";
    }
}

constexpr std::wstring_view OpcodeNameW(uint8_t opcode) noexcept {
    switch (opcode) {
        case std::to_underlying(Opcode::Start): return L"Start";
        case std::to_underlying(Opcode::Stop):  return L"Stop";
        case std::to_underlying(Opcode::Info):
        default:                                return L"Info";
    }
}

// Create a locally-unique activity ID without modifying the calling thread's implicit ETW activity ID.
GUID CreateActivityId() noexcept;
ULONG CreateActivityIdResult(GUID& id) noexcept;

// Metrics are ETW-only, independent of LogLevel; these queries never initialize the provider.
bool MetricsEnabled() noexcept;
uint64_t MetricsCaptureGeneration() noexcept;
void WriteMetric(const metrics::Event& event) noexcept;

// Initialize the module's shared ETW TraceLogging provider and set the active log level.
void InitLogger(Level level = Level::info) noexcept;

// Register once per module, outside DllMain, without changing the shared log level. Failure is diagnosed.
bool EnsureInitialized() noexcept;

// Query or dynamically change the active log level.
Level GetLevel() noexcept;
void SetLevel(Level level) noexcept;

// Query registration without initializing or changing provider state.
bool IsEtwProviderRegistered() noexcept;

// Return whether any log consumer (active ETW session or attached debugger) is currently listening.
bool IsLogSinkActive() noexcept;

// Return whether a log entry at `level` and `keyword` should be formatted and emitted for any active sink.
bool ShouldLog(Level level, Keyword keyword = Keyword::General) noexcept;

// Emit a pre-formatted UTF-8 log entry to Windows ETW TraceLogging (and debugger when attached).
void WriteLogMessage(Level level, std::string_view message, const LogContext& context = {}) noexcept;

// Preserve the result of a COM operation while diagnosing only failed HRESULTs.
bool CheckHResult(
    int32_t hr,
    std::string_view operation,
    Level level = Level::warn,
    const LogContext& context = {}) noexcept;
void ReportLoggingFailure() noexcept;

template <typename... Args>
void LogWithContext(Level level, const LogContext& context, std::string_view fmt, Args&&... args) noexcept {
    if (!ShouldLog(level, context.keyword)) {
        return;
    }
    try {
        const std::string msg = std::vformat(fmt, std::make_format_args(args...));
        WriteLogMessage(level, msg, context);
    } catch (...) {
        ReportLoggingFailure();
    }
}

template <typename... Args>
void Log(Level level, std::string_view fmt, Args&&... args) noexcept {
    LogWithContext(level, LogContext{}, fmt, std::forward<Args>(args)...);
}

// Module teardown only: callers must be quiescent. Does not initialize or acquire the initialization lock.
void Shutdown() noexcept;

} // namespace qiqi::log

#define QIQI_LOG_TRACE(...)    ::qiqi::log::Log(::qiqi::log::Level::trace,    __VA_ARGS__)
#define QIQI_LOG_DEBUG(...)    ::qiqi::log::Log(::qiqi::log::Level::debug,    __VA_ARGS__)
#define QIQI_LOG_INFO(...)     ::qiqi::log::Log(::qiqi::log::Level::info,     __VA_ARGS__)
#define QIQI_LOG_WARN(...)     ::qiqi::log::Log(::qiqi::log::Level::warn,     __VA_ARGS__)
#define QIQI_LOG_ERROR(...)    ::qiqi::log::Log(::qiqi::log::Level::err,      __VA_ARGS__)
#define QIQI_LOG_CRITICAL(...) ::qiqi::log::Log(::qiqi::log::Level::critical, __VA_ARGS__)

#define QIQI_LOG_CTX(level, context, ...) ::qiqi::log::LogWithContext((level), (context), __VA_ARGS__)

#define QIQI_LOG_CAT_TRACE(keyword, ...) \
    ::qiqi::log::LogWithContext(::qiqi::log::Level::trace, ::qiqi::log::LogContext{(keyword)}, __VA_ARGS__)
#define QIQI_LOG_CAT_DEBUG(keyword, ...) \
    ::qiqi::log::LogWithContext(::qiqi::log::Level::debug, ::qiqi::log::LogContext{(keyword)}, __VA_ARGS__)
#define QIQI_LOG_CAT_INFO(keyword, ...) \
    ::qiqi::log::LogWithContext(::qiqi::log::Level::info,  ::qiqi::log::LogContext{(keyword)}, __VA_ARGS__)
#define QIQI_LOG_CAT_WARN(keyword, ...) \
    ::qiqi::log::LogWithContext(::qiqi::log::Level::warn,  ::qiqi::log::LogContext{(keyword)}, __VA_ARGS__)
#define QIQI_LOG_CAT_ERROR(keyword, ...) \
    ::qiqi::log::LogWithContext(::qiqi::log::Level::err,   ::qiqi::log::LogContext{(keyword)}, __VA_ARGS__)

#define QIQI_LOG_ACT_START(level, keyword, activity_ptr, related_ptr, ...) \
    ::qiqi::log::LogWithContext(                                           \
        (level),                                                           \
        ::qiqi::log::LogContext{(keyword), ::qiqi::log::Opcode::Start, (activity_ptr), (related_ptr)}, \
        __VA_ARGS__)

#define QIQI_LOG_ACT_INFO(level, keyword, activity_ptr, ...) \
    ::qiqi::log::LogWithContext(                             \
        (level),                                             \
        ::qiqi::log::LogContext{(keyword), ::qiqi::log::Opcode::Info, (activity_ptr), nullptr}, \
        __VA_ARGS__)

#define QIQI_LOG_ACT_STOP(level, keyword, activity_ptr, ...) \
    ::qiqi::log::LogWithContext(                             \
        (level),                                             \
        ::qiqi::log::LogContext{(keyword), ::qiqi::log::Opcode::Stop, (activity_ptr), nullptr}, \
        __VA_ARGS__)
