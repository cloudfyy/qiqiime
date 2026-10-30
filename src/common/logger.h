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

// Initialize the global QiQiIME ETW TraceLogging provider and set the active log level.
void InitLogger(Level level = Level::info) noexcept;

// Query or dynamically change the active log level.
Level GetLevel() noexcept;
void SetLevel(Level level) noexcept;

// Query whether the Windows ETW TraceLogging provider is registered.
bool IsEtwProviderRegistered() noexcept;

// Return whether any log consumer (active ETW session or attached debugger) is currently listening.
bool IsLogSinkActive() noexcept;

// Emit a pre-formatted UTF-8 log entry to Windows ETW TraceLogging (and debugger when attached).
void WriteLogMessage(Level level, std::string_view message) noexcept;

template <typename... Args>
void Log(Level level, std::string_view fmt, Args&&... args) noexcept {
    if (level < GetLevel() || level == Level::off || !IsLogSinkActive()) {
        return;
    }
    try {
        const std::string msg = std::vformat(fmt, std::make_format_args(args...));
        WriteLogMessage(level, msg);
    } catch (...) {
    }
}

// Safely unregister the Windows ETW TraceLogging provider on dynamic DLL unload (when not in process termination).
void Shutdown() noexcept;

} // namespace qiqi::log

#define QIQI_LOG_TRACE(...)    ::qiqi::log::Log(::qiqi::log::Level::trace,    __VA_ARGS__)
#define QIQI_LOG_DEBUG(...)    ::qiqi::log::Log(::qiqi::log::Level::debug,    __VA_ARGS__)
#define QIQI_LOG_INFO(...)     ::qiqi::log::Log(::qiqi::log::Level::info,     __VA_ARGS__)
#define QIQI_LOG_WARN(...)     ::qiqi::log::Log(::qiqi::log::Level::warn,     __VA_ARGS__)
#define QIQI_LOG_ERROR(...)    ::qiqi::log::Log(::qiqi::log::Level::err,      __VA_ARGS__)
#define QIQI_LOG_CRITICAL(...) ::qiqi::log::Log(::qiqi::log::Level::critical, __VA_ARGS__)

