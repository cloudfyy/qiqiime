#pragma once
#include <cstdint>
#include <filesystem>
#include <format>
#include <memory>
#include <span>
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
void Log(Level level, std::format_string<Args...> fmt, Args&&... args) noexcept {
    if (level < GetLevel() || level == Level::off || !IsLogSinkActive()) {
        return;
    }
    try {
        const std::string msg = std::format(fmt, std::forward<Args>(args)...);
        WriteLogMessage(level, msg);
    } catch (...) {
    }
}

// Safely unregister the Windows ETW TraceLogging provider on dynamic DLL unload (when not in process termination).
void Shutdown() noexcept;

// Convert wide string (UTF-16 on Windows) to UTF-8 for log formatting.
std::string WideToUtf8(std::wstring_view wide);

// Convert UTF-8 string to Windows wide string (UTF-16).
std::wstring Utf8ToWide(std::string_view utf8);

// Query the full filesystem path of a loaded module (exe or dll). Returns empty path on failure.
std::filesystem::path GetModuleFilePath(void* module_handle) noexcept;

// Return the base user data directory: %LOCALAPPDATA%\QiQiIME (or %TEMP%\QiQiIME fallback).
std::filesystem::path GetAppDataDirectory();

// Strip leading UTF-8 BOM (\xEF\xBB\xBF) if present.
constexpr std::string_view StripUtf8Bom(std::string_view sv) noexcept {
    if (sv.starts_with("\xEF\xBB\xBF")) {
        sv.remove_prefix(3);
    }
    return sv;
}

// Read an entire file into a std::string in one pass. Returns false if file cannot be opened.
bool ReadFileToUtf8String(const std::filesystem::path& file_path, std::string& out_content);

// Write a byte buffer atomically to target_path via an exclusive-write temporary file + MoveFileExW,
// using ScopedFileHandle RAII and gsl::finally to guarantee temporary file cleanup on any error or exception.
bool WriteFileAtomically(const std::filesystem::path& target_path, std::span<const uint8_t> bytes);

} // namespace qiqi::log

#define QIQI_LOG_TRACE(...) do { try { ::qiqi::log::Log(::qiqi::log::Level::trace, __VA_ARGS__); } catch (...) {} } while (false)
#define QIQI_LOG_DEBUG(...) do { try { ::qiqi::log::Log(::qiqi::log::Level::debug, __VA_ARGS__); } catch (...) {} } while (false)
#define QIQI_LOG_INFO(...)  do { try { ::qiqi::log::Log(::qiqi::log::Level::info,  __VA_ARGS__); } catch (...) {} } while (false)
#define QIQI_LOG_WARN(...)  do { try { ::qiqi::log::Log(::qiqi::log::Level::warn,  __VA_ARGS__); } catch (...) {} } while (false)
#define QIQI_LOG_ERROR(...) do { try { ::qiqi::log::Log(::qiqi::log::Level::err,   __VA_ARGS__); } catch (...) {} } while (false)

