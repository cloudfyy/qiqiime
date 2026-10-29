#pragma once
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <spdlog/spdlog.h>

namespace qiqi::log {

// Initialize the global QiQiIME logger.
// If log_file_path is empty, defaults to %LOCALAPPDATA%\QiQiIME\logs\qiqiime.log.
void InitLogger(const std::filesystem::path& log_file_path = {},
                spdlog::level::level_enum level = spdlog::level::info);

// Retrieve the global logger instance (lazily initialized if InitLogger was not called yet).
const std::shared_ptr<spdlog::logger>& GetLogger();

// Dynamically change log level.
void SetLevel(spdlog::level::level_enum level);

// Flush buffered log messages.
void Flush();

// Convert wide string (UTF-16 on Windows) to UTF-8 for spdlog formatting.
std::string WideToUtf8(std::wstring_view wide);

// Convert UTF-8 string to Windows wide string (UTF-16).
std::wstring Utf8ToWide(std::string_view utf8);

// Query the full filesystem path of a loaded module (exe or dll). Returns empty path on failure.
std::filesystem::path GetModuleFilePath(void* module_handle) noexcept;

// Return the base user data directory: %LOCALAPPDATA%\QiQiIME (or %TEMP%\QiQiIME fallback).
std::filesystem::path GetAppDataDirectory();

} // namespace qiqi::log

#define QIQI_LOG_TRACE(...) ::qiqi::log::GetLogger()->trace(__VA_ARGS__)
#define QIQI_LOG_DEBUG(...) ::qiqi::log::GetLogger()->debug(__VA_ARGS__)
#define QIQI_LOG_INFO(...)  ::qiqi::log::GetLogger()->info(__VA_ARGS__)
#define QIQI_LOG_WARN(...)  ::qiqi::log::GetLogger()->warn(__VA_ARGS__)
#define QIQI_LOG_ERROR(...) ::qiqi::log::GetLogger()->error(__VA_ARGS__)
