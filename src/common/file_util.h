#pragma once
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace qiqi {

// Convert wide string (UTF-16 on Windows) to UTF-8.
std::string WideToUtf8(std::wstring_view wide) noexcept;

// Convert UTF-8 string to Windows wide string (UTF-16).
std::wstring Utf8ToWide(std::string_view utf8) noexcept;

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

} // namespace qiqi
