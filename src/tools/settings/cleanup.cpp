// SPDX-FileCopyrightText: 2026 cloudfyy
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tools/settings/cleanup.h"
#include "common/logger.h"
#include "common/scoped_handle.h"

#include <shlobj.h>
#include <gsl/gsl>

namespace qiqi::settings {
namespace {

constexpr bool Missing(DWORD error) noexcept {
    return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
}

HRESULT OpenDataItem(const std::filesystem::path& path, bool directory, bool remove, ScopedFileHandle& file) noexcept {
    // Deny renames while checking/deleting children, and never follow a junction or symlink.
    file.Reset(::CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES | (remove ? DELETE : 0),
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr));
    if (!file) {
        const DWORD error = ::GetLastError();
        return Missing(error) ? S_FALSE : HRESULT_FROM_WIN32(error);
    }
    BY_HANDLE_FILE_INFORMATION info{};
    if (!::GetFileInformationByHandle(file.Get(), &info)) return HRESULT_FROM_WIN32(::GetLastError());
    if ((info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
        ((info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) != directory) {
        return HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED);
    }
    return S_OK;
}

HRESULT RemoveDataFile(const std::filesystem::path& path) noexcept {
    ScopedFileHandle file;
    const HRESULT opened = OpenDataItem(path, false, true, file);
    if (opened != S_OK) return opened;
    FILE_DISPOSITION_INFO disposition{TRUE};
    return ::SetFileInformationByHandle(file.Get(), FileDispositionInfo, &disposition, sizeof(disposition))
        ? S_OK : HRESULT_FROM_WIN32(::GetLastError());
}

void RecordFailure(HRESULT hr, const char* operation, HRESULT& result) noexcept {
    if (FAILED(hr)) {
        std::ignore = log::CheckHResult(hr, operation);
        if (SUCCEEDED(result)) result = hr;
    }
}

HRESULT RemoveSyncData(const std::filesystem::path& directory) {
    ScopedFileHandle folder;
    const HRESULT opened = OpenDataItem(directory, true, false, folder);
    if (opened != S_OK) return opened;
    HRESULT result = S_OK;
    for (const auto* name : {L"snapshot.txt", L"snapshot.qdict", L"snapshot.qfreq",
                            L"source.meta", L"journal.bin", L"writer.lock"}) {
        RecordFailure(RemoveDataFile(directory / name), "Uninstall::RemoveSyncFile", result);
    }
    // Keep the directory itself and unrecognized files (including backups); do not recurse.
    return result;
}

} // namespace

HRESULT detail::RemoveUserDataAt(const std::filesystem::path& directory, const wchar_t* registry_subkey) {
    if (!directory.is_absolute() || directory.filename() != L"QiQiIME" || !registry_subkey || !*registry_subkey) {
        std::ignore = log::CheckHResult(E_INVALIDARG, "Uninstall::DataLocation");
        return E_INVALIDARG;
    }
    HRESULT result = S_OK;
    ScopedFileHandle folder;
    const HRESULT opened = OpenDataItem(directory, true, false, folder);
    RecordFailure(opened, "Uninstall::OpenDataDirectory", result);
    if (opened == S_OK) {
        for (const auto* name : {L"user_dict.txt", L"user_dict.qdict", L"phrase_freq.qfreq"}) {
            RecordFailure(RemoveDataFile(directory / name), "Uninstall::RemoveDataFile", result);
        }
        for (const auto* name : {L"user_dict.txt.sync", L"phrase_freq.qfreq.sync"}) {
            RecordFailure(RemoveSyncData(directory / name), "Uninstall::RemoveSyncData", result);
        }
    }
    const LSTATUS status = ::RegDeleteTreeW(HKEY_CURRENT_USER, registry_subkey);
    if (status != ERROR_SUCCESS && !Missing(gsl::narrow_cast<DWORD>(status))) {
        RecordFailure(HRESULT_FROM_WIN32(status), "Uninstall::RemoveConfiguration", result);
    }
    return result;
}

HRESULT RemoveCurrentUserData() {
    PWSTR local_app_data = nullptr;
    const HRESULT hr = ::SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DONT_VERIFY, nullptr, &local_app_data);
    const auto release = gsl::finally([&]() noexcept { ::CoTaskMemFree(local_app_data); });
    if (FAILED(hr)) {
        std::ignore = log::CheckHResult(hr, "Uninstall::LocalAppData");
        return hr;
    }
    return detail::RemoveUserDataAt(std::filesystem::path(local_app_data) / L"QiQiIME", L"Software\\QiQiIME");
}

} // namespace qiqi::settings
