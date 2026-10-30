#include "common/file_util.h"
#include "common/logger.h"
#include "common/scoped_handle.h"

#include <windows.h>
#include <gsl/gsl>
#include <array>
#include <string>

namespace qiqi {

std::string WideToUtf8(std::wstring_view wide) {
    if (wide.empty()) {
        return {};
    }
    const int len = ::WideCharToMultiByte(
        CP_UTF8, 0, wide.data(), gsl::narrow_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    if (len <= 0) {
        return {};
    }
    std::string result(gsl::narrow_cast<size_t>(len), '\0');
    ::WideCharToMultiByte(
        CP_UTF8, 0, wide.data(), gsl::narrow_cast<int>(wide.size()), result.data(), len, nullptr, nullptr);
    return result;
}

std::wstring Utf8ToWide(std::string_view utf8) {
    if (utf8.empty()) {
        return {};
    }
    const int len = ::MultiByteToWideChar(
        CP_UTF8, 0, utf8.data(), gsl::narrow_cast<int>(utf8.size()), nullptr, 0);
    if (len <= 0) {
        return {};
    }
    std::wstring result(gsl::narrow_cast<size_t>(len), L'\0');
    ::MultiByteToWideChar(
        CP_UTF8, 0, utf8.data(), gsl::narrow_cast<int>(utf8.size()), result.data(), len);
    return result;
}

std::filesystem::path GetModuleFilePath(void* module_handle) noexcept {
    try {
        std::array<wchar_t, MAX_PATH> module_path{};
        if (::GetModuleFileNameW(
                static_cast<HMODULE>(module_handle),
                module_path.data(),
                gsl::narrow_cast<DWORD>(module_path.size())) == 0) {
            return {};
        }
        return std::filesystem::path(module_path.data());
    } catch (...) {
        return {};
    }
}

std::filesystem::path GetAppDataDirectory() {
    std::array<wchar_t, MAX_PATH> env_buf{};
    const DWORD env_len = ::GetEnvironmentVariableW(
        L"LOCALAPPDATA", env_buf.data(), gsl::narrow_cast<DWORD>(env_buf.size()));
    std::filesystem::path base_dir;
    if (env_len > 0 && env_len < env_buf.size()) {
        base_dir = env_buf.data();
    } else {
        std::array<wchar_t, MAX_PATH> temp_path{};
        if (::GetTempPathW(gsl::narrow_cast<DWORD>(temp_path.size()), temp_path.data()) > 0) {
            base_dir = temp_path.data();
        }
    }
    if (base_dir.empty()) {
        return {};
    }
    return base_dir / L"QiQiIME";
}

bool ReadFileToUtf8String(const std::filesystem::path& file_path, std::string& out_content) {
    // Open with least privilege: read-only access, read-only sharing, anonymous SQOS to prevent pipe impersonation,
    // and verify the target is a regular disk file.
    const ScopedFileHandle file(::CreateFileW(
        file_path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN | SECURITY_SQOS_PRESENT | SECURITY_ANONYMOUS,
        nullptr));
    if (!file.IsValid() || ::GetFileType(file.Get()) != FILE_TYPE_DISK) {
        QIQI_LOG_DEBUG("ReadFileToUtf8String: could not open disk file '{}' (error={})",
                       WideToUtf8(file_path.wstring()), ::GetLastError());
        return false;
    }

    LARGE_INTEGER file_size{};
    constexpr LONGLONG kMaxAllowedReadBytes = 64LL * 1024LL * 1024LL; // 64 MB safety ceiling
    if (!::GetFileSizeEx(file.Get(), &file_size) ||
        file_size.QuadPart < 0 ||
        file_size.QuadPart > kMaxAllowedReadBytes) {
        QIQI_LOG_WARN("ReadFileToUtf8String: invalid or oversize file ({} bytes) for '{}'",
                      file_size.QuadPart, WideToUtf8(file_path.wstring()));
        return false;
    }

    out_content.clear();
    if (file_size.QuadPart == 0) {
        return true;
    }

    out_content.resize(gsl::narrow_cast<size_t>(file_size.QuadPart));
    DWORD total_read = 0;
    const DWORD to_read = gsl::narrow_cast<DWORD>(file_size.QuadPart);
    if (!::ReadFile(file.Get(), out_content.data(), to_read, &total_read, nullptr) ||
        total_read != to_read) {
        QIQI_LOG_WARN("ReadFileToUtf8String: ReadFile failed for '{}' (expected={}, read={}, error={})",
                      WideToUtf8(file_path.wstring()), to_read, total_read, ::GetLastError());
        out_content.clear();
        return false;
    }
    return true;
}

bool WriteFileAtomically(const std::filesystem::path& target_path, std::span<const uint8_t> bytes) {
    try {
        std::error_code ec;
        if (std::filesystem::is_directory(target_path, ec)) {
            QIQI_LOG_WARN("WriteFileAtomically: target path '{}' is a directory",
                          WideToUtf8(target_path.wstring()));
            return false;
        }
        if (target_path.has_parent_path()) {
            std::filesystem::create_directories(target_path.parent_path(), ec);
        }

        const std::filesystem::path tmp_path =
            target_path.wstring() + L".tmp." + std::to_wstring(::GetCurrentProcessId());
        bool moved_ok = false;
        const auto cleanup_tmp = gsl::finally([&tmp_path, &moved_ok]() noexcept {
            if (!moved_ok) {
                std::error_code rm_ec;
                std::filesystem::remove(tmp_path, rm_ec);
            }
        });

        {
            const ScopedFileHandle file(::CreateFileW(
                tmp_path.c_str(),
                GENERIC_WRITE,
                0,
                nullptr,
                CREATE_ALWAYS,
                FILE_ATTRIBUTE_NORMAL | SECURITY_SQOS_PRESENT | SECURITY_ANONYMOUS,
                nullptr));
            if (!file.IsValid() || ::GetFileType(file.Get()) != FILE_TYPE_DISK) {
                QIQI_LOG_WARN("WriteFileAtomically: CreateFileW failed for '{}' (error={})",
                              WideToUtf8(tmp_path.wstring()), ::GetLastError());
                return false;
            }

            DWORD written = 0;
            const DWORD to_write = gsl::narrow_cast<DWORD>(bytes.size());
            if (to_write > 0 &&
                (!::WriteFile(file.Get(), bytes.data(), to_write, &written, nullptr) || written != to_write)) {
                QIQI_LOG_ERROR("WriteFileAtomically: WriteFile failed for '{}' (expected={}, written={}, error={})",
                               WideToUtf8(tmp_path.wstring()), to_write, written, ::GetLastError());
                return false;
            }
        }

        moved_ok = (::MoveFileExW(tmp_path.c_str(), target_path.c_str(), MOVEFILE_REPLACE_EXISTING) != FALSE);
        if (!moved_ok) {
            QIQI_LOG_WARN("WriteFileAtomically: MoveFileExW '{}' -> '{}' failed (error={})",
                          WideToUtf8(tmp_path.wstring()), WideToUtf8(target_path.wstring()), ::GetLastError());
        }
        return moved_ok;
    } catch (...) {
        return false;
    }
}

} // namespace qiqi
