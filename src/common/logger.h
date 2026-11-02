#pragma once
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

namespace qiqi::log {

enum class Level : uint8_t {
    trace = 0,
    debug = 1,
    info = 2,
    warn = 3,
    err = 4,
    critical = 5,
    off = 6
};

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

// Return whether a log entry at `level` should be formatted and emitted for any active sink.
bool ShouldLog(Level level) noexcept;

// Emit a pre-formatted UTF-8 log entry to Windows ETW TraceLogging (and debugger when attached).
void WriteLogMessage(Level level, std::string_view message) noexcept;

// Preserve the result of a COM operation while diagnosing only failed HRESULTs.
bool CheckHResult(int32_t hr, std::string_view operation, Level level = Level::warn) noexcept;
void ReportLoggingFailure() noexcept;

template <typename... Args>
void Log(Level level, std::string_view fmt, Args&&... args) noexcept {
    if (!ShouldLog(level)) {
        return;
    }
    try {
        const std::string msg = std::vformat(fmt, std::make_format_args(args...));
        WriteLogMessage(level, msg);
    } catch (...) {
        ReportLoggingFailure();
    }
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
