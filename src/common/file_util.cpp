// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/file_util.h"
#include "common/logger.h"
#include "common/scoped_handle.h"

#include <windows.h>
#include <gsl/gsl>
#include <array>
#include <limits>
#include <new>
#include <string>

namespace qiqi {

std::string WideToUtf8(std::wstring_view wide) noexcept {
    try {
        if (wide.empty()) {
            return {};
        }
        const int len = ::WideCharToMultiByte(
            CP_UTF8, 0, wide.data(), gsl::narrow_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
        if (len <= 0) {
            const DWORD error = ::GetLastError();
            QIQI_LOG_WARN("WideCharToMultiByte(size) failed (error={})", error);
            return {};
        }
        std::string result(gsl::narrow_cast<size_t>(len), '\0');
        if (::WideCharToMultiByte(
                CP_UTF8, 0, wide.data(), gsl::narrow_cast<int>(wide.size()), result.data(), len, nullptr, nullptr) == 0) {
            const DWORD error = ::GetLastError();
            QIQI_LOG_WARN("WideCharToMultiByte(convert) failed (error={})", error);
            return {};
        }
        return result;
    } catch (...) {
        QIQI_LOG_ERROR("WideToUtf8 failed with a C++ exception");
        return {};
    }
}

std::wstring Utf8ToWide(std::string_view utf8) noexcept {
    try {
        std::wstring result;
        if (FAILED(Utf8ToWideResult(utf8, result, Utf8ErrorPolicy::Replace))) {
            return {};
        }
        return result;
    } catch (...) {
        QIQI_LOG_ERROR("Utf8ToWide failed with a C++ exception");
        return {};
    }
}

HRESULT Utf8ToWideResult(std::string_view utf8, std::wstring& out_text, Utf8ErrorPolicy policy) {
    if (policy != Utf8ErrorPolicy::Reject && policy != Utf8ErrorPolicy::Replace) {
        QIQI_LOG_WARN("Utf8ToWideResult received an invalid decoding policy");
        return E_INVALIDARG;
    }
    if (utf8.empty()) {
        out_text.clear();
        return S_OK;
    }
    if (utf8.size() > gsl::narrow_cast<size_t>((std::numeric_limits<int>::max)())) {
        QIQI_LOG_WARN("Utf8ToWideResult input exceeds Win32 conversion limits");
        return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
    }
    const DWORD flags = policy == Utf8ErrorPolicy::Reject ? MB_ERR_INVALID_CHARS : 0;
    const int size = gsl::narrow_cast<int>(utf8.size());
    const int count = ::MultiByteToWideChar(CP_UTF8, flags, utf8.data(), size, nullptr, 0);
    if (count == 0) {
        const DWORD error = ::GetLastError();
        QIQI_LOG_WARN("MultiByteToWideChar(size) failed (error={})", error);
        return error != ERROR_SUCCESS ? HRESULT_FROM_WIN32(error) : E_FAIL;
    }
    std::wstring decoded(gsl::narrow_cast<size_t>(count), L'\0');
    if (::MultiByteToWideChar(CP_UTF8, flags, utf8.data(), size, decoded.data(), count) == 0) {
        const DWORD error = ::GetLastError();
        QIQI_LOG_WARN("MultiByteToWideChar(convert) failed (error={})", error);
        return error != ERROR_SUCCESS ? HRESULT_FROM_WIN32(error) : E_FAIL;
    }
    out_text = std::move(decoded);
    return S_OK;
}

std::filesystem::path GetModuleFilePath(void* module_handle) noexcept {
    try {
        std::array<wchar_t, MAX_PATH> module_path{};
        const DWORD length = ::GetModuleFileNameW(
                static_cast<HMODULE>(module_handle),
                module_path.data(),
                gsl::narrow_cast<DWORD>(module_path.size()));
        if (length == 0 || length >= module_path.size()) {
            const DWORD error = ::GetLastError();
            QIQI_LOG_WARN("GetModuleFileNameW failed or buffer truncated (error={})", error);
            return {};
        }
        return std::filesystem::path(module_path.data());
    } catch (...) {
        QIQI_LOG_ERROR("GetModuleFilePath failed with a C++ exception");
        return {};
    }
}

std::filesystem::path GetAppDataDirectory() {
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        ::SetLastError(ERROR_SUCCESS);
        const DWORD length = ::GetEnvironmentVariableW(
            L"LOCALAPPDATA", buffer.data(), gsl::narrow_cast<DWORD>(buffer.size()));
        if (length >= buffer.size()) {
            buffer.resize(length);
            continue;
        }
        if (length > 0) {
            buffer.resize(length);
            return std::filesystem::path(buffer) / L"QiQiIME";
        }
        const DWORD error = ::GetLastError();
        if (error != ERROR_SUCCESS && error != ERROR_ENVVAR_NOT_FOUND) {
            QIQI_LOG_WARN("GetEnvironmentVariableW(LOCALAPPDATA) failed (error={})", error);
        }
        break;
    }
    for (;;) {
        const DWORD length = ::GetTempPathW(gsl::narrow_cast<DWORD>(buffer.size()), buffer.data());
        if (length >= buffer.size()) {
            buffer.resize(length);
            continue;
        }
        if (length == 0) {
            const DWORD error = ::GetLastError();
            QIQI_LOG_WARN("GetTempPathW failed (error={})", error);
            return {};
        }
        buffer.resize(length);
        return std::filesystem::path(buffer) / L"QiQiIME";
    }
}

bool ReadFileToUtf8String(const std::filesystem::path& file_path, std::string& out_content) {
    return SUCCEEDED(ReadFileToUtf8StringResult(file_path, out_content));
}

HRESULT ReadFileToUtf8StringResult(const std::filesystem::path& file_path, std::string& out_content) {
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
    if (!file.IsValid()) {
        const DWORD error = ::GetLastError();
        QIQI_LOG_DEBUG("ReadFileToUtf8String::CreateFileW failed (error={})", error);
        return HRESULT_FROM_WIN32(error);
    }
    if (::GetFileType(file.Get()) != FILE_TYPE_DISK) {
        QIQI_LOG_WARN("ReadFileToUtf8String: target is not a disk file");
        return E_INVALIDARG;
    }

    LARGE_INTEGER file_size{};
    constexpr LONGLONG kMaxAllowedReadBytes = 64LL * 1024LL * 1024LL; // 64 MB safety ceiling
    if (!::GetFileSizeEx(file.Get(), &file_size)) {
        const DWORD error = ::GetLastError();
        QIQI_LOG_WARN("ReadFileToUtf8String::GetFileSizeEx failed (error={})", error);
        return HRESULT_FROM_WIN32(error);
    }
    if (file_size.QuadPart < 0 ||
        file_size.QuadPart > kMaxAllowedReadBytes) {
        QIQI_LOG_WARN("ReadFileToUtf8String: invalid or oversize file ({} bytes)", file_size.QuadPart);
        return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
    }

    out_content.clear();
    if (file_size.QuadPart == 0) {
        return S_OK;
    }

    out_content.resize(gsl::narrow_cast<size_t>(file_size.QuadPart));
    DWORD total_read = 0;
    const DWORD to_read = gsl::narrow_cast<DWORD>(file_size.QuadPart);
    const BOOL read_ok = ::ReadFile(file.Get(), out_content.data(), to_read, &total_read, nullptr);
    const DWORD read_error = read_ok ? ERROR_SUCCESS : ::GetLastError();
    if (!read_ok || total_read != to_read) {
        QIQI_LOG_WARN("ReadFileToUtf8String::ReadFile incomplete (expected={}, read={}, error={})",
                      to_read, total_read, read_error);
        out_content.clear();
        return read_ok ? HRESULT_FROM_WIN32(ERROR_HANDLE_EOF) : HRESULT_FROM_WIN32(read_error);
    }
    return S_OK;
}

bool WriteFileAtomically(const std::filesystem::path& target_path, std::span<const uint8_t> bytes) {
    return SUCCEEDED(WriteFileAtomicallyResult(target_path, bytes));
}

HRESULT WriteFileAtomicallyResult(const std::filesystem::path& target_path, std::span<const uint8_t> bytes) {
    try {
        if (bytes.size() > MAXDWORD) {
            QIQI_LOG_ERROR("WriteFileAtomically: byte count exceeds DWORD capacity");
            return E_INVALIDARG;
        }
        std::error_code ec;
        if (std::filesystem::is_directory(target_path, ec)) {
            QIQI_LOG_WARN("WriteFileAtomically: target is a directory");
            return E_INVALIDARG;
        }
        if (target_path.has_parent_path()) {
            std::filesystem::create_directories(target_path.parent_path(), ec);
            if (ec) {
                QIQI_LOG_WARN("WriteFileAtomically::create_directories failed (error={})", ec.value());
                return HRESULT_FROM_WIN32(ec.value());
            }
        }

        const std::filesystem::path tmp_path =
            target_path.wstring() + L".tmp." + std::to_wstring(::GetCurrentProcessId());
        bool moved_ok = false;
        const auto cleanup_tmp = gsl::finally([&tmp_path, &moved_ok]() noexcept {
            if (!moved_ok && !::DeleteFileW(tmp_path.c_str())) {
                const DWORD error = ::GetLastError();
                if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND)
                    QIQI_LOG_WARN("WriteFileAtomically::DeleteFileW temporary file failed (error={})", error);
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
            if (!file.IsValid()) {
                const DWORD error = ::GetLastError();
                QIQI_LOG_WARN("WriteFileAtomically::CreateFileW failed (error={})", error);
                return HRESULT_FROM_WIN32(error);
            }
            if (::GetFileType(file.Get()) != FILE_TYPE_DISK) {
                QIQI_LOG_WARN("WriteFileAtomically: target is not a disk file");
                return E_INVALIDARG;
            }

            DWORD written = 0;
            const DWORD to_write = gsl::narrow_cast<DWORD>(bytes.size());
            if (to_write > 0) {
                const BOOL write_ok = ::WriteFile(file.Get(), bytes.data(), to_write, &written, nullptr);
                const DWORD error = write_ok ? ERROR_SUCCESS : ::GetLastError();
                if (!write_ok || written != to_write) {
                    QIQI_LOG_ERROR("WriteFileAtomically::WriteFile incomplete (expected={}, written={}, error={})",
                                   to_write, written, error);
                    return write_ok ? E_FAIL : HRESULT_FROM_WIN32(error);
                }
            }
        }

        moved_ok = (::MoveFileExW(tmp_path.c_str(), target_path.c_str(), MOVEFILE_REPLACE_EXISTING) != FALSE);
        if (!moved_ok) {
            const DWORD error = ::GetLastError();
            QIQI_LOG_WARN("WriteFileAtomically::MoveFileExW failed (error={})", error);
            return HRESULT_FROM_WIN32(error);
        }
        return S_OK;
    } catch (const std::bad_alloc&) {
        QIQI_LOG_ERROR("WriteFileAtomically failed with an allocation exception");
        return E_OUTOFMEMORY;
    } catch (...) {
        QIQI_LOG_ERROR("WriteFileAtomically failed with a C++ exception");
        return E_UNEXPECTED;
    }
}

} // namespace qiqi
